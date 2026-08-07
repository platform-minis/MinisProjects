/** Hydra — sterownik AS5600. */

#include "hydra/drivers/sense/As5600.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/hal/Hal.hpp"

namespace hydra {
namespace drivers {

Status As5600::probe() {
    return hal::Hal::i2c(bus_).transaction(
        [this](hal::II2cBus::Session& s) { return s.ping(addr_); });
}

Status As5600::configure(const sense::SensorCfg& cfg) {
    if (cfg.address != 0) addr_ = cfg.address;
    bus_ = cfg.busIndex;

    // AS5600 nie wymaga konfiguracji do pracy w trybie odczytu kąta —
    // sprawdzamy tylko, czy magnes jest wykryty i o odpowiedniej sile.
    return hal::Hal::i2c(bus_).transaction([this](hal::II2cBus::Session& s) -> Status {
        HYDRA_TRY(const u8 status, s.readReg8(addr_, RegStatus));
        magnetOk_ = (status & kStatusMagnetDetected) != 0 &&
                    (status & (kStatusMagnetTooWeak | kStatusMagnetTooStrong)) == 0;
        return ok();
    });
}

Status As5600::read(sense::Sample& out) {
    u8 raw[2] = {};

    HYDRA_CHECK(hal::Hal::i2c(bus_).transaction([this, &raw](hal::II2cBus::Session& s) {
        return s.readReg(addr_, RegAngle, ByteSpan{raw, sizeof(raw)});
    }));

    // 12 bitów w kolejności big-endian; górne 4 bity pierwszego bajtu są zerowe.
    const u16 counts = static_cast<u16>((static_cast<u16>(raw[0]) << 8 | raw[1]) & 0x0FFF);

    out.value[0] = static_cast<float>(counts) * 360.0f / 4096.0f;
    out.n        = 1;
    // Brak magnesu to nie błąd transferu — dane przychodzą, ale są bez wartości.
    out.q        = magnetOk_ ? Quality::Good : Quality::Suspect;
    return ok();
}

}  // namespace drivers
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
