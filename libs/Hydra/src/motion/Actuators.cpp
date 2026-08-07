/** Hydra — implementacja aktuatorów (rozdz. 9). */

#include "hydra/motion/IMotor.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/hal/Hal.hpp"

namespace hydra {
namespace motion {
namespace {

i16 clampPower(i16 v) {
    if (v > kMaxPower) return kMaxPower;
    if (v < -kMaxPower) return -kMaxPower;
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// HBridgeMotor
// ---------------------------------------------------------------------------

Status HBridgeMotor::configure(const Config& cfg) {
    if (cfg.freqHz == 0) return fail(Err::BadArgument);
    if (cfg.deadband < 0 || cfg.deadband >= kMaxPower) return fail(Err::BadArgument);

    if (cfg.wiring == Wiring::DualPwm) {
        if (cfg.in1 == hal::kNoPin || cfg.in2 == hal::kNoPin) return fail(Err::BadArgument);
    } else {
        if (cfg.pwm == hal::kNoPin || cfg.dir == hal::kNoPin) return fail(Err::BadArgument);
    }

    cfg_ = cfg;
    return ok();
}

Status HBridgeMotor::begin() {
    if (cfg_.wiring == Wiring::DualPwm) {
        HYDRA_CHECK(hal::Hal::pwm().configure(cfg_.in1, cfg_.freqHz));
        HYDRA_CHECK(hal::Hal::pwm().configure(cfg_.in2, cfg_.freqHz));
    } else {
        HYDRA_CHECK(hal::Hal::pwm().configure(cfg_.pwm, cfg_.freqHz));
        HYDRA_CHECK(hal::Hal::gpio().configure(cfg_.dir, hal::PinMode::Output));
    }

    ready_ = true;
    // Silnik startuje zatrzymany. Pozostawienie wyjść w stanie nieokreślonym
    // oznaczałoby ruch w chwili podania zasilania.
    return coast();
}

i16 HBridgeMotor::compensate(i16 permille) const {
    if (cfg_.deadband == 0 || permille == 0) return permille;

    const i16 magnitude = permille > 0 ? permille : static_cast<i16>(-permille);
    // Odwzorowanie [1, 1000] na [deadband, 1000]: najmniejsze niezerowe
    // zadanie od razu przekracza próg ruszenia.
    const i32 scaled = cfg_.deadband +
                       static_cast<i32>(magnitude) * (kMaxPower - cfg_.deadband) / kMaxPower;
    const i16 result = static_cast<i16>(scaled);
    return permille > 0 ? result : static_cast<i16>(-result);
}

Status HBridgeMotor::writeOutputs(i16 permille) {
    const i16 magnitude = permille > 0 ? permille : static_cast<i16>(-permille);
    const bool forward  = cfg_.invert ? (permille < 0) : (permille > 0);

    if (cfg_.wiring == Wiring::DualPwm) {
        // Kierunek wynika z tego, które wejście dostaje wypełnienie;
        // drugie musi być wyzerowane, inaczej mostek hamuje zamiast jechać.
        if (permille == 0) {
            HYDRA_CHECK(hal::Hal::pwm().setDutyPermille(cfg_.in1, 0));
            return hal::Hal::pwm().setDutyPermille(cfg_.in2, 0);
        }
        HYDRA_CHECK(hal::Hal::pwm().setDutyPermille(forward ? cfg_.in1 : cfg_.in2,
                                                    static_cast<u16>(magnitude)));
        return hal::Hal::pwm().setDutyPermille(forward ? cfg_.in2 : cfg_.in1, 0);
    }

    HYDRA_CHECK(hal::Hal::gpio().write(cfg_.dir, forward));
    return hal::Hal::pwm().setDutyPermille(cfg_.pwm, static_cast<u16>(magnitude));
}

Status HBridgeMotor::setPower(i16 permille) {
    if (!ready_) return fail(Err::NotInitialized);

    power_   = clampPower(permille);
    applied_ = compensate(power_);
    return writeOutputs(applied_);
}

Status HBridgeMotor::brake() {
    if (!ready_) return fail(Err::NotInitialized);
    power_   = 0;
    applied_ = 0;

    if (cfg_.wiring == Wiring::DualPwm) {
        // Oba wejścia w stanie wysokim zwierają uzwojenie — hamowanie dynamiczne.
        HYDRA_CHECK(hal::Hal::pwm().setDutyPermille(cfg_.in1, kMaxPower));
        return hal::Hal::pwm().setDutyPermille(cfg_.in2, kMaxPower);
    }
    return hal::Hal::pwm().setDutyPermille(cfg_.pwm, 0);
}

Status HBridgeMotor::coast() {
    if (!ready_) return fail(Err::NotInitialized);
    power_   = 0;
    applied_ = 0;
    return writeOutputs(0);
}

// ---------------------------------------------------------------------------
// PwmServo
// ---------------------------------------------------------------------------

Status PwmServo::configure(const Config& cfg) {
    if (cfg.pin == hal::kNoPin) return fail(Err::BadArgument);
    if (cfg.maxUs <= cfg.minUs) return fail(Err::BadArgument);
    if (cfg.maxAngle <= cfg.minAngle) return fail(Err::BadArgument);
    if (cfg.freqHz == 0) return fail(Err::BadArgument);

    cfg_  = cfg;
    angle_ = cfg.minAngle;
    return ok();
}

Status PwmServo::begin() {
    // Rozdzielczość 12 bitów przy 50 Hz daje krok około 5 µs — poniżej
    // rozdzielczości typowego serwa analogowego.
    HYDRA_CHECK(hal::Hal::pwm().configure(cfg_.pin, cfg_.freqHz, 12));
    ready_ = true;
    return setAngle(angle_);
}

Status PwmServo::setAngle(i16 degrees) {
    if (!ready_) return fail(Err::NotInitialized);

    if (degrees < cfg_.minAngle) degrees = cfg_.minAngle;
    if (degrees > cfg_.maxAngle) degrees = cfg_.maxAngle;

    const i32 span   = cfg_.maxAngle - cfg_.minAngle;
    const i32 offset = degrees - cfg_.minAngle;
    pulseUs_ = static_cast<u16>(cfg_.minUs +
                                (static_cast<i32>(cfg_.maxUs - cfg_.minUs) * offset) / span);
    angle_ = degrees;
    return hal::Hal::pwm().writeMicroseconds(cfg_.pin, pulseUs_);
}

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
