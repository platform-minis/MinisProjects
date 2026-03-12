#pragma once
// PWM Controller - PWM <pin> <freq_hz> <duty_pct> / PWM_STOP <pin>
// Thin validation wrapper around PwmHAL.

#include "hal.hpp"
#include <cstdint>

namespace pwm_ctrl {

static constexpr float MIN_FREQ_HZ  = 1.0f;
static constexpr float MAX_FREQ_HZ  = 100'000'000.0f;   // 100 MHz
static constexpr float MIN_DUTY_PCT = 0.0f;
static constexpr float MAX_DUTY_PCT = 100.0f;

enum class PwmError : uint8_t {
    OK = 0,
    INVALID_PIN,
    INVALID_PARAM,  // freq or duty out of range
};

class PwmController {
public:
    // 'pwm' must outlive this object.
    explicit PwmController(PwmHAL& pwm);

    // Start PWM. freq_hz in [1, 100e6]. duty_pct in [0, 100].
    PwmError start(uint8_t pin, float freq_hz, float duty_pct);

    // Stop PWM; pin is driven LOW afterwards.
    PwmError stop(uint8_t pin);

    // True if pin has an active PWM signal.
    bool is_active(uint8_t pin) const;

private:
    PwmHAL& pwm_;
};

} // namespace pwm_ctrl
