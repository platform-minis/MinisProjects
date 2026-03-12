#pragma once
// Pico 2 (RP2350) hardware implementation of the HAL interfaces.
// Only compiled when targeting the Pico SDK (PICO_BOARD is defined).

#include "hal.hpp"

// ---------------------------------------------------------------------------
// PicoGpioHAL  - wraps pico-sdk gpio_* and time_us_64()
// ---------------------------------------------------------------------------
class PicoGpioHAL final : public GpioHAL {
public:
    void init_pin(uint8_t pin, PinDirection dir) override;
    void set_pin(uint8_t pin, bool value) override;
    bool read_pin(uint8_t pin) override;
    void delay_us(uint32_t us) override;
    uint64_t time_us() override;
};

// ---------------------------------------------------------------------------
// PicoPwmHAL  - wraps pico-sdk hardware_pwm
// ---------------------------------------------------------------------------
class PicoPwmHAL final : public PwmHAL {
public:
    PicoPwmHAL();
    void start_pwm(uint8_t pin, float freq_hz, float duty) override;
    void stop_pwm(uint8_t pin) override;
    bool is_pwm_active(uint8_t pin) override;

private:
    bool active_[HAL_NUM_PINS];
};

// ---------------------------------------------------------------------------
// PicoCaptureHAL  - PIO-based edge capture (RP2350 PIO0)
// ---------------------------------------------------------------------------
class PicoCaptureHAL final : public CaptureHAL {
public:
    PicoCaptureHAL();
    void configure(uint8_t pin, EdgeType edge) override;
    void start() override;
    void stop() override;
    bool wait_event(SignalEvent& event, uint64_t timeout_us) override;

private:
    uint8_t  pin_;
    EdgeType edge_;
    uint64_t start_time_us_;

    // PIO state-machine handle (pico-sdk PIO type)
    // Declared as void* to avoid including pico-sdk headers here;
    // pico_hal.cpp casts appropriately.
    void*    pio_;
    uint     sm_;
    uint     offset_;
    bool     armed_;
};
