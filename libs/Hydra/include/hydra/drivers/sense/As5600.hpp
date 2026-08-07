#pragma once
/**
 * Hydra — AS5600: magnetyczny enkoder absolutny (I2C, 0x36).
 *
 * Wzorcowy przykład adaptera z rozdz. 8: cała logika harmonogramu, kalibracji,
 * filtracji i publikacji leży w SensorHub, więc sterownik sprowadza się do
 * rozmowy z układem. Stąd kilkanaście linii kodu na czujnik.
 *
 * Kanały: [0] kąt w stopniach (0–360).
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/sense/ISensor.hpp"

namespace hydra {
namespace drivers {

class As5600 : public sense::ISensor {
public:
    static constexpr u8 kDefaultAddress = 0x36;

    /** Rejestry AS5600 (dokumentacja producenta, tabela 5). */
    enum Reg : u8 {
        RegStatus   = 0x0B,
        RegRawAngle = 0x0C,  ///< 12 bitów, big-endian
        RegAngle    = 0x0E,  ///< kąt po filtrach układu
    };

    /** Bity rejestru statusu: obecność i siła magnesu. */
    static constexpr u8 kStatusMagnetDetected = 0x20;
    static constexpr u8 kStatusMagnetTooWeak  = 0x10;
    static constexpr u8 kStatusMagnetTooStrong = 0x08;

    const char*     name() const override { return "as5600"; }
    sense::PollMode pollMode() const override { return sense::PollMode::Periodic; }
    u8              channels() const override { return 1; }
    const char*     unit(u8) const override { return "deg"; }

    Status probe() override;
    Status configure(const sense::SensorCfg& cfg) override;
    Status read(sense::Sample& out) override;

    /** Czy magnes jest w polu widzenia i o właściwej sile. */
    bool magnetOk() const { return magnetOk_; }

private:
    u8   addr_     = kDefaultAddress;
    u8   bus_      = 0;
    bool magnetOk_ = false;
};

}  // namespace drivers
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
