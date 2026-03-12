// Pico 2 (RP2350) HAL implementation.
// This file is compiled ONLY for the Pico target (not for host-based tests).

#include "pico_hal.hpp"

// pico-sdk headers
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/timer.h"

// Generated PIO header (from capture.pio)
#include "capture.pio.h"

// ---------------------------------------------------------------------------
// PicoGpioHAL
// ---------------------------------------------------------------------------
void PicoGpioHAL::init_pin(uint8_t pin, PinDirection dir) {
    gpio_init(pin);
    gpio_set_dir(pin, dir == PinDirection::OUTPUT ? GPIO_OUT : GPIO_IN);
    if (dir == PinDirection::INPUT) {
        // Enable weak pull-down to avoid floating reads
        gpio_pull_down(pin);
    }
}

void PicoGpioHAL::set_pin(uint8_t pin, bool value) {
    gpio_put(pin, value);
}

bool PicoGpioHAL::read_pin(uint8_t pin) {
    return gpio_get(pin);
}

void PicoGpioHAL::delay_us(uint32_t us) {
    // busy_wait_us_32 uses the hardware TIMER peripheral (not a CPU loop).
    // Accuracy: ±1 µs at 150 MHz system clock.
    busy_wait_us_32(us);
}

uint64_t PicoGpioHAL::time_us() {
    return time_us_64();
}

// ---------------------------------------------------------------------------
// PicoPwmHAL
// ---------------------------------------------------------------------------
PicoPwmHAL::PicoPwmHAL() {
    for (uint8_t i = 0; i < HAL_NUM_PINS; ++i) active_[i] = false;
}

void PicoPwmHAL::start_pwm(uint8_t pin, float freq_hz, float duty) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);

    // Calculate divider and wrap for desired frequency.
    // PWM clock source = system clock (150 MHz on RP2350 default).
    uint32_t sys_clk = clock_get_hz(clk_sys);
    float clk_div   = (float)sys_clk / (freq_hz * 65536.0f);
    if (clk_div < 1.0f) clk_div = 1.0f;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clk_div);
    pwm_config_set_wrap(&cfg, 65535);
    pwm_init(slice, &cfg, true);

    uint16_t level = (uint16_t)(duty * 65535.0f);
    pwm_set_gpio_level(pin, level);

    active_[pin] = true;
}

void PicoPwmHAL::stop_pwm(uint8_t pin) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice, false);
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
    active_[pin] = false;
}

bool PicoPwmHAL::is_pwm_active(uint8_t pin) {
    return (pin < HAL_NUM_PINS) && active_[pin];
}

// ---------------------------------------------------------------------------
// PicoCaptureHAL
// ---------------------------------------------------------------------------
PicoCaptureHAL::PicoCaptureHAL()
    : pin_(0), edge_(EdgeType::BOTH), start_time_us_(0),
      pio_(pio0), sm_(0), offset_(0), armed_(false) {}

void PicoCaptureHAL::configure(uint8_t pin, EdgeType edge) {
    pin_  = pin;
    edge_ = edge;
}

void PicoCaptureHAL::start() {
    PIO pio = (PIO)pio_;
    // Load program if needed
    if (!pio_can_add_program(pio, &capture_program)) {
        // Program already loaded - reuse offset
    } else {
        offset_ = pio_add_program(pio, &capture_program);
    }
    capture_program_init(pio, sm_, offset_, pin_);
    // Flush RX FIFO
    while (!pio_sm_is_rx_fifo_empty(pio, sm_)) {
        pio_sm_get(pio, sm_);
    }
    start_time_us_ = time_us_64();
    armed_ = true;
}

void PicoCaptureHAL::stop() {
    PIO pio = (PIO)pio_;
    pio_sm_set_enabled(pio, sm_, false);
    armed_ = false;
}

bool PicoCaptureHAL::wait_event(SignalEvent& event, uint64_t timeout_us) {
    PIO      pio      = (PIO)pio_;
    uint64_t deadline = time_us_64() + timeout_us;

    while (time_us_64() < deadline) {
        if (!pio_sm_is_rx_fifo_empty(pio, sm_)) {
            uint32_t word = pio_sm_get(pio, sm_);
            // Lower 8 bits = pin states for GP0-GP7 (or the monitored pin range)
            bool val = (word >> (pin_ & 7)) & 1u;

            event.timestamp_us = time_us_64() - start_time_us_;
            event.pin_id       = pin_;
            event.state        = val ? PinState::HIGH : PinState::LOW;
            return true;
        }
        // Short sleep to avoid burning the CPU while polling
        busy_wait_us_32(1);
    }
    return false;
}
