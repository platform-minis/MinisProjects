import os, sys, io
from machine import Pin
import time

_pin_12 = Pin(12, mode=Pin.OUT)
_pin_13 = Pin(13, mode=Pin.OUT)
_pin_14 = Pin(14, mode=Pin.OUT)

def setup():
    pass

def loop():
    _pin_14.value(1)
    time.sleep_ms(3000)
    _pin_14.value(0)
    _pin_13.value(1)
    time.sleep_ms(1000)
    _pin_13.value(0)
    _pin_12.value(1)
    time.sleep_ms(3000)
    _pin_12.value(0)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
