import os, sys, io
from machine import Pin, I2C
from lcd1602 import LCD1602
import time

# HC-SR501 PIR motion sensor + LCD1602 display
#
# Wiring — HC-SR501:
#   OUT → GP2
#   VCC → 5V  (VBUS pin)
#   GND → GND
#
# Wiring — LCD1602 I2C:
#   SDA → GP21
#   SCL → GP22
#   VCC → 3.3 V
#   GND → GND
#
# After power-on the sensor needs ~30 s to stabilise.

_pir = Pin(2, Pin.IN)

_i2c = I2C(0, sda=Pin(21), scl=Pin(22), freq=400000)
_lcd = LCD1602(_i2c, 0x27)


def setup():
    _lcd.clear()
    _lcd.write_line(0, 'HC-SR501 ready')
    _lcd.write_line(1, 'OUT=GP2')


def loop():
    _lcd.write_line(0, 'Motion:')
    if _pir.value():
        _lcd.write_line(1, 'DETECTED!')
    else:
        _lcd.write_line(1, 'waiting...')
    time.sleep_ms(500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
