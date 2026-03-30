"""
hello_display.py — CheapYellowDisplay basic demo.

Cycles through colours, draws shapes and text, blinks the RGB LED,
and shows the LDR (ambient light) reading.

Hardware (CYD / ESP32-2432S028):
    Display ILI9341 (320x240)
      SPI1  MOSI=13  MISO=12  CLK=14
      CS=15  DC=2  BL=21

    RGB LED (active LOW)  R=4  G=16  B=17
    LDR (ADC)             GPIO=34
"""

import machine
import time
from ili9341 import ILI9341, color565, BLACK, WHITE, RED, GREEN, BLUE, YELLOW, CYAN, ORANGE, GRAY

# ── Display ───────────────────────────────────────────────────────────────────
spi = machine.SPI(1, baudrate=40_000_000,
                  sck=machine.Pin(14), mosi=machine.Pin(13), miso=machine.Pin(12))
display = ILI9341(spi,
                  cs=machine.Pin(15, machine.Pin.OUT),
                  dc=machine.Pin(2,  machine.Pin.OUT),
                  bl=machine.Pin(21, machine.Pin.OUT),
                  rotation=1)   # landscape 320x240

# ── RGB LED (active LOW — value(0) = ON) ─────────────────────────────────────
led_r = machine.Pin(4,  machine.Pin.OUT, value=1)
led_g = machine.Pin(16, machine.Pin.OUT, value=1)
led_b = machine.Pin(17, machine.Pin.OUT, value=1)

def set_led(r=0, g=0, b=0):
    led_r.value(0 if r else 1)
    led_g.value(0 if g else 1)
    led_b.value(0 if b else 1)

# ── LDR ───────────────────────────────────────────────────────────────────────
ldr = machine.ADC(machine.Pin(34))
ldr.atten(machine.ADC.ATTN_11DB)   # 0–3.3V range

# ─────────────────────────────────────────────────────────────────────────────

SLIDES = [
    (RED,    'Red',    (1, 0, 0)),
    (GREEN,  'Green',  (0, 1, 0)),
    (BLUE,   'Blue',   (0, 0, 1)),
    (YELLOW, 'Yellow', (1, 1, 0)),
    (CYAN,   'Cyan',   (0, 1, 1)),
    (ORANGE, 'Orange', (1, 0, 0)),
    (color565(128, 0, 128), 'Purple', (1, 0, 1)),
]

print('CheapYellowDisplay — hello demo starting')

while True:
    for bg, name, led in SLIDES:
        # Decide text colour for readability on bright backgrounds
        fg = BLACK if name in ('Yellow', 'Cyan', 'Orange', 'White') else WHITE

        display.fill(bg)

        # Header bar
        display.fill_rect(0, 0, 320, 40, color565(0, 0, 60))
        display.text('CheapYellowDisplay', 6, 8, WHITE, color565(0, 0, 60), scale=2)

        # Colour name — large
        display.text(name, 8, 56, fg, bg, scale=3)

        # LDR reading
        light = ldr.read()
        display.text('Light: {:4d}'.format(light), 8, 112, fg, bg, scale=2)

        # Rainbow bar of rectangles
        bar_colors = [RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, color565(128, 0, 128)]
        for i, c in enumerate(bar_colors):
            display.fill_rect(8 + i * 44, 160, 40, 30, c)

        # Border
        display.rect(0, 0, 320, 240, WHITE)

        set_led(*led)
        time.sleep(2)

    set_led()   # all off
