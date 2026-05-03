import os, sys, io
from machine import Pin, ADC
import time

# KY-018 photoresistor module — light-level measurement via ADC
#
# Wiring:
#   S  (signal) → GP7  (ADC1_CH6)
#   +  (VCC)    → 3.3 V
#   -  (GND)    → GND

_adc = ADC(Pin(7), atten=ADC.ATTN_11DB)  # full 0–3.3 V range → 0–4095


def read_light():
    return _adc.read()  # raw 12-bit value: 0 (dark) – 4095 (bright)


def light_category(raw):
    if raw < 1000:
        return 'DARK'
    elif raw < 2500:
        return 'NORMAL'
    else:
        return 'BRIGHT'


def setup():
    print('KY-018 ready   ADC=GP7')


def loop():
    raw      = read_light()
    category = light_category(raw)
    print('Light: ' + str(raw) + '  ' + category)
    time.sleep_ms(500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
