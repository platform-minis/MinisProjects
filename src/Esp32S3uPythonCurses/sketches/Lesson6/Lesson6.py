import os, sys, io
from machine import Pin
from machine import ADC
import time

_adc_7 = ADC(Pin(7), atten=ADC.ATTN_11DB)
Light = None

def setup():
    global Light
    pass

def loop():
    global Light
    Light = _adc_7.read()
    print(str('Light level') + str(Light))
    if Light < 1000:
        print('DARK')
    elif Light < 2500:
        print('NORMAL')
    else:
        print('BRIGHT')
    time.sleep_ms(500)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
