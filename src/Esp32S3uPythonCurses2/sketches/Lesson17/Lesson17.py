import os, sys, io
from machine import Pin
import time

# HC-SR501 PIR motion sensor
#
# Wiring:
#   OUT → GP2
#   VCC → 5V  (VBUS pin)
#   GND → GND
#
# After power-on the sensor needs ~30 s to stabilise — avoid false triggers.

_pir = Pin(2, Pin.IN)


def setup():
    print('HC-SR501 PIR ready   OUT=GP2')


def loop():
    if _pir.value():
        print('Motion detected!')
    else:
        print('--')
    time.sleep_ms(500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
