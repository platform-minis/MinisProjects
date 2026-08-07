#pragma once
/**
 * Hydra — BME280: temperatura, ciśnienie i wilgotność (I2C, 0x76/0x77).
 *
 * Najbardziej rozbudowany z referencyjnych adapterów, bo układ oddaje surowe
 * zliczenia przetwornika i wymaga kompensacji współczynnikami zapisanymi
 * fabrycznie w jego pamięci. Formuły kompensacyjne pochodzą z dokumentacji
 * producenta (Bosch BME280, rozdz. 4.2.3, warianty całkowitoliczbowe) —
 * celowo w arytmetyce stałoprzecinkowej, żeby działały także na RP2040
 * bez FPU (rozdz. 15).
 *
 * Kanały: [0] temperatura [°C], [1] ciśnienie [hPa], [2] wilgotność [%RH].
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/sense/ISensor.hpp"

namespace hydra {
namespace drivers {

class Bme280 : public sense::ISensor {
public:
    static constexpr u8 kDefaultAddress = 0x76;
    static constexpr u8 kChipId         = 0x60;

    enum Reg : u8 {
        RegChipId    = 0xD0,
        RegReset     = 0xE0,
        RegCalib00   = 0x88,  ///< 26 bajtów: T1–T3, P1–P9, H1
        RegCalib26   = 0xE1,  ///< 7 bajtów: H2–H6
        RegCtrlHum   = 0xF2,
        RegStatus    = 0xF3,
        RegCtrlMeas  = 0xF4,
        RegConfig    = 0xF5,
        RegData      = 0xF7,  ///< 8 bajtów: ciśnienie, temperatura, wilgotność
    };

    const char*     name() const override { return "bme280"; }
    sense::PollMode pollMode() const override { return sense::PollMode::Periodic; }
    u8              channels() const override { return 3; }
    const char*     unit(u8 channel) const override;

    Status probe() override;
    Status configure(const sense::SensorCfg& cfg) override;
    Status read(sense::Sample& out) override;

private:
    /** Współczynniki kompensacji odczytane z pamięci układu. */
    struct Calib {
        u16 t1 = 0; i16 t2 = 0, t3 = 0;
        u16 p1 = 0; i16 p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0;
        u8  h1 = 0; i16 h2 = 0; u8 h3 = 0; i16 h4 = 0, h5 = 0; i8 h6 = 0;
    };

    Status readCalibration();

    i32   compensateTemp(i32 adc);              ///< 0,01 °C; ustawia tFine_
    u32   compensatePressure(i32 adc) const;    ///< Pa
    u32   compensateHumidity(i32 adc) const;    ///< Q22.10 %RH

    Calib calib_{};
    i32   tFine_ = 0;
    u8    addr_  = kDefaultAddress;
    u8    bus_   = 0;
};

}  // namespace drivers
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
