#pragma once
/**
 * Hydra — atrapy napędu dla buildu hostowego.
 *
 * Silnik atrapowy zapamiętuje zadaną moc i tryb, a enkoder pozwala podstawić
 * dowolny przebieg zliczeń. Razem dają model pojazdu wystarczający, by
 * przetestować całą pętlę sterowania z regulatorami i odometrią — bez
 * napędu, bez zasilania i bez czekania na realny czas.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_PLAT_HOST && HYDRA_ENABLE_MOTION

#include "hydra/motion/IEncoder.hpp"
#include "hydra/motion/IMotor.hpp"

namespace hydra {
namespace motion {
namespace mock {

class MockMotor : public IMotor {
public:
    enum class Mode : u8 { Coast = 0, Drive, Brake };

    Status begin() override {
        ++begins;
        return ok();
    }

    Status setPower(i16 permille) override {
        if (permille > kMaxPower) permille = kMaxPower;
        if (permille < -kMaxPower) permille = -kMaxPower;
        power_ = permille;
        mode   = Mode::Drive;
        ++writes;
        return ok();
    }

    Status brake() override {
        power_ = 0;
        mode   = Mode::Brake;
        ++brakes;
        return ok();
    }

    Status coast() override {
        power_ = 0;
        mode   = Mode::Coast;
        return ok();
    }

    i16 power() const override { return power_; }

    void clear() {
        power_ = 0;
        mode   = Mode::Coast;
        begins = writes = brakes = 0;
    }

    Mode mode   = Mode::Coast;
    u32  begins = 0;
    u32  writes = 0;
    u32  brakes = 0;

private:
    i16 power_ = 0;
};

class MockEncoder : public IEncoder {
public:
    Status begin() override { return ok(); }

    Result<i32> counts() override {
        if (faulty) return unexpected(Err::IoError);
        return value;
    }

    Status reset() override {
        value = 0;
        return ok();
    }

    /** Dodaje zliczenia, jakby koło się obróciło. */
    void advance(i32 delta) { value += delta; }
    void clear() {
        value  = 0;
        faulty = false;
    }

    i32  value  = 0;
    bool faulty = false;
};

}  // namespace mock
}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST && HYDRA_ENABLE_MOTION
