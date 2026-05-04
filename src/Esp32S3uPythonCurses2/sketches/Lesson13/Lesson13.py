import os, sys, io
from machine import Pin, SPI
import time

# MAX7219 8×8 LED matrix
# Wiring:
#   CLK  (SCK)  → GP18
#   DIN  (MOSI) → GP19
#   CS          → GP5
#   VCC         → 3.3 V  (or 5 V for brighter display)
#   GND         → GND
#   GP4 (MISO)  → not connected (required by the SPI driver, leave floating)

_REG_DECODE    = 0x09  # pixel decode mode
_REG_INTENSITY = 0x0A  # brightness 0x00–0x0F
_REG_SCANLIMIT = 0x0B  # number of active rows (0x07 = all 8)
_REG_SHUTDOWN  = 0x0C  # 0 = shutdown, 1 = normal operation
_REG_DISPTEST  = 0x0F  # 1 = all LEDs on (test), 0 = normal

_spi = SPI(1, baudrate=10_000_000, polarity=0, phase=0,
           sck=Pin(18), mosi=Pin(19), miso=Pin(4))
_cs  = Pin(5, Pin.OUT, value=1)


def _write(reg, data):
    _cs.value(0)
    _spi.write(bytes([reg, data]))
    _cs.value(1)


def init_max7219():
    _write(_REG_DECODE,    0x00)  # raw pixel mode (no BCD decode for digits)
    _write(_REG_INTENSITY, 0x04)  # medium brightness
    _write(_REG_SCANLIMIT, 0x07)  # scan all 8 rows
    _write(_REG_SHUTDOWN,  0x01)  # wake up (exit shutdown mode)
    _write(_REG_DISPTEST,  0x00)  # normal operation (not test)


def clear_display():
    for row in range(1, 9):
        _write(row, 0x00)


def show_pattern(pattern):
    for row_idx, bits in enumerate(pattern):
        _write(row_idx + 1, bits)


_patterns = [
    [0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C],  # Smiley
    [0x00, 0x66, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00],  # Heart
    [0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81],  # Cross
    [0x18, 0x3C, 0x7E, 0xFF, 0x18, 0x18, 0x18, 0x18],  # Arrow up
]
_names = ['Smiley', 'Heart', 'Cross', 'Arrow up']
_step  = 0


def setup():
    init_max7219()
    clear_display()
    print('MAX7219 ready  CLK=GP18  DIN=GP19  CS=GP5')


def loop():
    global _step
    show_pattern(_patterns[_step])
    print(_names[_step])
    _step = (_step + 1) % len(_patterns)
    time.sleep_ms(1500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
