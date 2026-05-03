import os, sys, io
from machine import Pin, ADC, I2C
from lcd1602 import LCD1602
import time

# KY-018 light sensor + LCD1602 display
#
# Wiring — KY-018:
#   S   → GP7  (ADC)
#   +   → 3.3 V
#   −   → GND
#
# Wiring — LCD1602 I2C:
#   SDA → GP21
#   SCL → GP22
#   VCC → 3.3 V
#   GND → GND

_adc = ADC(Pin(7), atten=ADC.ATTN_11DB)

_i2c = I2C(0, sda=Pin(21), scl=Pin(22), freq=400000)
_lcd = LCD1602(_i2c, 0x27)


def read_light():
    return _adc.read()


def light_category(raw):
    if raw < 1000:
        return 'DARK'
    elif raw < 2500:
        return 'NORMAL'
    else:
        return 'BRIGHT'


def setup():
    _lcd.clear()
    _lcd.write_line(0, 'KY-018 ready')
    _lcd.write_line(1, 'ADC=GP7')


def loop():
    raw = read_light()
    category = light_category(raw)
    _lcd.write_line(0, 'Light: ' + str(raw))
    _lcd.write_line(1, category)
    time.sleep_ms(500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
