import os, sys, io
from machine import Pin
import time

_led = Pin(21, mode=Pin.OUT)
counter = None

def setup():
    global counter
    counter = 0
    print("ESP32-S3 Zero - Hello!")

def loop():
    global counter
    print(counter)
    counter += 1
    _led.on()
    time.sleep_ms(1000)
    _led.off()
    time.sleep_ms(1000)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
