import os, sys, io
from machine import Pin, SPI, I2C
from lcd1602 import LCD1602
import time

# MAX7219 8×8 LED matrix + LCD1602 display
#
# Wiring — MAX7219:
#   CLK  (SCK)  → GP18
#   DIN  (MOSI) → GP19
#   CS          → GP5
#   VCC         → 3.3 V
#   GND         → GND
#   GP4 (MISO)  → not connected
#
# Wiring — LCD1602 I2C:
#   SDA → GP21
#   SCL → GP22
#   VCC → 3.3 V
#   GND → GND

# ─── MAX7219 ──────────────────────────────────────────────────────────────────
_REG_DECODE    = 0x09
_REG_INTENSITY = 0x0A
_REG_SCANLIMIT = 0x0B
_REG_SHUTDOWN  = 0x0C
_REG_DISPTEST  = 0x0F

_spi = SPI(1, baudrate=10_000_000, polarity=0, phase=0,
           sck=Pin(18), mosi=Pin(19), miso=Pin(4))
_cs  = Pin(5, Pin.OUT, value=1)


def _write(reg, data):
    _cs.value(0)
    _spi.write(bytes([reg, data]))
    _cs.value(1)


def init_max7219():
    _write(_REG_DECODE,    0x00)
    _write(_REG_INTENSITY, 0x04)
    _write(_REG_SCANLIMIT, 0x07)
    _write(_REG_SHUTDOWN,  0x01)
    _write(_REG_DISPTEST,  0x00)


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

# ─── LCD1602 ──────────────────────────────────────────────────────────────────
_i2c = I2C(0, sda=Pin(21), scl=Pin(22), freq=400000)
_lcd = LCD1602(_i2c, 0x27)


def setup():
    init_max7219()
    clear_display()
    _lcd.clear()
    _lcd.write_line(0, 'MAX7219 + LCD')
    _lcd.write_line(1, 'Ready')


def loop():
    global _step
    show_pattern(_patterns[_step])
    _lcd.write_line(0, 'Pattern:')
    _lcd.write_line(1, _names[_step])
    _step = (_step + 1) % len(_patterns)
    time.sleep_ms(1500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
