"""
color_setup.py — micro-gui hardware config for CYD (ESP32-2432S028).

Deploy this file to the MicroPython root (/) alongside CYDTest.py.
micro-gui imports it automatically via ``from color_setup import ssd``.

CYD display pinout (ILI9341, hardware SPI1 / VSPI):
  SCK  = GPIO 14   MOSI = GPIO 13   MISO = GPIO 12
  CS   = GPIO 15   DC   = GPIO  2   BL   = GPIO 21

Reference: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display
"""

import machine
from drivers.display.ili9341 import ILI9341 as SSD

# SPI bus — 40 MHz, hardware SPI1
_spi = machine.SPI(1,
                   baudrate=40_000_000,
                   sck=machine.Pin(14),
                   mosi=machine.Pin(13),
                   miso=machine.Pin(12))

# Display — landscape 320 × 240
ssd = SSD(_spi,
          cs=machine.Pin(15, machine.Pin.OUT),
          dc=machine.Pin(2,  machine.Pin.OUT),
          rst=None,
          height=240,
          width=320)

# Backlight on
machine.Pin(21, machine.Pin.OUT).value(1)
