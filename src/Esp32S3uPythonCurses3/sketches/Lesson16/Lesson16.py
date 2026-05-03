import os, sys, io
from machine import Pin, time_pulse_us, I2C
from lcd1602 import LCD1602
import time

# HC-SR04 ultrasonic distance sensor + LCD1602 display
#
# Wiring — HC-SR04:
#   TRIG → GP5
#   ECHO → GP4
#   VCC  → 3.3 V
#   GND  → GND
#
# Wiring — LCD1602 I2C:
#   SDA → GP21
#   SCL → GP22
#   VCC → 3.3 V
#   GND → GND

_trig = Pin(5, Pin.OUT)
_echo = Pin(4, Pin.IN)

_i2c = I2C(0, sda=Pin(21), scl=Pin(22), freq=400000)
_lcd = LCD1602(_i2c, 0x27)


def measure_distance():
    _trig.value(0)
    time.sleep_us(2)
    _trig.value(1)
    time.sleep_us(10)
    _trig.value(0)
    duration = time_pulse_us(_echo, 1, 30_000)
    if duration < 0:
        return -1
    return duration / 58


def setup():
    _trig.value(0)
    _lcd.clear()
    _lcd.write_line(0, 'HC-SR04 ready')
    _lcd.write_line(1, 'TRIG=GP5 ECHO=GP4')


def loop():
    dist = measure_distance()
    _lcd.write_line(0, 'Distance:')
    if dist < 0 or dist < 2 or dist > 400:
        _lcd.write_line(1, 'Out of range')
    else:
        _lcd.write_line(1, str(round(dist, 1)) + ' cm')
    time.sleep_ms(500)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
