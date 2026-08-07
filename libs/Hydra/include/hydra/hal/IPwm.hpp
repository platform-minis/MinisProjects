#pragma once
/**
 * Hydra — PWM: częstotliwość, wypełnienie, serwa (rozdz. 5).
 *
 * API operuje na jednostkach fizycznych, nie na surowych wartościach rejestrów:
 * wypełnienie podaje się w promilach (0–1000), a szerokość impulsu serwa
 * w mikrosekundach. Rozdzielczość sprzętowa jest szczegółem backendu —
 * ledcWrite na ESP32, PWM slice na RP2, timer na STM32.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/hal/Pin.hpp"

namespace hydra {
namespace hal {

/** Typowe zakresy impulsu dla serw modelarskich. */
constexpr u16 kServoMinUs     = 1000;
constexpr u16 kServoCenterUs  = 1500;
constexpr u16 kServoMaxUs     = 2000;
constexpr u32 kServoFreqHz    = 50;

class IPwm {
public:
    virtual ~IPwm() = default;

    /**
     * Przygotowuje kanał PWM na pinie. resolutionBits dotyczy sprzętu;
     * API użytkownika i tak posługuje się promilami.
     */
    virtual Status configure(PinNum pin, u32 freqHz, u8 resolutionBits = 10) = 0;

    /** Wypełnienie w promilach: 0 = stale nisko, 1000 = stale wysoko. */
    virtual Status setDutyPermille(PinNum pin, u16 permille) = 0;

    /** Zatrzymuje generowanie i zwalnia kanał. */
    virtual Status release(PinNum pin) = 0;

    /**
     * Szerokość impulsu w mikrosekundach — dla serw i sterowników ESC.
     * Domyślna implementacja przelicza na wypełnienie z aktualnej częstotliwości.
     */
    virtual Status writeMicroseconds(PinNum pin, u16 us);

    /** Częstotliwość ustawiona na kanale (0 = kanał nieskonfigurowany). */
    virtual u32 frequencyHz(PinNum pin) const = 0;
};

}  // namespace hal
}  // namespace hydra
