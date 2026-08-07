/** Hydra — sterownik INA219. */

#include "hydra/drivers/sense/Ina219.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/hal/Hal.hpp"

namespace hydra {
namespace drivers {
namespace {

/** Zapis rejestru 16-bitowego, big-endian — tak jak wymaga INA219. */
Status writeReg16(hal::II2cBus::Session& s, u8 addr, u8 reg, u16 value) {
    const u8 payload[2] = {static_cast<u8>(value >> 8), static_cast<u8>(value & 0xFF)};
    return s.writeReg(addr, reg, CByteSpan{payload, sizeof(payload)});
}

Result<u16> readReg16(hal::II2cBus::Session& s, u8 addr, u8 reg) {
    u8 buf[2] = {};
    auto r = s.readReg(addr, reg, ByteSpan{buf, sizeof(buf)});
    if (!r) return unexpected(r.error());
    return static_cast<u16>(static_cast<u16>(buf[0]) << 8 | buf[1]);
}

}  // namespace

Ina219::Ina219(const Setup& setup) : setup_(setup) {}
Ina219::Ina219() : Ina219(Setup{}) {}

const char* Ina219::unit(u8 channel) const {
    switch (channel) {
        case 0: return "V";
        case 1: return "mA";
        case 2: return "mW";
        default: return "";
    }
}

Status Ina219::probe() {
    // INA219 nie ma rejestru identyfikacyjnego. Po resecie rejestr konfiguracji
    // ma znaną wartość — to najbliższe potwierdzeniu tożsamości, co da się
    // zrobić bez zmiany stanu układu.
    return hal::Hal::i2c(bus_).transaction([this](hal::II2cBus::Session& s) -> Status {
        HYDRA_TRY(const u16 cfg, readReg16(s, addr_, RegConfig));
        HYDRA_UNUSED(cfg);
        return ok();
    });
}

Status Ina219::configure(const sense::SensorCfg& cfg) {
    if (cfg.address != 0) addr_ = cfg.address;
    bus_ = cfg.busIndex;

    if (setup_.shuntOhms <= 0.0f || setup_.maxAmperes <= 0.0f) return fail(Err::BadArgument);

    // Waga bitu prądu: pełny zakres na 15 bitach (bit 16 to znak).
    currentLsbMa_ = (setup_.maxAmperes * 1000.0f) / 32768.0f;

    // Rejestr kalibracji wg dokumentacji układu (równanie 1):
    //   cal = trunc(0.04096 / (Current_LSB[A] * R_shunt[Ω]))
    const float currentLsbA = currentLsbMa_ / 1000.0f;
    const u32   calValue    = static_cast<u32>(0.04096f / (currentLsbA * setup_.shuntOhms));
    if (calValue == 0 || calValue > 0xFFFF) return fail(Err::OutOfRange);

    return hal::Hal::i2c(bus_).transaction([&](hal::II2cBus::Session& s) -> Status {
        HYDRA_CHECK(writeReg16(s, addr_, RegCalibration, static_cast<u16>(calValue)));
        // 32 V, bocznik ±320 mV, 12 bitów, tryb ciągły shunt+bus.
        return writeReg16(s, addr_, RegConfig, 0x399F);
    });
}

Status Ina219::read(sense::Sample& out) {
    u16 bus = 0, current = 0, power = 0;

    HYDRA_CHECK(hal::Hal::i2c(bus_).transaction([&](hal::II2cBus::Session& s) -> Status {
        HYDRA_TRY(bus, readReg16(s, addr_, RegBusVolt));
        HYDRA_TRY(current, readReg16(s, addr_, RegCurrent));
        HYDRA_TRY(power, readReg16(s, addr_, RegPower));
        return ok();
    }));

    // Rejestr napięcia szyny: bit 1 to flaga gotowości konwersji, bit 0 to
    // przepełnienie; sama wartość zaczyna się od bitu 3, waga 4 mV.
    const bool overflow = (bus & 0x0001) != 0;
    out.value[0] = static_cast<float>(bus >> 3) * 0.004f;

    // Prąd i moc są liczbami ze znakiem w uzupełnieniu do dwóch.
    out.value[1] = static_cast<float>(static_cast<i16>(current)) * currentLsbMa_;
    // Waga bitu mocy to dwudziestokrotność wagi bitu prądu (równanie 4).
    out.value[2] = static_cast<float>(power) * currentLsbMa_ * 20.0f;

    out.n = 3;
    out.q = overflow ? Quality::Suspect : Quality::Good;
    return ok();
}

}  // namespace drivers
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
