// RPiPico2DisplayDVI — 1bpp monochrome DVI via PicoDVI
// Raspberry Pi Pico 2 (RP2350), 640×480 @ 60 Hz, black/white.
//
// Uses PicoDVI library (Wren6991) — PIO-based TMDS serialisation,
// hardware SIO TMDS encoder on RP2350.
//
// DVI Breakout wiring:
//   GP12/13 — D0 Blue  (differential pair)
//   GP14/15 — D1 Green (differential pair)
//   GP16/17 — D2 Red   (differential pair)
//   GP18/19 — CLK      (differential pair)
//   GND     — GND

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"

#define FRAME_WIDTH  640u
#define FRAME_HEIGHT 480u
#define FB_WORDS     (FRAME_WIDTH / 32u)   // 20 uint32_t per scanline

// ---------------------------------------------------------------------------
// Custom pin config — matches our DVI breakout (GP12-GP19)
// PicoDVI outputs base pin = positive, base+1 = negative of each pair.
// ---------------------------------------------------------------------------
static const struct dvi_serialiser_cfg my_dvi_cfg = {
    .pio              = pio0,
    .sm_tmds          = {0, 1, 2},
    .pins_tmds        = {12, 14, 16},  // base pins: Blue=GP12/13, Green=GP14/15, Red=GP16/17
    .pins_clk         = 18,            // CLK: GP18/19
    .invert_diffpairs = true,          // GP12=D0-, GP13=D0+ → invert so base=negative
};

// ---------------------------------------------------------------------------
// Framebuffer: 640×480, 1 bpp, LSB = leftmost pixel
//   0 bit = black, 1 bit = white
// ---------------------------------------------------------------------------
static uint32_t framebuf[FRAME_HEIGHT][FB_WORDS];

static inline void fb_set_pixel(uint32_t x, uint32_t y, bool on) {
    if (x >= FRAME_WIDTH || y >= FRAME_HEIGHT) return;
    if (on) framebuf[y][x / 32u] |=  (1u << (x % 32u));
    else    framebuf[y][x / 32u] &= ~(1u << (x % 32u));
}

// 8×8 checkerboard test pattern
static void draw_checkerboard(void) {
    for (uint32_t y = 0; y < FRAME_HEIGHT; y++)
        for (uint32_t x = 0; x < FRAME_WIDTH; x++)
            fb_set_pixel(x, y, (((x >> 3u) ^ (y >> 3u)) & 1u) != 0u);
}

// ---------------------------------------------------------------------------
// DVI instance
// ---------------------------------------------------------------------------
static struct dvi_inst dvi0;

// ---------------------------------------------------------------------------
// Core 1: TMDS encode + DVI output
// ---------------------------------------------------------------------------
static void core1_main(void) {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);

    // Wait until core 0 has pushed the first scanline
    while (queue_is_empty(&dvi0.q_colour_valid))
        __wfe();

    dvi_start(&dvi0);

    // With DVI_MONOCHROME_TMDS=1 the library uses the same TMDS buffer for
    // all three lanes — encode once per scanline.
    while (true) {
        uint32_t *pixbuf;
        queue_remove_blocking_u32(&dvi0.q_colour_valid, &pixbuf);

        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);

        tmds_encode_1bpp(pixbuf, tmdsbuf, FRAME_WIDTH);

        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
        queue_add_blocking_u32(&dvi0.q_colour_free, &pixbuf);
    }
}

// ---------------------------------------------------------------------------
// Main (Core 0)
// ---------------------------------------------------------------------------
int main(void) {
    // Raise core voltage for 252 MHz operation
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);

    // 252 MHz system clock (= TMDS bit clock for 640×480 @ 60 Hz)
    set_sys_clock_khz(dvi_timing_640x480p_60hz.bit_clk_khz, true);

    stdio_init_all();

    draw_checkerboard();

    dvi0.timing  = &dvi_timing_640x480p_60hz;
    dvi0.ser_cfg = my_dvi_cfg;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    multicore_launch_core1(core1_main);

    // Feed framebuffer scanlines to core 1 in a continuous loop
    uint32_t y = 0;
    while (true) {
        uint32_t *ptr = framebuf[y];
        queue_add_blocking_u32(&dvi0.q_colour_valid, &ptr);

        // Drain freed colour buffers (not needed for static framebuf, but keeps queues healthy)
        uint32_t *freed;
        while (queue_try_remove_u32(&dvi0.q_colour_free, &freed)) {}

        if (++y == FRAME_HEIGHT) y = 0u;
    }
}
