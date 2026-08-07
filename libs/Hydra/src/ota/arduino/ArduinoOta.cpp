/**
 * Hydra — magazyn obrazu oprogramowania na Arduino (rozdz. 7.2).
 *
 * Trzeci katalog backendu z nagłówkami Arduino. Różnice między platformami
 * są tu największe w całym frameworku:
 *
 *   - ESP32 ma natywne partycje OTA i bibliotekę Update, która sama wybiera
 *     partycję nieaktywną i przełącza wskaźnik rozruchu,
 *   - arduino-pico wystawia to samo API Update nad pamięcią Flash RP2,
 *   - stm32duino nie ma warstwy OTA — obraz trzeba zapisać do drugiego banku
 *     przez API HAL producenta i przestawić bit wyboru banku.
 *
 * Tryb próbny wygląda inaczej na każdej platformie i jest jedynym miejscem,
 * w którym framework nie potrafi ukryć różnicy do końca — ESP-IDF ma na to
 * gotowy mechanizm, pozostałe wymagają wsparcia programu rozruchowego.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST && HYDRA_ENABLE_OTA

#include <Arduino.h>

#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
#  include <Update.h>
#endif
#if HYDRA_PLAT_ESP32
#  include <esp_ota_ops.h>
#endif

#include "hydra/ota/IFirmwareStore.hpp"

namespace hydra {
namespace ota {
namespace arduino {

class ArduinoFirmwareStore : public IFirmwareStore {
public:
    const char* name() const override {
#if HYDRA_PLAT_ESP32
        return "esp-ota";
#elif HYDRA_PLAT_RP2
        return "rp2-ota";
#else
        return "stm32-dualbank";
#endif
    }

    size_t capacity() const override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        // Rozmiar partycji nieaktywnej; biblioteka Update zna go sama.
        return static_cast<size_t>(UPDATE_SIZE_UNKNOWN);
#else
        return 0;
#endif
    }

    Status begin(size_t imageSize) override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        if (!Update.begin(imageSize, U_FLASH)) return fail(Err::OutOfRange);
        written_  = 0;
        open_     = true;
        expected_ = imageSize;
        return ok();
#else
        HYDRA_UNUSED(imageSize);
        return fail(Err::NotSupported);
#endif
    }

    Status write(CByteSpan chunk) override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        if (!open_) return fail(Err::NotInitialized);
        const size_t n = Update.write(const_cast<u8*>(chunk.data()), chunk.size());
        if (n != chunk.size()) return fail(Err::IoError);
        written_ += n;
        return ok();
#else
        HYDRA_UNUSED(chunk);
        return fail(Err::NotSupported);
#endif
    }

    Status finish() override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        if (!open_) return fail(Err::NotInitialized);
        if (written_ != expected_) return fail(Err::Protocol);
        // Update.end() sam sprawdza spójność nagłówka obrazu — obraz
        // przeznaczony na inny układ zostanie tu odrzucony.
        if (!Update.end(true)) return fail(Err::Protocol);
        open_ = false;
        return ok();
#else
        return fail(Err::NotSupported);
#endif
    }

    void abort() override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        if (open_) Update.abort();
#endif
        open_    = false;
        written_ = 0;
    }

    Status commit() override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        // Update.end(true) przestawił już wskaźnik rozruchu; przełączenie
        // zaczyna obowiązywać po restarcie.
        return ok();
#else
        return fail(Err::NotSupported);
#endif
    }

    bool pendingVerify() const override {
#if HYDRA_PLAT_ESP32
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t   state;
        if (esp_ota_get_state_partition(running, &state) != ESP_OK) return false;
        return state == ESP_OTA_IMG_PENDING_VERIFY;
#else
        // Poza ESP32 tryb próbny wymaga wsparcia programu rozruchowego,
        // którego stan nie jest widoczny z poziomu aplikacji.
        return false;
#endif
    }

    Status markValid() override {
#if HYDRA_PLAT_ESP32
        return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK ? ok() : fail(Err::IoError);
#else
        return ok();
#endif
    }

    Status rollback() override {
#if HYDRA_PLAT_ESP32
        // Nie wraca — układ restartuje się na poprzednim obrazie.
        esp_ota_mark_app_invalid_rollback_and_reboot();
        return ok();
#else
        return fail(Err::NotSupported);
#endif
    }

    size_t written() const override { return written_; }

private:
    size_t written_  = 0;
    size_t expected_ = 0;
    bool   open_     = false;
};

}  // namespace arduino

IFirmwareStore& defaultFirmwareStore() {
    static arduino::ArduinoFirmwareStore store;
    return store;
}

}  // namespace ota
}  // namespace hydra

#endif  // !HYDRA_PLAT_HOST && HYDRA_ENABLE_OTA
