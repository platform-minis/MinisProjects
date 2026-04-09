import os, sys, io
from machine import Pin
import time

# Silnik krokowy 28BYJ-48 przez sterownik ULN2003
# IN1=GP4, IN2=GP5, IN3=GP6, IN4=GP17
_pin_4 = Pin(4, mode=Pin.OUT)
_pin_5 = Pin(5, mode=Pin.OUT)
_pin_6 = Pin(6, mode=Pin.OUT)
_pin_17 = Pin(17, mode=Pin.OUT)

def setup():
    pass

def loop():
    # Krok 1
    _pin_4.value(1)
    _pin_5.value(0)
    _pin_6.value(1)
    _pin_17.value(0)
    time.sleep_ms(3)
    # Krok 2
    _pin_4.value(0)
    _pin_5.value(1)
    _pin_6.value(1)
    _pin_17.value(0)
    time.sleep_ms(3)
    # Krok 3
    _pin_4.value(0)
    _pin_5.value(1)
    _pin_6.value(0)
    _pin_17.value(1)
    time.sleep_ms(3)
    # Krok 4
    _pin_4.value(1)
    _pin_5.value(0)
    _pin_6.value(0)
    _pin_17.value(1)
    time.sleep_ms(3)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
