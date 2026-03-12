// RPiPico2DisplayDVI — Stimulus Controller
// Raspberry Pi Pico 2 (RP2350), 640x480 monochrome DVI output via HSTX.
//
// Hardware:
//   GP12/13 — D0 Blue  (differential -)/(+)
//   GP14/15 — D1 Green (differential -)/(+)
//   GP16/17 — D2 Red   (differential -)/(+)
//   GP18/19 — CLK      (differential -)/(+)
//
// Based on pico-examples/hstx/dvi (pico-sdk 2.x, RP2350).
// HSTX EXPAND_TMDS converts 1bpp pixel data to TMDS symbols in hardware.
// Blanking lines feed pre-computed TMDS control-symbol words directly.

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include <string.h>

// ---------------------------------------------------------------------------
// DVI 640x480 @ 60Hz timing (pixel clock 25.175 MHz)
// ---------------------------------------------------------------------------
#define H_ACTIVE        640u
#define H_FRONT_PORCH   16u
#define H_SYNC          96u
#define H_BACK_PORCH    48u
#define H_TOTAL         (H_ACTIVE + H_FRONT_PORCH + H_SYNC + H_BACK_PORCH)  // 800

#define V_ACTIVE        480u
#define V_FRONT_PORCH   10u
#define V_SYNC          2u
#define V_BACK_PORCH    33u
#define V_TOTAL         (V_ACTIVE + V_FRONT_PORCH + V_SYNC + V_BACK_PORCH)  // 525

// System clock: 252 MHz → TMDS serial rate ≈ 252 Mbit/s ≈ 25.2 MHz pixel clock
#define SYS_CLK_KHZ     252000u

// ---------------------------------------------------------------------------
// HSTX pin base (GP12..GP19 → HSTX lanes 0..7)
// ---------------------------------------------------------------------------
#define DVI_PIN_BASE    12u

// ---------------------------------------------------------------------------
// TMDS control symbols (10-bit, transmitted on channel 0 during blanking)
// Channel 0 (Blue) encodes HSYNC/VSYNC state.
// Channels 1 (Green) and 2 (Red) always send CTRL_00 during blanking.
// ---------------------------------------------------------------------------
#define TMDS_CTRL_00    0x354u   // no HSYNC, no VSYNC
#define TMDS_CTRL_01    0x0ABu   // HSYNC,    no VSYNC
#define TMDS_CTRL_10    0x154u   // no HSYNC, VSYNC
#define TMDS_CTRL_11    0x2ABu   // HSYNC,    VSYNC

// Pack three 10-bit TMDS control symbols into a 32-bit word.
// Layout: [29:20]=CH2, [19:10]=CH1, [9:0]=CH0
#define PACK_CTRL(ch2, ch1, ch0) \
    ((uint32_t)(((ch2) << 20) | ((ch1) << 10) | (ch0)))

// Pre-computed blanking words for each sync state.
// All use CTRL_00 on CH1/CH2; CH0 encodes the HSYNC/VSYNC combination.
static const uint32_t BLANK_NOSYNC  = PACK_CTRL(TMDS_CTRL_00, TMDS_CTRL_00, TMDS_CTRL_00);
static const uint32_t BLANK_HSYNC   = PACK_CTRL(TMDS_CTRL_00, TMDS_CTRL_00, TMDS_CTRL_01);
static const uint32_t BLANK_VSYNC   = PACK_CTRL(TMDS_CTRL_00, TMDS_CTRL_00, TMDS_CTRL_10);
static const uint32_t BLANK_HVSYNC  = PACK_CTRL(TMDS_CTRL_00, TMDS_CTRL_00, TMDS_CTRL_11);

// ---------------------------------------------------------------------------
// Framebuffer: 640×480, 1 bit per pixel, row-major, LSB = leftmost pixel
// ---------------------------------------------------------------------------
#define FB_WIDTH    640u
#define FB_HEIGHT   480u
#define FB_WORDS    (FB_WIDTH / 32u)   // 20 uint32_t per row

static uint32_t framebuf[FB_HEIGHT][FB_WORDS];

// ---------------------------------------------------------------------------
// DMA state
// ---------------------------------------------------------------------------
static int  dma_ch;
static volatile uint32_t cur_scanline;

// Blanking line buffers (one word repeated H_TOTAL/32 times per line).
// H_TOTAL=800 pixels; 800/32=25 words per blanking line.
#define BLANK_WORDS (H_TOTAL / 32u)   // 25
static uint32_t blank_line_nosync [BLANK_WORDS];
static uint32_t blank_line_hsync  [BLANK_WORDS];
static uint32_t blank_line_vsync  [BLANK_WORDS];
static uint32_t blank_line_hvsync [BLANK_WORDS];

// ---------------------------------------------------------------------------
// Framebuffer utilities
// ---------------------------------------------------------------------------
static inline void fb_set_pixel(uint32_t x, uint32_t y, bool on) {
    if (x >= FB_WIDTH || y >= FB_HEIGHT) return;
    if (on) framebuf[y][x / 32u] |=  (1u << (x % 32u));
    else    framebuf[y][x / 32u] &= ~(1u << (x % 32u));
}

// 8×8 checkerboard test pattern
static void draw_test_pattern(void) {
    memset(framebuf, 0, sizeof(framebuf));
    for (uint32_t y = 0; y < FB_HEIGHT; y++) {
        for (uint32_t x = 0; x < FB_WIDTH; x++) {
            fb_set_pixel(x, y, (((x >> 3) ^ (y >> 3)) & 1u) != 0u);
        }
    }
}

// ---------------------------------------------------------------------------
// HSTX initialisation
// ---------------------------------------------------------------------------
static void hstx_init(void) {
    // Assign GPIO function HSTX for GP12..GP19
    for (uint i = DVI_PIN_BASE; i < DVI_PIN_BASE + 8u; i++) {
        gpio_set_function(i, GPIO_FUNC_HSTX);
    }

    // BIT[n] maps HSTX lane n (= GPn+12) to a TMDS channel.
    // SEL field values (RP2350 TRM, Table 567):
    //   0 = TMDS CH0 current serial bit (Blue)
    //  10 = TMDS CH1 current serial bit (Green)
    //  20 = TMDS CH2 current serial bit (Red)
    //  30 = TMDS CLK current serial bit
    // INV bit: invert for the differential "-" pin.
    //
    // Lane 0 = GP12 = D0-  : CH0, inverted
    // Lane 1 = GP13 = D0+  : CH0, normal
    // Lane 2 = GP14 = D1-  : CH1, inverted
    // Lane 3 = GP15 = D1+  : CH1, normal
    // Lane 4 = GP16 = D2-  : CH2, inverted
    // Lane 5 = GP17 = D2+  : CH2, normal
    // Lane 6 = GP18 = CLK- : CLK, inverted
    // Lane 7 = GP19 = CLK+ : CLK, normal
    hstx_ctrl_hw->bit[0] = (0u  << HSTX_CTRL_BIT0_SEL_P_LSB) | HSTX_CTRL_BIT0_INV_BITS;
    hstx_ctrl_hw->bit[1] = (0u  << HSTX_CTRL_BIT1_SEL_P_LSB);
    hstx_ctrl_hw->bit[2] = (10u << HSTX_CTRL_BIT2_SEL_P_LSB) | HSTX_CTRL_BIT2_INV_BITS;
    hstx_ctrl_hw->bit[3] = (10u << HSTX_CTRL_BIT3_SEL_P_LSB);
    hstx_ctrl_hw->bit[4] = (20u << HSTX_CTRL_BIT4_SEL_P_LSB) | HSTX_CTRL_BIT4_INV_BITS;
    hstx_ctrl_hw->bit[5] = (20u << HSTX_CTRL_BIT5_SEL_P_LSB);
    hstx_ctrl_hw->bit[6] = (30u << HSTX_CTRL_BIT6_SEL_P_LSB) | HSTX_CTRL_BIT6_INV_BITS;
    hstx_ctrl_hw->bit[7] = (30u << HSTX_CTRL_BIT7_SEL_P_LSB);

    // EXPAND_TMDS: 1bpp monochrome.
    // All three channels read the same 1-bit value (ROT=0) from the shift register.
    // N_TMDS_SYM_M1=31 → 32 TMDS symbols per 32-bit FIFO word (one per pixel bit).
    hstx_ctrl_hw->expand_tmds =
        (0u  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB) |
        (0u  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB)   |
        (0u  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB) |
        (0u  << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB)   |
        (0u  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB) |
        (0u  << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB)   |
        (31u << HSTX_CTRL_EXPAND_TMDS_N_TMDS_SYM_M1_LSB);

    // EXPAND_SHIFT: advance shift register by 1 bit after each symbol.
    hstx_ctrl_hw->expand_shift =
        (1u << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB) |
        (1u << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB);

    // CSR: enable, TMDS expand on, pixel clock = sys_clk / 10 ≈ 25.2 MHz.
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_ENABLE_BITS       |
        HSTX_CTRL_CSR_EXPAND_EN_BITS    |
        ((10u - 1u) << HSTX_CTRL_CSR_CLKDIV_LSB);
}

// ---------------------------------------------------------------------------
// DMA scanline IRQ
// Fires at end of each DMA transfer (= end of one scanline's pixel data).
// Queues the next scanline or blanking line.
// ---------------------------------------------------------------------------
static void __isr dma_irq_handler(void) {
    dma_hw->ints0 = (1u << dma_ch);   // acknowledge

    cur_scanline++;
    if (cur_scanline >= V_TOTAL) {
        cur_scanline = 0u;
    }

    uint32_t line = cur_scanline;

    if (line < V_ACTIVE) {
        // Active line: stream 1bpp pixel words from framebuffer.
        dma_channel_set_read_addr(dma_ch, framebuf[line], false);
        dma_channel_set_trans_count(dma_ch, FB_WORDS, true);

    } else if (line < V_ACTIVE + V_FRONT_PORCH) {
        // Vertical front porch: no sync
        dma_channel_set_read_addr(dma_ch, blank_line_nosync, false);
        dma_channel_set_trans_count(dma_ch, BLANK_WORDS, true);

    } else if (line < V_ACTIVE + V_FRONT_PORCH + V_SYNC) {
        // Vertical sync: VSYNC active, HSYNC also active (DVI polarity: both negative)
        dma_channel_set_read_addr(dma_ch, blank_line_hvsync, false);
        dma_channel_set_trans_count(dma_ch, BLANK_WORDS, true);

    } else {
        // Vertical back porch: no sync
        dma_channel_set_read_addr(dma_ch, blank_line_nosync, false);
        dma_channel_set_trans_count(dma_ch, BLANK_WORDS, true);
    }

    (void)blank_line_hsync;   // used for horizontal blanking (handled within line)
    (void)blank_line_vsync;
}

// ---------------------------------------------------------------------------
// DMA initialisation
// ---------------------------------------------------------------------------
static void dma_init(void) {
    // Fill blanking line buffers
    for (uint i = 0; i < BLANK_WORDS; i++) {
        blank_line_nosync[i] = BLANK_NOSYNC;
        blank_line_hsync[i]  = BLANK_HSYNC;
        blank_line_vsync[i]  = BLANK_VSYNC;
        blank_line_hvsync[i] = BLANK_HVSYNC;
    }

    dma_ch = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, DREQ_HSTX);

    dma_channel_configure(
        dma_ch,
        &cfg,
        &hstx_fifo_hw->fifo,  // write to HSTX FIFO
        framebuf[0],           // start from line 0
        FB_WORDS,
        false
    );

    dma_channel_set_irq0_enabled(dma_ch, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    cur_scanline = 0u;
    dma_channel_start(dma_ch);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(void) {
    set_sys_clock_khz(SYS_CLK_KHZ, true);
    stdio_init_all();

    draw_test_pattern();
    hstx_init();
    dma_init();

    while (true) {
        tight_loop_contents();
    }
}
