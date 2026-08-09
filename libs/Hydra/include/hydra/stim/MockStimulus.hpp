#pragma once
/**
 * Hydra — bodziec na atrapach HAL, czyli świat udawany na hoście.
 *
 * Tłumaczy zjawiska z {@link hydra::stim::IStimulus} na wywołania atrap:
 * poziom wejścia to `MockGpio::setInputLevel`, awaria magistrali to
 * `MockI2c::failNext`. Dzięki temu test przestaje grzebać w atrapach i zaczyna
 * opisywać sytuacje — a ten sam opis pojedzie kiedyś na stanowisko ze sprzętem.
 *
 * Istnieje wyłącznie w buildzie hostowym, tak jak same atrapy.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_PLAT_HOST

#include "hydra/stim/Stimulus.hpp"

namespace hydra {
namespace stim {

class MockStimulus : public IStimulus {
public:
    const char* name() const override { return "mock"; }

    /**
     * Atrapy potrafią wszystko, co da się udawać elektrycznie.
     *
     * Nie znaczy to, że host potrafi wszystko: zjawiska, których nie ma na
     * liście {@link Phenomenon} — temperatura, pobór prądu, zakłócenia — nie
     * dają się tu wywołać w ogóle i dlatego świadomie nie mają metod.
     */
    bool supports(Phenomenon phenomenon) const override;

    Status digitalInput(hal::PinNum pin, bool high) override;
    Status edge(hal::PinNum pin) override;
    Status analogInput(hal::PinNum pin, u16 millivolts) override;
    Status devicePresence(u8 bus, u8 address, bool present) override;
    Status deviceRegister(u8 bus, u8 address, u8 reg, u16 value, u8 width = 1) override;
    Status busFault(u8 bus, u32 count, Err error = Err::IoError) override;

    void reset() override;
};

}  // namespace stim
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST
