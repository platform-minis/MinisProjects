import os, sys, io
from machine import Pin
import time

_pin_11 = Pin(11, mode=Pin.OUT)
_pin_16 = Pin(16, mode=Pin.IN)

def setup():
    pass

def loop():
    if _pin_16.value() == 1:
        _pin_11.value(1)
        print('Button pressed')
    else:
        _pin_11.value(0)
    time.sleep_ms(100)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
