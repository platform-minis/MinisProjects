import os, sys, io
from machine import Pin
import time

# HC-SR04 ultrasonic distance sensor
# Pinout:
#   TRIG → GP5  (output — sends 10µs trigger pulse)
#   ECHO → GP4  (input  — high for the duration of the return echo)
#
# Distance formula:
#   duration [µs] / 58 = distance [cm]
#   (speed of sound ~343 m/s, round-trip so ÷ 2 already baked in)

_pin_5 = Pin(5, mode=Pin.OUT)
_pin_4 = Pin(4, mode=Pin.IN)


def measure_distance():
    # Send trigger pulse: LOW 2µs → HIGH 10µs → LOW
    _pin_5.value(0)
    time.sleep_us(2)
    _pin_5.value(1)
    time.sleep_us(10)
    _pin_5.value(0)
    # Wait for echo to go HIGH
    while not (_pin_4.value() == 1):
        pass
    start = time.ticks_us()
    # Wait for echo to go LOW
    while not (_pin_4.value() == 0):
        pass
    duration = time.ticks_us() - start
    return duration / 58


def setup():
    print('HC-SR04 ready  TRIG=GP5  ECHO=GP4')


def loop():
    dist = measure_distance()
    if dist > 400 or dist < 2:
        print('Out of range')
    else:
        print('Distance: ' + str(dist) + ' cm')
    time.sleep_ms(500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
