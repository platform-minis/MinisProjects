import os, sys, io
from machine import Pin, I2C
from lcd1602 import LCD1602
import dht
import time

# DHT11 temperature & humidity sensor + LCD1602 display
#
# Wiring — DHT11:
#   DATA → GP3
#   VCC  → 3.3 V
#   GND  → GND
#
# Wiring — LCD1602 I2C:
#   SDA → GP21
#   SCL → GP22
#   VCC → 3.3 V
#   GND → GND

_sensor = dht.DHT11(Pin(3))

_i2c = I2C(0, sda=Pin(21), scl=Pin(22), freq=400000)
_lcd = LCD1602(_i2c, 0x27)


def read_dht():
    _sensor.measure()
    return _sensor.temperature(), _sensor.humidity()


def setup():
    _lcd.clear()
    _lcd.write_line(0, 'DHT11 ready')
    _lcd.write_line(1, 'DATA=GP3')


def loop():
    temp, hum = read_dht()
    _lcd.write_line(0, 'Temp: ' + str(temp) + ' C')
    _lcd.write_line(1, 'Hum:  ' + str(hum) + ' %')
    time.sleep_ms(2000)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
