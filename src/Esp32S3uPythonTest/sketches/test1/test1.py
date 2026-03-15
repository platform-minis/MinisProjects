import os, sys, io
from machine import Pin
import time

_pin_11 = Pin(11, mode=Pin.OUT)
item = None

def setup():
    global item
    item = 0

def loop():
    global item
    print(item)
    item += 1
    _pin_11.on()
    time.sleep_ms(3000)
    _pin_11.off()
    time.sleep_ms(3000)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
