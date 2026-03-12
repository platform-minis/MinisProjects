#include "gpio_ctrl.hpp"
#include <cstring>

namespace gpio_ctrl {

GpioController::GpioController(GpioHAL& gpio, PwmHAL& pwm)
    : gpio_(gpio), pwm_(pwm)
{
    for (uint8_t i = 0; i < HAL_NUM_PINS; ++i) {
        cached_[i] = PinState::UNDEFINED;
    }
}

// ---------------------------------------------------------------------------
bool GpioController::is_valid_pin(uint8_t pin) {
    return pin <= HAL_MAX_PIN;
}

bool GpioController::is_pin_busy(uint8_t pin) const {
    if (!is_valid_pin(pin)) return false;
    return pwm_.is_pwm_active(pin);
}

PinState GpioController::cached_state(uint8_t pin) const {
    if (!is_valid_pin(pin)) return PinState::UNDEFINED;
    return cached_[pin];
}

// ---------------------------------------------------------------------------
GpioError GpioController::set_pin(uint8_t pin, PinState state) {
    if (!is_valid_pin(pin))   return GpioError::INVALID_PIN;
    if (is_pin_busy(pin))     return GpioError::PIN_BUSY;

    gpio_.init_pin(pin, PinDirection::OUTPUT);
    gpio_.set_pin(pin, state == PinState::HIGH);
    cached_[pin] = state;
    return GpioError::OK;
}

// ---------------------------------------------------------------------------
PinState GpioController::read_pin(uint8_t pin, GpioError& err) {
    if (!is_valid_pin(pin)) {
        err = GpioError::INVALID_PIN;
        return PinState::UNDEFINED;
    }
    err = GpioError::OK;
    gpio_.init_pin(pin, PinDirection::INPUT);
    bool val = gpio_.read_pin(pin);
    // Update cache with hardware reading
    cached_[pin] = bool_to_state(val);
    return cached_[pin];
}

// ---------------------------------------------------------------------------
PulseResult GpioController::pulse(uint8_t pin, uint32_t duration_us) {
    PulseResult r{};
    r.requested_us = duration_us;

    if (!is_valid_pin(pin)) {
        r.error = GpioError::INVALID_PIN;
        return r;
    }
    if (is_pin_busy(pin)) {
        r.error = GpioError::PIN_BUSY;
        return r;
    }
    if (duration_us < HAL_PULSE_US_MIN || duration_us > HAL_PULSE_US_MAX) {
        r.error = GpioError::INVALID_PARAM;
        return r;
    }

    // Determine start state; treat UNDEFINED as LOW
    PinState start = cached_[pin];
    if (!is_logic(start)) start = PinState::LOW;
    r.state_before = start;

    PinState pulse_state = invert_state(start);

    gpio_.init_pin(pin, PinDirection::OUTPUT);

    r.start_time_us = gpio_.time_us();
    gpio_.set_pin(pin, pulse_state == PinState::HIGH);
    gpio_.delay_us(duration_us);
    gpio_.set_pin(pin, start == PinState::HIGH);
    r.end_time_us = gpio_.time_us();

    cached_[pin]  = start;
    r.state_after = start;
    r.error       = GpioError::OK;
    return r;
}

// ---------------------------------------------------------------------------
GpioError GpioController::set_pwm(uint8_t pin, float freq_hz, float duty_pct) {
    if (!is_valid_pin(pin)) return GpioError::INVALID_PIN;
    if (freq_hz < 1.0f || freq_hz > 100'000'000.0f) return GpioError::INVALID_PARAM;
    if (duty_pct < 0.0f || duty_pct > 100.0f)        return GpioError::INVALID_PARAM;

    pwm_.start_pwm(pin, freq_hz, duty_pct / 100.0f);
    cached_[pin] = PinState::UNDEFINED;  // PWM owns the pin
    return GpioError::OK;
}

GpioError GpioController::stop_pwm(uint8_t pin) {
    if (!is_valid_pin(pin)) return GpioError::INVALID_PIN;
    pwm_.stop_pwm(pin);
    cached_[pin] = PinState::LOW;  // stop_pwm drives pin LOW
    return GpioError::OK;
}

// ---------------------------------------------------------------------------
void GpioController::delay_us(uint32_t us) {
    gpio_.delay_us(us);
}

void GpioController::reset() {
    for (uint8_t i = 0; i < HAL_NUM_PINS; ++i) {
        if (pwm_.is_pwm_active(i)) {
            pwm_.stop_pwm(i);
        }
        // Drive all pins LOW and reconfigure as outputs
        gpio_.init_pin(i, PinDirection::OUTPUT);
        gpio_.set_pin(i, false);
        cached_[i] = PinState::LOW;
    }
}

} // namespace gpio_ctrl
