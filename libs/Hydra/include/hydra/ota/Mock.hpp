#pragma once
/**
 * Hydra — atrapa magazynu obrazu dla buildu hostowego.
 *
 * Pozwala przejść całą ścieżkę aktualizacji — pobranie, weryfikację,
 * przełączenie, tryb próbny i powrót — bez pamięci Flash i bez restartu.
 * Symuluje też awarie: zapełnienie miejsca, błąd zapisu, obraz, który
 * po przełączeniu nie wstaje.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_PLAT_HOST && HYDRA_ENABLE_OTA

#include "hydra/ota/IFirmwareStore.hpp"

namespace hydra {
namespace ota {
namespace mock {

class MockFirmwareStore : public IFirmwareStore {
public:
    static constexpr size_t kCapacity = 8192;

    const char* name() const override { return "mock"; }
    size_t      capacity() const override { return capacity_; }

    Status begin(size_t imageSize) override;
    Status write(CByteSpan chunk) override;
    Status finish() override;
    void   abort() override;
    Status commit() override;

    bool   pendingVerify() const override { return pending_; }
    Status markValid() override;
    Status rollback() override;
    size_t written() const override { return written_; }

    // --- sterowanie atrapą ---
    /** Ogranicza dostępne miejsce, żeby sprawdzić odrzucenie za dużego obrazu. */
    void setCapacity(size_t bytes) { capacity_ = bytes; }
    /** Wymusza błąd przy kolejnym zapisie. */
    void failNextWrite(Err error) { writeError_ = error; }
    /** Ustawia stan po restarcie: obraz w trybie próbnym. */
    void setPendingVerify(bool pending) { pending_ = pending; }

    CByteSpan image() const { return CByteSpan{data_, written_}; }
    bool      committed() const { return committed_; }
    bool      finished() const { return finished_; }
    u32       rollbacks() const { return rollbacks_; }
    u32       validations() const { return validations_; }
    void      clear();

private:
    u8     data_[kCapacity] = {};
    size_t capacity_    = kCapacity;
    size_t expected_    = 0;
    size_t written_     = 0;
    bool   open_        = false;
    bool   finished_    = false;
    bool   committed_   = false;
    bool   pending_     = false;
    u32    rollbacks_   = 0;
    u32    validations_ = 0;
    Err    writeError_  = Err::None;
};

}  // namespace mock
}  // namespace ota
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST && HYDRA_ENABLE_OTA
