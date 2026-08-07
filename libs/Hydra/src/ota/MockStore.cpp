/** Hydra — implementacja atrapy magazynu obrazu (build hostowy). */

#include "hydra/ota/Mock.hpp"

#if HYDRA_PLAT_HOST && HYDRA_ENABLE_OTA

#include <string.h>

namespace hydra {
namespace ota {
namespace mock {

Status MockFirmwareStore::begin(size_t imageSize) {
    if (imageSize == 0) return fail(Err::BadArgument);
    // Sprawdzenie rozmiaru przed pobraniem, a nie po — inaczej urządzenie
    // ściągałoby kilkaset kilobajtów, żeby dowiedzieć się, że się nie mieszczą.
    if (imageSize > capacity_) return fail(Err::OutOfRange);

    expected_  = imageSize;
    written_   = 0;
    open_      = true;
    finished_  = false;
    committed_ = false;
    return ok();
}

Status MockFirmwareStore::write(CByteSpan chunk) {
    if (!open_) return fail(Err::NotInitialized);

    if (writeError_ != Err::None) {
        const Err error = writeError_;
        writeError_     = Err::None;
        return fail(error);
    }
    if (written_ + chunk.size() > capacity_) return fail(Err::OutOfRange);

    memcpy(data_ + written_, chunk.data(), chunk.size());
    written_ += chunk.size();
    return ok();
}

Status MockFirmwareStore::finish() {
    if (!open_) return fail(Err::NotInitialized);
    // Obraz krótszy niż zapowiedziany jest niekompletny — przełączenie na
    // niego skończyłoby się urządzeniem, które nie wstaje.
    if (written_ != expected_) return fail(Err::Protocol);

    finished_ = true;
    open_     = false;
    return ok();
}

void MockFirmwareStore::abort() {
    open_     = false;
    finished_ = false;
    written_  = 0;
}

Status MockFirmwareStore::commit() {
    if (!finished_) return fail(Err::NotInitialized);
    committed_ = true;
    // Po przełączeniu obraz startuje warunkowo, aż potwierdzi sprawność.
    pending_   = true;
    return ok();
}

Status MockFirmwareStore::markValid() {
    if (!pending_) return ok();
    pending_ = false;
    ++validations_;
    return ok();
}

Status MockFirmwareStore::rollback() {
    pending_   = false;
    committed_ = false;
    ++rollbacks_;
    return ok();
}

void MockFirmwareStore::clear() {
    capacity_    = kCapacity;
    expected_    = 0;
    written_     = 0;
    open_        = false;
    finished_    = false;
    committed_   = false;
    pending_     = false;
    rollbacks_   = 0;
    validations_ = 0;
    writeError_  = Err::None;
    memset(data_, 0, sizeof(data_));
}

}  // namespace mock

// Na hoście magazynem jest atrapa — dzięki temu przykłady i testy działają
// bez pamięci Flash.
IFirmwareStore& defaultFirmwareStore() {
    static mock::MockFirmwareStore store;
    return store;
}

}  // namespace ota
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST && HYDRA_ENABLE_OTA
