import os, sys, io
from machine import Pin, time_pulse_us
import time

# HC-SR04 ultrasonic distance sensor
# Wiring:
#   TRIG → GP5
#   ECHO → GP4
#   VCC  → 3.3 V  (keeps ECHO output at 3.3 V — safe for GPIO input)
#   GND  → GND
#
# Operating range: 2–400 cm
# Resolution:      ~0.3 cm
# distance [cm] = echo_duration [µs] / 58

_trig = Pin(5, Pin.OUT)
_echo = Pin(4, Pin.IN)


def measure_distance():
    # 10 µs trigger pulse
    _trig.value(0)
    time.sleep_us(2)
    _trig.value(1)
    time.sleep_us(10)
    _trig.value(0)
    # Measure echo duration; 30 000 µs timeout ≈ 500 cm — returns -1 on timeout
    duration = time_pulse_us(_echo, 1, 30_000)
    if duration < 0:
        return -1
    return duration / 58


def setup():
    _trig.value(0)
    print('HC-SR04 ready  TRIG=GP5  ECHO=GP4')


def loop():
    dist = measure_distance()
    if dist < 0 or dist < 2 or dist > 400:
        print('Out of range')
    else:
        print('Distance: ' + str(round(dist, 1)) + ' cm')
    time.sleep_ms(500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
