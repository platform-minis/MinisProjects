/** Hydra — pula bloków multimedialnych. */

#include "hydra/media/Block.hpp"

#if HYDRA_ENABLE_MEDIA

#include <string.h>

namespace hydra {
namespace media {

bool MediaFormat::equals(const MediaFormat& other) const {
    if (kind != other.kind) return false;
    if (kind == MediaKind::Audio) {
        return sampleRate == other.sampleRate &&
               sampleFormat == other.sampleFormat &&
               channels == other.channels;
    }
    if (kind == MediaKind::Video) {
        return frameFormat == other.frameFormat &&
               width == other.width && height == other.height;
        // Liczba klatek celowo poza porównaniem: kamera podaje ją orientacyjnie
        // i potrafi się wahać, a niezgodność tutaj oznaczałaby potok, który
        // odmawia startu z powodu zmiennego oświetlenia.
    }
    return true;
}

Status BlockPool::attach(PoolId id, ByteSpan storage, u32 blockSize, u16 count,
                         u8 alignment) {
    if (blockSize == 0 || count == 0) return fail(Err::BadArgument);
    if (count > kMaxSlots) return fail(Err::OutOfRange);
    if (alignment == 0) alignment = 1;

    // Początek wyrównujemy w górę, bo bufor DMA o niewyrównanym adresie
    // kończy się na ESP32 błędem magistrali, a na Cortex-A cichym zapisem
    // pod sąsiedni adres przy czyszczeniu pamięci podręcznej.
    u8* base = storage.data();
    const size_t misaligned = reinterpret_cast<uintptr_t>(base) % alignment;
    if (misaligned != 0) base += alignment - misaligned;

    const u32 stride = (blockSize + alignment - 1u) / alignment * alignment;
    const size_t used = static_cast<size_t>(base - storage.data()) +
                        static_cast<size_t>(stride) * count;
    if (used > storage.size()) return fail(Err::OutOfRange);

    base_      = base;
    blockSize_ = stride;
    count_     = count;
    free_      = count;
    lowWater_  = count;
    id_        = id;
    exhausted_ = 0;

    for (u16 i = 0; i < count; ++i) {
        nextFree_[i] = static_cast<u16>(i + 1 < count ? i + 1 : kNoSlot);
        refs_[i] = 0;
    }
    head_ = 0;
    return ok();
}

Block BlockPool::acquire() {
    if (head_ == kNoSlot) {
        ++exhausted_;
        return Block{};
    }

    const u16 slot = head_;
    head_ = nextFree_[slot];
    refs_[slot] = 1;
    --free_;
    if (free_ < lowWater_) lowWater_ = free_;

    Block block;
    block.data     = base_ + static_cast<size_t>(slot) * blockSize_;
    block.capacity = blockSize_;
    block.length   = 0;
    block.pts      = 0;
    block.pool     = id_;
    block.slot     = slot;
    block.flags    = 0;
    return block;
}

void BlockPool::release(Block& block) {
    // Zwolnienie bloku z innej puli albo już zwolnionego jest ignorowane, a nie
    // zgłaszane. W potoku z rozgałęzieniem obie gałęzie wołają release() na
    // uchwycie tego samego bloku i tak ma być — liczy się licznik odwołań.
    if (block.pool != id_ || block.slot >= count_) return;
    const u16 slot = block.slot;
    if (refs_[slot] == 0) return;

    if (--refs_[slot] > 0) { block = Block{}; return; }

    nextFree_[slot] = head_;
    head_ = slot;
    ++free_;
    block = Block{};
}

void BlockPool::retain(const Block& block) {
    if (block.pool != id_ || block.slot >= count_) return;
    if (refs_[block.slot] == 0) return;   // blok nie jest w obiegu
    ++refs_[block.slot];
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
