import os, sys, io
from machine import Pin
import dht
import time

# DHT11 temperature and humidity sensor
# Pinout:
#   DATA → GP3  (single-wire protocol, internal pull-up recommended)
#
# DHT11 specs:
#   Temperature: 0–50 °C  ±2 °C resolution 1 °C
#   Humidity:    20–90 %  ±5 % resolution 1 %
#   Min interval between readings: 1 s (recommended 2 s)

_sensor = dht.DHT11(Pin(3))


def read_temperature():
    _sensor.measure()
    return _sensor.temperature()


def read_humidity():
    _sensor.measure()
    return _sensor.humidity()


def setup():
    print('DHT11 ready  DATA=GP3')


def loop():
    temp = read_temperature()
    hum  = read_humidity()
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
