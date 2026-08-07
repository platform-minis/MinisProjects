#pragma once
/**
 * Hydra — pomiar napięcia (rozdz. 5).
 *
 * Interfejs zwraca miliwolty, nie surowe zliczenia: charakterystyka przetwornika
 * różni się między platformami (ESP32 ma nieliniowy ADC z fabryczną kalibracją
 * w eFuse, RP2 i STM32 są liniowe), a kod aplikacji ma tego nie widzieć.
 *
 * Dzielnik napięcia jest częścią kalibracji, a nie obowiązkiem wołającego —
 * pomiar baterii przez dzielnik 1:2 zwraca napięcie baterii, nie napięcie na
 * pinie. Przeliczenie idzie na liczbach całkowitych, żeby działało bez FPU.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/hal/Pin.hpp"

namespace hydra {
namespace hal {

/** Tłumienie wejściowe — na ESP32 wybiera zakres pomiarowy. */
enum class AdcAttenuation : u8 {
    Db0 = 0,   ///< ok. 0–950 mV
    Db2_5,     ///< ok. 0–1250 mV
    Db6,       ///< ok. 0–1750 mV
    Db11,      ///< ok. 0–3300 mV (domyślne)
};

/**
 * Kalibracja kanału. Napięcie źródła liczone jest jako:
 *   mV = (surowe_mV * dividerNum / dividerDen) * gainPermille / 1000 + offsetMv
 */
struct AdcCalibration {
    i16 offsetMv     = 0;
    u16 gainPermille = 1000;  ///< 1000 = wzmocnienie 1.0
    u16 dividerNum   = 1;     ///< licznik przełożenia dzielnika (np. 2 dla 1:2)
    u16 dividerDen   = 1;
};

struct AdcConfig {
    AdcAttenuation attenuation = AdcAttenuation::Db11;
    u8             samples     = 1;  ///< uśrednianie sprzętowe/programowe
};

class IAdc {
public:
    virtual ~IAdc() = default;

    virtual Status configure(PinNum pin, const AdcConfig& cfg) = 0;

    /** Surowe zliczenia przetwornika — do diagnostyki i wyznaczania kalibracji. */
    virtual Result<u16> readRaw(PinNum pin) = 0;

    /** Napięcie na pinie w miliwoltach, bez uwzględnienia dzielnika. */
    virtual Result<u16> readPinMv(PinNum pin) = 0;

    /** Napięcie źródła w miliwoltach: pomiar po kalibracji i dzielniku. */
    Result<u32> readMv(PinNum pin);

    /** Zapisuje kalibrację kanału. Tablica ma stały rozmiar (rozdz. 11). */
    Status setCalibration(PinNum pin, const AdcCalibration& cal);
    AdcCalibration calibration(PinNum pin) const;

    /** Rozdzielczość przetwornika w bitach. */
    virtual u8 resolutionBits() const = 0;

private:
    static constexpr u8 kMaxChannels = 8;
    struct Entry {
        PinNum         pin = kNoPin;
        AdcCalibration cal{};
    };
    Entry cal_[kMaxChannels];
};

}  // namespace hal
}  // namespace hydra
