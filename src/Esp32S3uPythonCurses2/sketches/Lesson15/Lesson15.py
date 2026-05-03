import os, sys, io
from machine import Pin
import dht
import time

# DHT11 temperature and humidity sensor
# Wiring:
#   DATA → GP3  (single-wire protocol)
#   VCC  → 3.3 V
#   GND  → GND
#
# DHT11 specs:
#   Temperature: 0–50 °C   ±2 °C   resolution 1 °C
#   Humidity:    20–90 %   ±5 %    resolution 1 %
#   Minimum interval between readings: 1 s (use 2 s to be safe)

_sensor = dht.DHT11(Pin(3))


def read_dht():
    # One measure() call populates both temperature and humidity
    _sensor.measure()
    return _sensor.temperature(), _sensor.humidity()


def setup():
    print('DHT11 ready  DATA=GP3')


def loop():
    temp, hum = read_dht()
    print('Temp: ' + str(temp) + ' C   Hum: ' + str(hum) + ' %')
    if temp > 30 or hum > 80:
        print('WARNING: high temp or humidity!')
    time.sleep_ms(2000)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
