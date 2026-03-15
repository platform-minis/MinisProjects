import os, sys, io
from machine import Pin
import time

_pin_11 = Pin(11, mode=Pin.OUT)

def setup():
    pass

def loop():
    _pin_11.value(1)
    time.sleep_ms(1000)
    _pin_11.value(0)
    time.sleep_ms(1000)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
