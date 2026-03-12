#pragma once
// MockHAL - deterministic, no-sleep implementations of all HAL interfaces.
// Used exclusively in host-side unit tests.
//
// Key properties:
//   - delay_us() advances mock time without sleeping (tests run in microseconds)
//   - MockCaptureHAL has an inject queue: pre-load events before calling capture()
//   - All state is inspectable from the test for verification

#include "hal.hpp"
#include <cstring>
#include <queue>

// ---------------------------------------------------------------------------
// MockGpioHAL + MockPwmHAL (combined for convenience)
// ---------------------------------------------------------------------------
class MockHAL : public GpioHAL, public PwmHAL {
public:
    static constexpr uint8_t N = HAL_NUM_PINS;

    // Visible state for test assertions
    bool      pin_value[N]     = {};   // electrical state (true = HIGH)
    PinDirection pin_dir[N]    = {};   // last configured direction
    bool      pwm_active[N]    = {};   // is PWM running on pin?
    float     pwm_freq[N]      = {};   // last configured PWM frequency
    float     pwm_duty[N]      = {};   // last configured PWM duty (0..1)
    uint64_t  mock_time_us     = 0;    // virtual clock

    // Call counters for interaction testing
    unsigned  init_calls[N]    = {};
    unsigned  set_calls[N]     = {};
    unsigned  read_calls[N]    = {};

    MockHAL() {
        memset(pin_value,  0, sizeof(pin_value));
        memset(pin_dir,    0, sizeof(pin_dir));
        memset(pwm_active, 0, sizeof(pwm_active));
        memset(pwm_freq,   0, sizeof(pwm_freq));
        memset(pwm_duty,   0, sizeof(pwm_duty));
        memset(init_calls, 0, sizeof(init_calls));
        memset(set_calls,  0, sizeof(set_calls));
        memset(read_calls, 0, sizeof(read_calls));
    }

    // --- GpioHAL ---
    void init_pin(uint8_t pin, PinDirection dir) override {
        pin_dir[pin] = dir;
        ++init_calls[pin];
    }
    void set_pin(uint8_t pin, bool value) override {
        pin_value[pin] = value;
        ++set_calls[pin];
    }
    bool read_pin(uint8_t pin) override {
        ++read_calls[pin];
        return pin_value[pin];
    }
    void delay_us(uint32_t us) override {
        mock_time_us += us;   // advance virtual clock, no real sleep
    }
    uint64_t time_us() override {
        return mock_time_us;
    }

    // --- PwmHAL ---
    void start_pwm(uint8_t pin, float freq_hz, float duty) override {
        pwm_active[pin] = true;
        pwm_freq[pin]   = freq_hz;
        pwm_duty[pin]   = duty;
    }
    void stop_pwm(uint8_t pin) override {
        pwm_active[pin] = false;
        pin_value[pin]  = false;  // driven LOW on stop
    }
    bool is_pwm_active(uint8_t pin) override {
        return (pin < N) && pwm_active[pin];
    }

    // --- Test helpers ---
    // Force a pin's electrical state (simulates DUT driving the pin).
    void force_pin(uint8_t pin, bool value) {
        pin_value[pin] = value;
    }

    void reset_all() {
        *this = MockHAL();
    }
};

// ---------------------------------------------------------------------------
// MockCaptureHAL
// Allows tests to pre-inject SignalEvents that will be returned by wait_event().
// ---------------------------------------------------------------------------
class MockCaptureHAL : public CaptureHAL {
public:
    uint8_t  configured_pin  = 0;
    EdgeType configured_edge = EdgeType::BOTH;
    bool     started         = false;
    bool     stopped         = false;
    unsigned wait_calls      = 0;

    // Inject events BEFORE calling capture().
    // timestamp_us is relative to start() call.
    void inject(uint8_t pin, PinState state, uint64_t timestamp_us = 0) {
        SignalEvent ev{timestamp_us, pin, state};
        queue_.push(ev);
    }

    // Inject a burst of alternating HIGH/LOW transitions with uniform spacing
    void inject_square_wave(uint8_t pin, unsigned count, uint64_t period_us) {
        PinState state = PinState::HIGH;
        for (unsigned i = 0; i < count; ++i) {
            inject(pin, state, i * (period_us / 2));
            state = invert_state(state);
        }
    }

    // --- CaptureHAL ---
    void configure(uint8_t pin, EdgeType edge) override {
        configured_pin  = pin;
        configured_edge = edge;
    }
    void start() override {
        started = true;
        stopped = false;
    }
    void stop() override {
        stopped = true;
    }
    bool wait_event(SignalEvent& event, uint64_t /*timeout_us*/) override {
        ++wait_calls;
        if (queue_.empty()) return false;
        event = queue_.front();
        queue_.pop();
        return true;
    }

    bool has_pending() const { return !queue_.empty(); }
    void clear()             { while (!queue_.empty()) queue_.pop(); }

private:
    std::queue<SignalEvent> queue_;
};
