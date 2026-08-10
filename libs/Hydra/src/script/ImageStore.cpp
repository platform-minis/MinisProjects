/**
 * Hydra — magazyn obrazów skryptu.
 *
 * Skrót liczony jest w trakcie zbierania, nie po. Przy obrazie mieszczącym się
 * w RAM-ie różnica jest niewielka, ale reguła jest ta sama co w OTA: dane
 * przechodzą przez funkcję skrótu raz, w drodze, a nie w osobnym przebiegu
 * po pamięci, który trzeba dodatkowo zabezpieczyć przed zmianą pod ręką.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/ImageStore.hpp"

#include <string.h>

namespace hydra {
namespace script {

Status ImageStore::configure(const Config& cfg) {
    if (cfg.slotA.data() == nullptr || cfg.slotB.data() == nullptr) {
        return fail(Err::BadArgument);
    }
    // Sloty muszą być równe: obraz przyjęty do większego nie zmieściłby się
    // po wycofaniu i z powrotem, a taki błąd wyszedłby dopiero przy drugiej
    // aktualizacji w drugą stronę.
    if (cfg.slotA.size() != cfg.slotB.size()) return fail(Err::BadArgument);
    if (cfg.slotA.size() == 0) return fail(Err::BadArgument);

    slots_[0] = cfg.slotA;
    slots_[1] = cfg.slotB;
    return ok();
}

void ImageStore::adoptBuiltin(CByteSpan image) {
    active_ = ImageRef{};
    active_.data  = image.data();
    active_.bytes = image.size();
    active_.slot  = -1;
    if (image.data() != nullptr && image.size() > 0) {
        util::Sha256::hash(image, active_.sha);
    }
    previous_ = ImageRef{};
}

ByteSpan ImageStore::stagingBuffer() {
    const i8 slot = pickFreeSlot();
    if (slot < 0) return ByteSpan{};
    staging_ = slot;
    return slots_[slot];
}

Status ImageStore::adoptRestored(size_t bytes) {
    if (staging_ < 0) return fail(Err::NotInitialized);
    if (bytes == 0 || bytes > capacity()) return fail(Err::OutOfRange);

    ImageRef fresh{};
    fresh.data  = slots_[staging_].data();
    fresh.bytes = bytes;
    fresh.slot  = staging_;
    util::Sha256::hash(fresh.span(), fresh.sha);

    previous_ = active_;
    active_   = fresh;

    staging_  = -1;
    received_ = 0;
    expected_ = 0;
    verified_ = false;
    return ok();
}

i8 ImageStore::pickFreeSlot() const {
    for (i8 i = 0; i < 2; ++i) {
        if (active_.slot == i) continue;
        if (previous_.slot == i) continue;
        return i;
    }
    return -1;
}

Status ImageStore::beginTransfer(size_t totalBytes,
                                 const u8 expectedSha[util::kSha256Size]) {
    if (slots_[0].data() == nullptr) return fail(Err::NotInitialized);
    if (totalBytes == 0 || expectedSha == nullptr) return fail(Err::BadArgument);
    if (totalBytes > capacity()) return fail(Err::OutOfRange);

    const i8 slot = pickFreeSlot();
    // Oba sloty zajęte oznacza trwający okres próbny. Odmowa jest tu
    // zamierzona: obraz niepotwierdzony nie ma prawa wyprzeć jedynego,
    // o którym wiadomo, że wstaje.
    if (slot < 0) return fail(Err::Busy);

    staging_  = slot;
    expected_ = totalBytes;
    received_ = 0;
    nextSeq_  = 0;
    verified_ = false;
    memcpy(wantSha_, expectedSha, util::kSha256Size);

    ++stats_.transfers;
    return ok();
}

Status ImageStore::appendChunk(u32 seq, CByteSpan data) {
    if (staging_ < 0) return fail(Err::NotInitialized);
    if (seq != nextSeq_) return fail(Err::BadArgument);
    if (data.data() == nullptr || data.size() == 0) return fail(Err::BadArgument);
    if (received_ + data.size() > expected_) return fail(Err::OutOfRange);

    memcpy(slots_[staging_].data() + received_, data.data(), data.size());
    received_ += data.size();
    ++nextSeq_;
    return ok();
}

Status ImageStore::verifyStaged() {
    if (staging_ < 0) return fail(Err::NotInitialized);
    if (received_ != expected_) return fail(Err::BadArgument);

    u8 got[util::kSha256Size] = {};
    util::Sha256::hash(CByteSpan{slots_[staging_].data(), received_}, got);

    if (!util::Sha256::equal(got, wantSha_)) {
        ++stats_.rejects;
        abortTransfer();
        return fail(Err::BadArgument);
    }

    verified_ = true;
    return ok();
}

Result<ImageRef> ImageStore::activateStaged() {
    if (staging_ < 0 || !verified_) return unexpected(Err::NotInitialized);

    ImageRef fresh{};
    fresh.data  = slots_[staging_].data();
    fresh.bytes = received_;
    fresh.slot  = staging_;
    memcpy(fresh.sha, wantSha_, util::kSha256Size);

    previous_ = active_;
    active_   = fresh;

    staging_  = -1;
    verified_ = false;
    ++stats_.commits;
    return active_;
}

void ImageStore::abortTransfer() {
    if (staging_ < 0) return;
    staging_  = -1;
    received_ = 0;
    expected_ = 0;
    nextSeq_  = 0;
    verified_ = false;
    ++stats_.aborts;
}

Result<ImageRef> ImageStore::rollback() {
    if (!previous_.valid()) return unexpected(Err::NotFound);

    active_   = previous_;
    previous_ = ImageRef{};
    ++stats_.rollbacks;
    return active_;
}

void ImageStore::confirm() {
    previous_ = ImageRef{};
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
