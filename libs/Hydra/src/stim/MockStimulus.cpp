/** Hydra — bodziec na atrapach HAL (tylko build hostowy). */

#include "hydra/stim/MockStimulus.hpp"

#if HYDRA_PLAT_HOST

#include "hydra/hal/Mock.hpp"

namespace hydra {
namespace stim {

namespace {

/**
 * Atrapy mają jedną magistralę.
 *
 * Numer i tak jest w interfejsie, bo urządzenie z dwiema magistralami to rzecz
 * zwyczajna i scenariusz musi umieć powiedzieć, o którą chodzi. Tutaj wszystko
 * poza zerem jest odmową, a nie cichym przekierowaniem na jedyną istniejącą —
 * inaczej scenariusz pisany pod dwie magistrale „przechodziłby" na hoście,
 * sprawdzając za każdym razem tę samą.
 */
bool onlyBus(u8 bus) { return bus == 0; }

}  // namespace

bool MockStimulus::supports(Phenomenon phenomenon) const {
    switch (phenomenon) {
        case Phenomenon::DigitalInput:
        case Phenomenon::Edge:
        case Phenomenon::AnalogInput:
        case Phenomenon::DevicePresence:
        case Phenomenon::DeviceRegister:
        case Phenomenon::BusFault:
            return true;
    }
    return false;
}

Status MockStimulus::digitalInput(hal::PinNum pin, bool high) {
    hal::mock::backend().gpio.setInputLevel(pin, high);
    return ok();
}

Status MockStimulus::edge(hal::PinNum pin) {
    // Brak podpiętego handlera nie jest błędem: na stole impuls też pojawia się
    // niezależnie od tego, czy urządzenie akurat go słucha.
    hal::mock::backend().gpio.triggerInterrupt(pin);
    return ok();
}

Status MockStimulus::analogInput(hal::PinNum pin, u16 millivolts) {
    hal::mock::backend().adc.setPinMv(pin, millivolts);
    return ok();
}

Status MockStimulus::devicePresence(u8 bus, u8 address, bool present) {
    if (!onlyBus(bus)) return fail(Err::NotFound);

    if (present) return hal::mock::backend().i2c.addDevice(address);
    hal::mock::backend().i2c.removeDevice(address);
    return ok();
}

Status MockStimulus::deviceRegister(u8 bus, u8 address, u8 reg, u16 value, u8 width) {
    if (!onlyBus(bus)) return fail(Err::NotFound);
    if (width != 1 && width != 2) return fail(Err::BadArgument);

    hal::mock::MockI2c& i2c = hal::mock::backend().i2c;
    // Układ musi istnieć, zanim da się ustawić jego rejestr — inaczej scenariusz
    // opisujący nieobecny czujnik po cichu udawałby, że wszystko gra.
    if (!i2c.device(address)) HYDRA_CHECK(i2c.addDevice(address));

    if (width == 2) {
        i2c.setWordRegisters(address, true);
        i2c.setReg16(address, reg, value);
    } else {
        i2c.setReg(address, reg, static_cast<u8>(value));
    }
    return ok();
}

Status MockStimulus::busFault(u8 bus, u32 count, Err error) {
    if (!onlyBus(bus)) return fail(Err::NotFound);
    hal::mock::backend().i2c.failNext(count, error);
    return ok();
}

void MockStimulus::reset() {
    hal::mock::backend().clear();
}

}  // namespace stim
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST
