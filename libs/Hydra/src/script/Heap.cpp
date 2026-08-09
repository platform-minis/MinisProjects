/**
 * Hydra — alokator sterty skryptu.
 *
 * Lista niejawna z tagami granicznymi: pula jest w całości pokryta blokami,
 * każdy blok zna swój rozmiar i rozmiar poprzednika, a bloki wolne wiszą
 * dodatkowo na dwukierunkowej liście, której ogniwa leżą w ich własnym ładunku.
 * Stąd narzut osiem bajtów na blok i scalanie w obie strony w czasie stałym.
 *
 * Wybór strategii: pierwszy pasujący (first fit). Najlepiej pasujący dawałby
 * mniejszą fragmentację przy wyraźnie wyższym koszcie każdego przydziału,
 * a Lua alokuje często i drobno — przy takim profilu ruchu first fit z natychmiastowym
 * scalaniem wygrywa. Fragmentację widać w `largestFree` i to ona, a nie
 * `used`, mówi, czy pula jest jeszcze zdrowa.
 */

#include "hydra/script/Heap.hpp"

#include <string.h>

namespace hydra {
namespace script {

namespace {

/** Zaokrąglenie w górę do wielokrotności wyrównania. */
inline u32 alignUp(u32 v) {
    return (v + (Heap::kAlign - 1)) & ~(Heap::kAlign - 1);
}

/** Rozmiar bloku potrzebny na ładunek `payload` bajtów. */
inline u32 blockFor(size_t payload) {
    u32 need = alignUp(static_cast<u32>(payload) + Heap::kHeaderSize);
    return need < Heap::kMinBlock ? Heap::kMinBlock : need;
}

}  // namespace

// ---------------------------------------------------------------------------
// Nawigacja po puli
// ---------------------------------------------------------------------------

Heap::Block* Heap::at(u32 offset) const {
    return reinterpret_cast<Block*>(base_ + offset);
}

u32 Heap::offsetOf(const Block* b) const {
    return static_cast<u32>(reinterpret_cast<const u8*>(b) - base_);
}

Heap::Block* Heap::nextPhysical(const Block* b) const {
    const u32 next = offsetOf(b) + blockSize(b);
    return next < size_ ? at(next) : nullptr;
}

Heap::Block* Heap::prevPhysical(const Block* b) const {
    return b->prev == 0 ? nullptr : at(offsetOf(b) - b->prev);
}

Heap::Links* Heap::linksOf(Block* b) const {
    return reinterpret_cast<Links*>(reinterpret_cast<u8*>(b) + kHeaderSize);
}

// ---------------------------------------------------------------------------
// Lista wolnych bloków
// ---------------------------------------------------------------------------

void Heap::listInsert(Block* b) {
    Links* l = linksOf(b);
    l->next = free_;
    l->prev = kNil;
    if (free_ != kNil) linksOf(at(free_))->prev = offsetOf(b);
    free_ = offsetOf(b);
}

void Heap::listRemove(Block* b) {
    Links* l = linksOf(b);
    if (l->prev != kNil) {
        linksOf(at(l->prev))->next = l->next;
    } else {
        free_ = l->next;
    }
    if (l->next != kNil) linksOf(at(l->next))->prev = l->prev;
    l->next = kNil;
    l->prev = kNil;
}

Heap::Block* Heap::findFit(u32 need) {
    for (u32 o = free_; o != kNil; o = linksOf(at(o))->next) {
        Block* b = at(o);
        if (blockSize(b) >= need) return b;
    }
    return nullptr;
}

/**
 * Odcina od bloku nadmiar, jeśli zostaje z niego użyteczny blok wolny.
 * Reszta trafia na listę wolnych; blok wejściowy zachowuje swój stan zajętości.
 *
 * Reszta jest od razu scalana z fizycznie następnym blokiem, jeśli ten też jest
 * wolny. Bez tego kroku niezmiennik „żadne dwa wolne bloki nie sąsiadują" łamie
 * się przy zmniejszaniu bloku zajętego, którego następnik był już wolny —
 * a wtedy pula rozsypuje się na drobiazgi mimo dużej ilości wolnej pamięci.
 */
void Heap::splitIfWorthwhile(Block* b, u32 need) {
    const u32 total = blockSize(b);
    if (total - need < kMinBlock) return;

    u32    restSize = total - need;
    Block* rest     = at(offsetOf(b) + need);
    setSize(rest, restSize, false);
    rest->prev = need;
    setSize(b, need, isUsed(b));

    if (Block* after = nextPhysical(rest); after != nullptr && !isUsed(after)) {
        listRemove(after);
        restSize += blockSize(after);
        setSize(rest, restSize, false);
    }

    // Blok za resztą musi poznać jej rozmiar, inaczej scalanie wstecz trafi
    // w środek cudzego ładunku.
    if (Block* after = nextPhysical(rest)) after->prev = restSize;

    listInsert(rest);
}

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status Heap::init(void* pool, size_t bytes) {
    if (pool == nullptr) return fail(Err::BadArgument);

    // Pula bywa polem struktury o mniejszym wyrównaniu — dociągamy początek
    // do ośmiu bajtów i o tyle samo skracamy długość.
    u8*          raw     = static_cast<u8*>(pool);
    const size_t misalign = reinterpret_cast<uintptr_t>(raw) % kAlign;
    const size_t shift    = misalign ? (kAlign - misalign) : 0;
    if (bytes <= shift) return fail(Err::BadArgument);

    base_ = raw + shift;
    const size_t usable = (bytes - shift) & ~static_cast<size_t>(kAlign - 1);
    if (usable < kMinBlock || usable > 0xFFFFFFF0u) return fail(Err::BadArgument);

    size_  = static_cast<u32>(usable);
    stats_ = Stats{};
    stats_.capacity = size_;

    Block* b = at(0);
    setSize(b, size_, false);
    b->prev = 0;
    free_   = kNil;
    listInsert(b);
    return ok();
}

void Heap::reset() {
    if (!ready()) return;
    const u32 capacity = size_;
    stats_ = Stats{};
    stats_.capacity = capacity;

    Block* b = at(0);
    setSize(b, size_, false);
    b->prev = 0;
    free_   = kNil;
    listInsert(b);
}

// ---------------------------------------------------------------------------
// Przydział i zwolnienie
// ---------------------------------------------------------------------------

void* Heap::allocate(size_t bytes) {
    if (!ready() || bytes == 0) return nullptr;
    ++stats_.requests;

    // Żądanie większe niż cała pula obcięłoby się przy zawężeniu do u32
    // i alokator oddałby blok mniejszy, niż proszono.
    if (bytes > size_) {
        ++stats_.failures;
        return nullptr;
    }

    const u32 need = blockFor(bytes);
    Block*    b    = findFit(need);
    if (b == nullptr) {
        ++stats_.failures;
        return nullptr;
    }

    listRemove(b);
    splitIfWorthwhile(b, need);
    setSize(b, blockSize(b), true);

    stats_.used += blockSize(b);
    if (stats_.used > stats_.peak) stats_.peak = stats_.used;
    return reinterpret_cast<u8*>(b) + kHeaderSize;
}

void Heap::release(void* ptr) {
    if (!ready() || ptr == nullptr) return;

    Block* b = reinterpret_cast<Block*>(static_cast<u8*>(ptr) - kHeaderSize);
    stats_.used -= blockSize(b);
    setSize(b, blockSize(b), false);

    // Scalanie w przód.
    if (Block* n = nextPhysical(b); n != nullptr && !isUsed(n)) {
        listRemove(n);
        const u32 merged = blockSize(b) + blockSize(n);
        setSize(b, merged, false);
        if (Block* after = nextPhysical(b)) after->prev = merged;
    }

    // Scalanie wstecz. Poprzednik jest już na liście wolnych, więc trzeba go
    // z niej zdjąć i wstawić z powrotem dopiero po zmianie rozmiaru — inaczej
    // pierwszy pasujący znalazłby blok o nieaktualnej pojemności.
    if (Block* p = prevPhysical(b); p != nullptr && !isUsed(p)) {
        listRemove(p);
        const u32 merged = blockSize(p) + blockSize(b);
        setSize(p, merged, false);
        if (Block* after = nextPhysical(p)) after->prev = merged;
        b = p;
    }

    listInsert(b);
}

void* Heap::reallocate(void* ptr, size_t oldBytes, size_t newBytes) {
    if (ptr == nullptr) return allocate(newBytes);
    if (newBytes == 0) {
        release(ptr);
        return nullptr;
    }
    if (!ready()) return nullptr;
    ++stats_.requests;
    if (newBytes > size_) {
        ++stats_.failures;
        return nullptr;
    }

    Block*    b       = reinterpret_cast<Block*>(static_cast<u8*>(ptr) - kHeaderSize);
    const u32 current = blockSize(b);
    const u32 need    = blockFor(newBytes);

    if (need <= current) {
        // Zmniejszenie — odcinamy nadmiar, o ile zostaje z niego użyteczny blok.
        stats_.used -= current;
        splitIfWorthwhile(b, need);
        stats_.used += blockSize(b);
        return ptr;
    }

    // Powiększenie bez przenoszenia, jeśli fizycznie następny blok jest wolny
    // i wystarczająco duży. To najczęstszy przypadek przy rosnącym buforze
    // napisu albo tablicy — przeniesienie kosztowałoby kopię całej zawartości.
    if (Block* n = nextPhysical(b); n != nullptr && !isUsed(n) && current + blockSize(n) >= need) {
        listRemove(n);
        const u32 merged = current + blockSize(n);
        setSize(b, merged, true);
        if (Block* after = nextPhysical(b)) after->prev = merged;

        stats_.used -= current;
        splitIfWorthwhile(b, need);
        stats_.used += blockSize(b);
        if (stats_.used > stats_.peak) stats_.peak = stats_.used;
        return ptr;
    }

    void* moved = allocate(newBytes);
    if (moved == nullptr) {
        ++stats_.failures;
        return nullptr;
    }
    const size_t copy = oldBytes < newBytes ? oldBytes : newBytes;
    memcpy(moved, ptr, copy);
    release(ptr);
    return moved;
}

// ---------------------------------------------------------------------------
// Diagnostyka
// ---------------------------------------------------------------------------

Heap::Stats Heap::stats() const {
    Stats s = stats_;
    if (!ready()) return s;

    for (u32 o = 0; o < size_;) {
        const Block* b = at(o);
        const u32    n = blockSize(b);
        if (n == 0) break;  // pula niespójna — validate() poda szczegóły
        if (isUsed(b)) {
            ++s.liveBlocks;
        } else {
            ++s.freeBlocks;
            const u32 payload = n - kHeaderSize;
            if (payload > s.largestFree) s.largestFree = payload;
        }
        o += n;
    }
    return s;
}

bool Heap::validate() const {
    if (!ready()) return false;

    u32 offset    = 0;
    u32 prevSize  = 0;
    u32 usedBytes = 0;
    u32 freeSeen  = 0;

    while (offset < size_) {
        const Block* b = at(offset);
        const u32    n = blockSize(b);

        if (n < kMinBlock || (n % kAlign) != 0) return false;
        if (offset + n > size_) return false;
        if (b->prev != prevSize) return false;

        if (isUsed(b)) {
            usedBytes += n;
        } else {
            ++freeSeen;
            // Dwa wolne bloki obok siebie oznaczają, że scalanie zawiodło.
            const u32 next = offset + n;
            if (next < size_ && !isUsed(at(next))) return false;
        }

        prevSize = n;
        offset  += n;
    }
    if (offset != size_) return false;
    if (usedBytes != stats_.used) return false;

    // Lista wolnych musi zawierać dokładnie te bloki, które fizycznie są wolne.
    u32 listed = 0;
    u32 back   = kNil;
    for (u32 o = free_; o != kNil;) {
        if (o >= size_ || (o % kAlign) != 0) return false;
        Block* b = at(o);
        if (isUsed(b)) return false;
        const Links* l = linksOf(b);
        if (l->prev != back) return false;
        if (++listed > freeSeen) return false;
        back = o;
        o    = l->next;
    }
    return listed == freeSeen;
}

}  // namespace script
}  // namespace hydra
