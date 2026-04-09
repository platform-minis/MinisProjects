import os, sys, io
from machine import Pin
import time

# PIR passive infrared motion sensor
# Pinout:
#   PIR OUT → GP4  (input — HIGH when motion detected)
#   LED     → GP11 (output — lights up on motion)

_pin_4 = Pin(4, mode=Pin.IN)
_pin_11 = Pin(11, mode=Pin.OUT)


def is_motion_detected():
    return _pin_4.value() == 1


def setup():
    _pin_11.value(0)
    print('PIR ready  PIR=GP4  LED=GP11')


def loop():
    motion = is_motion_detected()
    if motion:
        _pin_11.value(1)
        print('Motion detected!')
    else:
        _pin_11.value(0)
    time.sleep_ms(100)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        _pin_11.value(0)
        print(e)
