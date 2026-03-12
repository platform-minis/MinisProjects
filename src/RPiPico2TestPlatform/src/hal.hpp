#pragma once
// Hardware Abstraction Layer for Stimulus Controller
// Allows unit tests to run on HOST (x86) using MockHAL
// Real hardware uses PicoHAL (pico-sdk implementation)

#include <cstdint>
#include <cstddef>

// RP2350 has 30 GPIO pins (GP0-GP29)
static constexpr uint8_t HAL_NUM_PINS = 30;
static constexpr uint8_t HAL_MAX_PIN  = 29;

// Minimum/maximum pulse duration in microseconds
static constexpr uint32_t HAL_PULSE_US_MIN = 1;
static constexpr uint32_t HAL_PULSE_US_MAX = 1'000'000;

// PIO clock frequency (RP2350 default system clock)
static constexpr uint32_t HAL_PIO_CLOCK_HZ = 150'000'000;

enum class PinState : uint8_t {
    LOW       = 0,
    HIGH      = 1,
    UNDEFINED = 2,
    FLOAT     = 3,
};

enum class PinDirection : uint8_t {
    OUTPUT = 0,
    INPUT  = 1,
};

enum class EdgeType : uint8_t {
    RISING  = 0,  // LOW -> HIGH
    FALLING = 1,  // HIGH -> LOW
    BOTH    = 2,  // any transition
};

// Returns the inverse of a logic state.
// UNDEFINED and FLOAT are returned unchanged.
inline PinState invert_state(PinState s) {
    if (s == PinState::HIGH) return PinState::LOW;
    if (s == PinState::LOW)  return PinState::HIGH;
    return s;
}

// Converts bool to logic PinState (true = HIGH, false = LOW)
inline PinState bool_to_state(bool v) {
    return v ? PinState::HIGH : PinState::LOW;
}

// Returns true if the state is a valid logic level (not UNDEFINED/FLOAT)
inline bool is_logic(PinState s) {
    return s == PinState::LOW || s == PinState::HIGH;
}

// ---------------------------------------------------------------------------
// GpioHAL - set/read individual GPIO pins with timing
// ---------------------------------------------------------------------------
class GpioHAL {
public:
    virtual ~GpioHAL() = default;

    // Configure pin direction (must be called before set_pin/read_pin)
    virtual void init_pin(uint8_t pin, PinDirection dir) = 0;

    // Drive an output pin
    virtual void set_pin(uint8_t pin, bool value) = 0;

    // Read a pin (works for both input and output)
    virtual bool read_pin(uint8_t pin) = 0;

    // Busy-wait for exactly 'us' microseconds (as close as hardware allows)
    virtual void delay_us(uint32_t us) = 0;

    // Monotonic time in microseconds since boot / simulator start
    virtual uint64_t time_us() = 0;
};

// ---------------------------------------------------------------------------
// PwmHAL - hardware PWM
// ---------------------------------------------------------------------------
class PwmHAL {
public:
    virtual ~PwmHAL() = default;

    // Start PWM on pin with given frequency (Hz) and duty cycle (0.0-1.0)
    virtual void start_pwm(uint8_t pin, float freq_hz, float duty) = 0;

    // Stop PWM; pin is driven LOW after stop
    virtual void stop_pwm(uint8_t pin) = 0;

    // Returns true if PWM is currently active on this pin
    virtual bool is_pwm_active(uint8_t pin) = 0;
};

// ---------------------------------------------------------------------------
// CaptureHAL - PIO-based edge detection
// ---------------------------------------------------------------------------
struct SignalEvent {
    uint64_t timestamp_us;  // time since capture start
    uint8_t  pin_id;
    PinState state;         // new state (after transition)
};

class CaptureHAL {
public:
    virtual ~CaptureHAL() = default;

    // Configure the capture channel for a pin and edge type
    virtual void configure(uint8_t pin, EdgeType edge) = 0;

    // Arm capture (must be called before wait_event)
    virtual void start() = 0;

    // Disarm capture
    virtual void stop() = 0;

    // Block until an edge event arrives or timeout_us elapses.
    // Returns true if an event was recorded (fills 'event').
    // Returns false on timeout.
    virtual bool wait_event(SignalEvent& event, uint64_t timeout_us) = 0;
};
