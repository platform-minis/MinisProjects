import os, sys, io
from machine import Pin

_pin_11 = Pin(11, mode=Pin.OUT)

def setup():
    _pin_11.value(1)
    print('Led is On')

def loop():
    pass

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
