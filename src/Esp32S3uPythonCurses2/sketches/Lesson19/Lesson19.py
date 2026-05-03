import os, sys, io
from machine import Pin, ADC
import time

# PS2 Joystick module (KY-023 or similar)
#
# Wiring:
#   VRx → GP1  (ADC1_CH0)
#   VRy → GP6  (ADC1_CH5)
#   SW  → GP8  (digital, active LOW — internal pull-up enabled)
#   VCC → 3.3 V
#   GND → GND

_adc_x = ADC(Pin(1), atten=ADC.ATTN_11DB)
_adc_y = ADC(Pin(6), atten=ADC.ATTN_11DB)
_sw    = Pin(8, Pin.IN, Pin.PULL_UP)  # active LOW: 0 = pressed, 1 = released


def read_joystick():
    x   = _adc_x.read()   # 0 (left)  – 4095 (right), ~2048 at center
    y   = _adc_y.read()   # 0 (up)    – 4095 (down),  ~2048 at center
    btn = not _sw.value() # True when button is pressed
    return x, y, btn


def joy_direction(x, y):
    h = 'LEFT'  if x < 1000 else ('RIGHT' if x > 3000 else '')
    v = 'UP'    if y < 1000 else ('DOWN'  if y > 3000 else '')
    if h and v:
        return h + '-' + v
    return h or v or 'CENTER'


def setup():
    print('Joystick ready   VRx=GP1  VRy=GP6  SW=GP8')


def loop():
    x, y, btn = read_joystick()
    direction  = joy_direction(x, y)
    btn_str    = ' [BTN]' if btn else ''
    print('X:' + str(x) + '  Y:' + str(y) + '  ' + direction + btn_str)
    time.sleep_ms(200)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
