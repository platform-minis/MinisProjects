#include "pwm_ctrl.hpp"

namespace pwm_ctrl {

PwmController::PwmController(PwmHAL& pwm) : pwm_(pwm) {}

PwmError PwmController::start(uint8_t pin, float freq_hz, float duty_pct) {
    if (pin > HAL_MAX_PIN)                         return PwmError::INVALID_PIN;
    if (freq_hz < MIN_FREQ_HZ || freq_hz > MAX_FREQ_HZ)  return PwmError::INVALID_PARAM;
    if (duty_pct < MIN_DUTY_PCT || duty_pct > MAX_DUTY_PCT) return PwmError::INVALID_PARAM;

    pwm_.start_pwm(pin, freq_hz, duty_pct / 100.0f);
    return PwmError::OK;
}

PwmError PwmController::stop(uint8_t pin) {
    if (pin > HAL_MAX_PIN) return PwmError::INVALID_PIN;
    pwm_.stop_pwm(pin);
    return PwmError::OK;
}

bool PwmController::is_active(uint8_t pin) const {
    if (pin > HAL_MAX_PIN) return false;
    return pwm_.is_pwm_active(pin);
}

} // namespace pwm_ctrl
