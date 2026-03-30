"""
touch_demo.py — CheapYellowDisplay touchscreen paint demo.

Draw on the screen by touching it.  Use the colour buttons at the
bottom to change the pen colour.  Tap "Clear" to erase.

Hardware (CYD / ESP32-2432S028):
    Display   ILI9341  SPI1  MOSI=13 MISO=12 CLK=14 CS=15 DC=2 BL=21
    Touch     XPT2046  SoftSPI  CLK=25 MOSI=32 MISO=39  CS=33 IRQ=36

Note: GPIO 39 is input-only on ESP32 — touch bus MUST use SoftSPI.
"""

import machine
import time
from ili9341 import ILI9341, color565, BLACK, WHITE, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA
from xpt2046 import XPT2046

# ── Display ───────────────────────────────────────────────────────────────────
spi = machine.SPI(1, baudrate=40_000_000,
                  sck=machine.Pin(14), mosi=machine.Pin(13), miso=machine.Pin(12))
display = ILI9341(spi,
                  cs=machine.Pin(15, machine.Pin.OUT),
                  dc=machine.Pin(2,  machine.Pin.OUT),
                  bl=machine.Pin(21, machine.Pin.OUT),
                  rotation=1)

# ── Touch (SoftSPI required — GPIO 39 is input-only) ─────────────────────────
touch_spi = machine.SoftSPI(baudrate=1_000_000,
                             sck=machine.Pin(25),
                             mosi=machine.Pin(32),
                             miso=machine.Pin(39))
touch = XPT2046(touch_spi,
                cs=machine.Pin(33, machine.Pin.OUT),
                irq=machine.Pin(36, machine.Pin.IN),
                size=(320, 240))

# ── RGB LED (active LOW) ──────────────────────────────────────────────────────
led_r = machine.Pin(4,  machine.Pin.OUT, value=1)
led_g = machine.Pin(16, machine.Pin.OUT, value=1)
led_b = machine.Pin(17, machine.Pin.OUT, value=1)

# ── Palette buttons ───────────────────────────────────────────────────────────
PALETTE = [
    RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA,
    WHITE, color565(255, 128, 0),   # orange
]
BTN_H     = 36
BTN_W     = 320 // len(PALETTE)
CANVAS_Y  = BTN_H   # canvas starts below the palette row

# "Clear" button occupies the full top bar
CLEAR_X, CLEAR_Y, CLEAR_W, CLEAR_H = 0, 0, 320, BTN_H

pen_color = RED
pen_idx   = 0


def draw_palette(active_idx):
    for i, c in enumerate(PALETTE):
        x = i * BTN_W
        display.fill_rect(x, 0, BTN_W, BTN_H, c)
        if i == active_idx:
            display.rect(x, 0, BTN_W, BTN_H, WHITE)

    # "CLR" label on first button (overlay)
    display.text('CLR', 2, 10, BLACK, PALETTE[0], scale=2)


def clear_canvas():
    display.fill_rect(0, CANVAS_Y, 320, 240 - CANVAS_Y, BLACK)
    draw_palette(pen_idx)


# ── Startup ───────────────────────────────────────────────────────────────────
display.fill(BLACK)
draw_palette(pen_idx)
print('Touch demo — draw with your finger, tap palette to change colour')
print('Tap the leftmost (red) button to clear the canvas')

# ── Main loop ─────────────────────────────────────────────────────────────────
while True:
    pos = touch.read()
    if pos:
        tx, ty = pos

        if ty < BTN_H:
            # Palette row hit
            idx = min(tx // BTN_W, len(PALETTE) - 1)
            if idx == 0:
                clear_canvas()
            else:
                pen_idx   = idx
                pen_color = PALETTE[idx]
                draw_palette(pen_idx)
            time.sleep_ms(120)   # debounce

        elif ty >= CANVAS_Y:
            # Draw a dot (3x3 block for visibility)
            display.fill_rect(tx - 1, ty - 1, 3, 3, pen_color)

    time.sleep_ms(15)
