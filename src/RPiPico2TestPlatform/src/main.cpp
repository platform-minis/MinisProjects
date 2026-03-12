// McuTestPlatform - Stimulus Controller Firmware
// Target: Raspberry Pi Pico 2 (RP2350)
// Protocol: ASCII text commands over USB-CDC (stdio_usb)
//
// Commands are read line-by-line from USB-CDC; responses are written back.
// See protocol.hpp for the full command reference.

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include "pico_hal.hpp"
#include "gpio_ctrl.hpp"
#include "capture.hpp"
#include "sequence.hpp"
#include "pwm_ctrl.hpp"
#include "command_handler.hpp"

#include <cstdio>
#include <cstring>

// Line buffer sizes
static constexpr size_t RX_BUF_SIZE = 300;
static constexpr size_t TX_BUF_SIZE = 1024;

// Read one '\n'-terminated line from stdio (blocking).
// Returns number of characters in buf (excluding null terminator).
// Strips trailing '\r'.
static size_t read_line(char* buf, size_t buf_size) {
    size_t n = 0;
    while (n < buf_size - 1) {
        int c = getchar();   // pico-sdk: blocks until character available
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[n++] = (char)c;
    }
    buf[n] = '\0';
    return n;
}

int main() {
    // Initialise USB-CDC stdio (blocks until host connects)
    stdio_usb_init();

    // Wait for USB enumeration
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }

    // --- Instantiate HAL implementations ---
    PicoGpioHAL   gpio_hal;
    PicoPwmHAL    pwm_hal;
    PicoCaptureHAL capture_hal;

    // --- Instantiate controllers ---
    gpio_ctrl::GpioController       gpio_ctrl(gpio_hal, pwm_hal);
    capture_ctrl::CaptureController capture_ctrl(capture_hal);
    sequence_ctrl::SequenceController seq_ctrl(gpio_ctrl);
    pwm_ctrl::PwmController         pwm_ctrl(pwm_hal);

    // --- Command dispatcher ---
    CommandHandler handler(gpio_ctrl, capture_ctrl, seq_ctrl, pwm_ctrl);

    // --- Banner ---
    char tx[TX_BUF_SIZE];
    snprintf(tx, sizeof(tx), "OK:McuTestPlatform/%s ready\n",
             protocol::FIRMWARE_VERSION);
    fputs(tx, stdout);
    fflush(stdout);

    // --- Main command loop ---
    char rx[RX_BUF_SIZE];
    while (true) {
        size_t n = read_line(rx, sizeof(rx));
        if (n == 0) continue;

        size_t written = handler.handle(rx, tx, sizeof(tx));
        if (written > 0) {
            fwrite(tx, 1, written, stdout);
            fflush(stdout);
        }
    }
}
