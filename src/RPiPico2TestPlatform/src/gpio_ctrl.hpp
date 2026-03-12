#pragma once
// GPIO Controller - SET / READ / PULSE operations
// Wraps GpioHAL and PwmHAL to enforce invariants:
//   - valid pin ID
//   - pin not busy with PWM before SET / PULSE
//   - pulse returns pin to original state
//   - cached state tracking for UNDEFINED initial condition

#include "hal.hpp"
#include <cstdint>

namespace gpio_ctrl {

// Error codes returned by controller methods (map to protocol error codes)
enum class GpioError : uint8_t {
    OK = 0,
    INVALID_PIN,   // pin >= HAL_NUM_PINS
    PIN_BUSY,      // active PWM on this pin
    INVALID_PARAM, // bad duration, frequency, duty-cycle, etc.
};

// Result of a PULSE operation
struct PulseResult {
    GpioError error;
    uint32_t  requested_us;
    uint64_t  start_time_us;   // absolute time at pulse start
    uint64_t  end_time_us;     // absolute time at pulse end
    PinState  state_before;    // pin state before pulse
    PinState  state_after;     // pin state after pulse (== state_before on success)
};

// ---------------------------------------------------------------------------
// GpioController
// ---------------------------------------------------------------------------
class GpioController {
public:
    // Both HAL pointers must outlive this object.
    GpioController(GpioHAL& gpio, PwmHAL& pwm);

    // --- Core GPIO ---

    // Drive pin to state. Returns INVALID_PIN or PIN_BUSY on failure.
    GpioError set_pin(uint8_t pin, PinState state);

    // Read pin. Returns UNDEFINED if pin has never been touched.
    // Fills 'err' with INVALID_PIN if pin is out of range.
    PinState  read_pin(uint8_t pin, GpioError& err);

    // Generate one pulse: flip pin, busy-wait duration_us, flip back.
    // On success result.error == OK and state_before == state_after.
    // On failure no GPIO change has occurred.
    PulseResult pulse(uint8_t pin, uint32_t duration_us);

    // --- PWM ---

    // freq_hz in (0, 100e6], duty_pct in [0, 100].
    GpioError set_pwm(uint8_t pin, float freq_hz, float duty_pct);
    GpioError stop_pwm(uint8_t pin);

    // --- Utility ---

    // Reset all pins to UNDEFINED; stop all PWM outputs.
    void reset();

    // True if 'pin' is a valid index (0 .. HAL_MAX_PIN).
    static bool is_valid_pin(uint8_t pin);

    // True if the pin has an active PWM signal.
    bool is_pin_busy(uint8_t pin) const;

    // Return last-known state (without reading hardware).
    PinState cached_state(uint8_t pin) const;

    // Busy-wait (delegates to GpioHAL::delay_us).
    // Used by SequenceController between steps.
    void delay_us(uint32_t us);

private:
    GpioHAL& gpio_;
    PwmHAL&  pwm_;

    // Tracks the last state we drove (or UNDEFINED if never touched).
    PinState cached_[HAL_NUM_PINS];
};

} // namespace gpio_ctrl
