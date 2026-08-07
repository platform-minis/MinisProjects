#pragma once
/**
 * Hydra — INA219: pomiar prądu i napięcia na boczniku (I2C, 0x40–0x4F).
 *
 * Używany w Hydrze także jako element łańcucha bezpieczeństwa modułu motion
 * (limity prądu, rozdz. 9), dlatego domyślna konfiguracja stawia na szybki
 * odczyt, a nie na maksymalne uśrednianie.
 *
 * Kanały: [0] napięcie szyny [V], [1] prąd [mA], [2] moc [mW].
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/sense/ISensor.hpp"

namespace hydra {
namespace drivers {

class Ina219 : public sense::ISensor {
public:
    static constexpr u8 kDefaultAddress = 0x40;

    enum Reg : u8 {
        RegConfig      = 0x00,
        RegShuntVolt   = 0x01,
        RegBusVolt     = 0x02,
        RegPower       = 0x03,
        RegCurrent     = 0x04,
        RegCalibration = 0x05,
    };

    /** Wartość domyślna rejestru konfiguracji po resecie układu. */
    static constexpr u16 kConfigReset = 0x399F;

    /**
     * Parametry układu pomiarowego. Domyślnie bocznik 0,1 Ω i zakres 3,2 A —
     * typowa konfiguracja modułów INA219 sprzedawanych jako gotowe płytki.
     */
    struct Setup {
        float shuntOhms   = 0.1f;
        float maxAmperes  = 3.2f;
    };

    explicit Ina219(const Setup& setup);
    Ina219();

    const char*     name() const override { return "ina219"; }
    sense::PollMode pollMode() const override { return sense::PollMode::Periodic; }
    u8              channels() const override { return 3; }
    const char*     unit(u8 channel) const override;

    Status probe() override;
    Status configure(const sense::SensorCfg& cfg) override;
    Status read(sense::Sample& out) override;

    /** Waga bitu prądu w mA — wynika z wybranego zakresu pomiarowego. */
    float currentLsbMa() const { return currentLsbMa_; }

private:
    Setup setup_;
    u8    addr_         = kDefaultAddress;
    u8    bus_          = 0;
    float currentLsbMa_ = 0.1f;
};

}  // namespace drivers
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
