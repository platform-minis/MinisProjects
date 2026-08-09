/**
 * Hydra — pady, elementy i potok.
 *
 * Nazwa pliku odbiega od nazwy nagłówka (`media/Pipeline.hpp`) celowo:
 * `sense/Pipeline.cpp` już istnieje, a testy budują się do jednego, płaskiego
 * katalogu. Dwa pliki o tej samej nazwie dawały tam jeden plik obiektowy —
 * kompilacja przechodziła, a konsolidator zgłaszał brak symboli z tego,
 * który przegrał.
 */

#include "hydra/media/Pipeline.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("media")

namespace hydra {
namespace media {

// ---------------------------------------------------------------------------
// Pad
// ---------------------------------------------------------------------------

u8 Pad::depth() const {
    const u8 head = head_.load(std::memory_order_acquire);
    const u8 tail = tail_.load(std::memory_order_acquire);
    return static_cast<u8>((head - tail) % (HYDRA_MEDIA_PAD_DEPTH + 1));
}

bool Pad::push(const Block& block, Block& evicted) {
    evicted = Block{};

    const u8 head = head_.load(std::memory_order_relaxed);
    const u8 tail = tail_.load(std::memory_order_acquire);
    const u8 used = static_cast<u8>((head - tail) % (HYDRA_MEDIA_PAD_DEPTH + 1));

    if (used >= HYDRA_MEDIA_PAD_DEPTH) {
        ++dropped_;
        switch (policy_) {
            case OverflowPolicy::Reject:
                // Blok zostaje u wołającego — źródło ma wstrzymać produkcję,
                // a nie oddać nam coś, czego nie przyjmiemy.
                return false;

            case OverflowPolicy::DropNewest:
                // Nowy blok idzie do wyrzucenia; kolejność zachowana, dziura
                // powstaje na końcu. Tak trzeba przy zapisie do pliku.
                evicted = block;
                return true;

            case OverflowPolicy::DropOldest: {
                // Najstarszy ustępuje miejsca. Przy podglądzie z kamery świeża
                // klatka jest warta więcej niż ta, której nikt już nie zobaczy.
                //
                // Wyjmuje tu **producent**, a normalnie robi to konsument —
                // ta polityka jest więc bezpieczna wyłącznie wtedy, gdy oba
                // końce są w tej samej domenie. Sprawdza to `Pipeline::link()`.
                const u8 t = tail_.load(std::memory_order_relaxed);
                evicted = ring_[t % HYDRA_MEDIA_PAD_DEPTH];
                tail_.store(static_cast<u8>((t + 1) % (HYDRA_MEDIA_PAD_DEPTH + 1)),
                            std::memory_order_release);
                break;
            }
        }
    }

    ring_[head % HYDRA_MEDIA_PAD_DEPTH] = block;
    head_.store(static_cast<u8>((head + 1) % (HYDRA_MEDIA_PAD_DEPTH + 1)),
                std::memory_order_release);

    const u8 now = depth();
    if (now > highWater_) highWater_ = now;
    return true;
}

bool Pad::pop(Block& out) {
    const u8 tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;

    out = ring_[tail % HYDRA_MEDIA_PAD_DEPTH];
    tail_.store(static_cast<u8>((tail + 1) % (HYDRA_MEDIA_PAD_DEPTH + 1)),
                std::memory_order_release);
    return true;
}

bool Pad::peek(Block& out) const {
    const u8 tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    out = ring_[tail % HYDRA_MEDIA_PAD_DEPTH];
    return true;
}

// ---------------------------------------------------------------------------
// Element
// ---------------------------------------------------------------------------

bool Element::emit(u8 outPad, const Block& block, Block& evicted) {
    evicted = Block{};
    OutputPad& out = outputs_[outPad];

    if (!out.connected()) {
        // Niepodłączone wyjście nie jest błędem — element diagnostyczny bywa
        // wpięty tylko czasem. Blok wraca do wołającego jako wyrzucony, żeby
        // miał co zwolnić.
        out.countDiscarded();
        evicted = block;
        return true;
    }

    if (!out.peer()->push(block, evicted)) return false;
    out.countSent();
    return true;
}

// ---------------------------------------------------------------------------
// Pipeline — budowa
// ---------------------------------------------------------------------------

Status Pipeline::add(Element& element, DomainId domain) {
    if (state_ != PipelineState::Idle) return fail(Err::Busy);
    if (count_ >= HYDRA_MEDIA_MAX_ELEMENTS) return fail(Err::OutOfMemory);
    if (domain >= HYDRA_MEDIA_MAX_DOMAINS) return fail(Err::BadArgument);

    element.setIndex(count_);
    entries_[count_].element = &element;
    entries_[count_].domain  = domain;
    ++count_;
    return ok();
}

Pipeline::Entry* Pipeline::entryFor(const Element& element) {
    for (u8 i = 0; i < count_; ++i) {
        if (entries_[i].element == &element) return &entries_[i];
    }
    return nullptr;
}

Status Pipeline::link(Element& from, u8 fromPad, Element& to, u8 toPad,
                      OverflowPolicy policy) {
    if (state_ != PipelineState::Idle) return fail(Err::Busy);
    if (fromPad >= HYDRA_MEDIA_MAX_PADS || toPad >= HYDRA_MEDIA_MAX_PADS) {
        return fail(Err::BadArgument);
    }

    Entry* source = entryFor(from);
    Entry* sink   = entryFor(to);
    if (source == nullptr || sink == nullptr) return fail(Err::NotFound);

    // Kolejność rejestracji jest kierunkiem przepływu — połączenie „pod prąd"
    // oznaczałoby element przetwarzany przed swoim dostawcą, czyli o jeden
    // krok opóźnienia na każdym obiegu. Zamiast to tolerować, mówimy wprost.
    if (source->element->index() >= sink->element->index()) {
        HYDRA_LOGE("połączenie pod prąd: %s (#%u) → %s (#%u); zarejestruj "
                   "elementy w kolejności przepływu",
                   from.name(), static_cast<unsigned>(from.index()),
                   to.name(), static_cast<unsigned>(to.index()));
        return fail(Err::BadArgument);
    }

    // DropOldest wyjmuje z ogona po stronie producenta, więc przy dwóch
    // wątkach jest wyścigiem z konsumentem. Między domenami wymuszamy
    // DropNewest — tam gubi się tak samo, ale bez uszkodzenia kolejki.
    if (source->domain != sink->domain && policy == OverflowPolicy::DropOldest) {
        HYDRA_LOGW("%s → %s przekracza domenę: polityka DropOldest zamieniona "
                   "na DropNewest (wyjmowanie z dwóch wątków)",
                   from.name(), to.name());
        policy = OverflowPolicy::DropNewest;
    }

    to.input(toPad).configure(MediaFormat{}, policy);
    from.output(fromPad).connect(to.input(toPad));
    return ok();
}

Status Pipeline::addPool(ByteSpan storage, u32 blockSize, u16 count, u8 alignment) {
    if (state_ != PipelineState::Idle) return fail(Err::Busy);
    if (poolCount_ >= HYDRA_MEDIA_MAX_POOLS) return fail(Err::OutOfMemory);

    HYDRA_CHECK(pools_[poolCount_].attach(poolCount_, storage, blockSize, count, alignment));
    ++poolCount_;
    return ok();
}

// ---------------------------------------------------------------------------
// Pipeline — przygotowanie
// ---------------------------------------------------------------------------

/**
 * Uzgadnianie formatów z prądem.
 *
 * Element dostaje format swojego wejścia i odpowiada, co wyjdzie. Źródła
 * dostają format pusty i wymyślają własny. Wynik zapisujemy na obu końcach
 * połączenia, żeby odbiorca znał format bez pytania nadawcy.
 */
Status Pipeline::negotiateAll() {
    for (u8 i = 0; i < count_; ++i) {
        Element& element = *entries_[i].element;

        for (u8 pad = 0; pad < element.outputCount(); ++pad) {
            // Format wejścia bierzemy z padu 0 — element o wielu wejściach
            // (mikser) i tak musi je sprowadzić do jednego, a robi to sam
            // w negotiate().
            const MediaFormat in = element.inputCount() > 0
                                       ? element.input(0).format()
                                       : MediaFormat{};

            auto out = element.negotiate(pad, in);
            if (!out) {
                HYDRA_LOGE("%s: nie umie wyprodukować formatu na wyjściu %u",
                           element.name(), static_cast<unsigned>(pad));
                return fail(out.error());
            }
            if (!out->valid()) {
                HYDRA_LOGE("%s: pusty format na wyjściu %u", element.name(),
                           static_cast<unsigned>(pad));
                return fail(Err::Protocol);
            }

            element.output(pad).setFormat(*out);
            if (Pad* peer = element.output(pad).peer(); peer != nullptr) {
                peer->configure(*out, peer->policy());
            }
        }
    }
    return ok();
}

/**
 * Przypisanie pul.
 *
 * Element deklaruje, jak dużych bloków potrzebuje; dostaje najciaśniejszą
 * pulę, która je pomieści. Współdzielenie puli między elementami o zbliżonym
 * rozmiarze bloku jest tu regułą, nie oszczędnością na siłę: osobna pula na
 * element oznaczałaby, że zapas jednego nie ratuje drugiego.
 */
Status Pipeline::bindPools() {
    for (u8 i = 0; i < count_; ++i) {
        Element& element = *entries_[i].element;

        for (u8 pad = 0; pad < element.outputCount(); ++pad) {
            const MemReq need = element.memoryRequest(pad);
            if (!need.valid()) { entries_[i].pool[pad] = kNoPool; continue; }

            BlockPool* chosen = poolAtLeast(need.blockSize);
            if (chosen == nullptr) {
                HYDRA_LOGE("%s: brak puli na bloki po %lu B — dodaj ją przez "
                           "Pipeline::addPool()", element.name(),
                           static_cast<unsigned long>(need.blockSize));
                return fail(Err::OutOfMemory);
            }
            // Za mało bloków to nie błąd konfiguracji, tylko przyszły underrun
            // w losowym momencie. Ostrzegamy teraz, bo później objawi się to
            // jako trzask w dźwięku bez żadnego tropu.
            if (chosen->capacityBlocks() < need.count) {
                HYDRA_LOGW("%s: chce %u bloków, pula ma %u — spodziewaj się "
                           "przerw przy obciążeniu", element.name(),
                           static_cast<unsigned>(need.count),
                           static_cast<unsigned>(chosen->capacityBlocks()));
            }
            entries_[i].pool[pad] = chosen->id();
        }
    }
    return ok();
}

Status Pipeline::prepare() {
    if (state_ != PipelineState::Idle) return fail(Err::Busy);
    if (count_ == 0) return fail(Err::NotInitialized);

    HYDRA_CHECK(negotiateAll());
    HYDRA_CHECK(bindPools());

    for (u8 i = 0; i < count_; ++i) {
        if (auto r = entries_[i].element->onPrepare(*this); !r) {
            HYDRA_LOGE("%s: przygotowanie nieudane (%s)",
                       entries_[i].element->name(), toString(r.error()));
            return r;
        }
    }

    transition(PipelineState::Ready);
    return ok();
}

Status Pipeline::start() {
    if (state_ == PipelineState::Running) return ok();
    if (state_ == PipelineState::Idle) HYDRA_CHECK(prepare());

    for (u8 i = 0; i < count_; ++i) {
        if (auto r = entries_[i].element->onStart(); !r) {
            // Zatrzymujemy to, co już ruszyło — tak samo jak App::begin().
            // Pół działającego potoku nie zostaje.
            for (u8 j = 0; j < i; ++j) entries_[j].element->onStop();
            return r;
        }
    }
    transition(PipelineState::Running);
    return ok();
}

void Pipeline::stop() {
    if (state_ == PipelineState::Idle) return;
    for (u8 i = count_; i > 0; --i) entries_[i - 1].element->onStop();

    // Bloki uwięzione w kolejkach wracają do pul. Bez tego drugi start po
    // zatrzymaniu zaczyna z pulą uszczuploną o zawartość kolejek.
    for (u8 i = 0; i < count_; ++i) {
        Element& element = *entries_[i].element;
        for (u8 pad = 0; pad < element.inputCount(); ++pad) {
            Block block;
            while (element.input(pad).drain(block)) {
                if (BlockPool* p = pool(block.pool); p != nullptr) p->release(block);
            }
        }
    }
    transition(PipelineState::Ready);
}

Status Pipeline::pause() {
    if (state_ != PipelineState::Running) return fail(Err::NotInitialized);
    transition(PipelineState::Paused);
    return ok();
}

Status Pipeline::resume() {
    if (state_ != PipelineState::Paused) return fail(Err::NotInitialized);
    transition(PipelineState::Running);
    return ok();
}

void Pipeline::transition(PipelineState next) {
    if (next == state_) return;
    const PipelineState from = state_;
    state_ = next;

    u8 domains = 0;
    for (u8 i = 0; i < count_; ++i) {
        if (entries_[i].domain + 1 > domains) domains = static_cast<u8>(entries_[i].domain + 1);
    }
    EventBus::publish(MediaStateChanged{from, next, count_, domains});
}

// ---------------------------------------------------------------------------
// Pipeline — praca
// ---------------------------------------------------------------------------

void Pipeline::step(DomainId domain, u64 nowUs) {
    if (state_ != PipelineState::Running) return;
    ++stats_.steps;

    // Z prądem, czyli w kolejności rejestracji. Odwrotna kolejność dokłada
    // jeden okres opóźnienia na każde połączenie — przy czterech elementach
    // i okresie 5 ms to 20 ms, słyszalne.
    for (u8 i = 0; i < count_; ++i) {
        if (entries_[i].domain != domain) continue;
        entries_[i].element->process(nowUs);
    }
}

void Pipeline::stepAll(u64 nowUs) {
    if (state_ != PipelineState::Running) return;
    ++stats_.steps;
    for (u8 i = 0; i < count_; ++i) entries_[i].element->process(nowUs);
}

BlockPool* Pipeline::pool(PoolId id) {
    return id < poolCount_ ? &pools_[id] : nullptr;
}

BlockPool* Pipeline::poolAtLeast(u32 bytes) {
    BlockPool* best = nullptr;
    for (u8 i = 0; i < poolCount_; ++i) {
        if (pools_[i].blockSize() < bytes) continue;
        if (best == nullptr || pools_[i].blockSize() < best->blockSize()) best = &pools_[i];
    }
    return best;
}

BlockPool* Pipeline::poolFor(const Element& element, u8 outPad) {
    for (u8 i = 0; i < count_; ++i) {
        if (entries_[i].element != &element) continue;
        return pool(entries_[i].pool[outPad]);
    }
    return nullptr;
}

void Pipeline::raise(MediaFault fault, const Element& element, u8 pad) {
    ++stats_.faults;
    const u8 slot = static_cast<u8>(fault);
    const u32 total = ++faultCount_[slot < 5 ? slot : 4];

    // Zdarzenie zamiast logu: przerwa w dźwięku trwa trzy milisekundy i w logu
    // ginie, a licznik na wykresie pokazuje, że urządzenie nie wyrabia.
    EventBus::publish(MediaFaultRaised{fault, element.index(), pad, total});
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
