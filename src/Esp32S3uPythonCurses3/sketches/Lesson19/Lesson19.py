import os, sys, io
from machine import Pin, ADC, I2C
from lcd1602 import LCD1602
import time

# PS2 Joystick + LCD1602 display
#
# Wiring — Joystick:
#   VRx → GP1  (ADC)
#   VRy → GP6  (ADC)
#   SW  → GP8  (active LOW, pull-up)
#   VCC → 3.3 V
#   GND → GND
#
# Wiring — LCD1602 I2C:
#   SDA → GP21
#   SCL → GP22
#   VCC → 3.3 V
#   GND → GND

_adc_x = ADC(Pin(1), atten=ADC.ATTN_11DB)
_adc_y = ADC(Pin(6), atten=ADC.ATTN_11DB)
_sw    = Pin(8, Pin.IN, Pin.PULL_UP)

_i2c = I2C(0, sda=Pin(21), scl=Pin(22), freq=400000)
_lcd = LCD1602(_i2c, 0x27)


def read_joystick():
    x   = _adc_x.read()
    y   = _adc_y.read()
    btn = not _sw.value()
    return x, y, btn


def joy_direction(x, y):
    h = 'LEFT'  if x < 1000 else ('RIGHT' if x > 3000 else '')
    v = 'UP'    if y < 1000 else ('DOWN'  if y > 3000 else '')
    if h and v:
        return h + '-' + v
    return h or v or 'CENTER'


def setup():
    _lcd.clear()
    _lcd.write_line(0, 'Joystick ready')
    _lcd.write_line(1, 'GP1 GP6 SW=GP8')


def loop():
    x, y, btn = read_joystick()
    direction  = joy_direction(x, y)
    btn_str    = ' [BTN]' if btn else ''
    _lcd.write_line(0, 'X:' + str(x) + ' Y:' + str(y))
    _lcd.write_line(1, direction + btn_str)
    time.sleep_ms(200)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
