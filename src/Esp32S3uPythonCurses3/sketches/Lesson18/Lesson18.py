import os, sys, io
from machine import Pin, I2C
from lcd1602 import LCD1602
import time
import mfrc522

# RC-522 RFID reader + LCD1602 display
#
# Wiring — RC-522:
#   SCK  → GP18
#   MOSI → GP19
#   MISO → GP16
#   SDA  → GP17  (CS / chip select)
#   RST  → GP15
#   3V3  → 3.3 V  (NEVER 5 V — damages the chip)
#   GND  → GND
#
# Wiring — LCD1602 I2C:
#   SDA → GP21
#   SCL → GP22
#   VCC → 3.3 V
#   GND → GND

_reader = mfrc522.MFRC522(sck=18, mosi=19, miso=16, rst=15, cs=17)

_i2c = I2C(0, sda=Pin(21), scl=Pin(22), freq=400000)
_lcd = LCD1602(_i2c, 0x27)


def read_uid():
    stat, _ = _reader.request(_reader.REQIDL)
    if stat != _reader.OK:
        return None
    stat, uid = _reader.SelectTagSN()
    if stat != _reader.OK:
        return None
    return ':'.join('{:02X}'.format(b) for b in uid)


def setup():
    _lcd.clear()
    _lcd.write_line(0, 'RFID reader')
    _lcd.write_line(1, 'Hold card...')


def loop():
    uid = read_uid()
    if uid:
        _lcd.write_line(0, 'UID:')
        _lcd.write_line(1, uid)
        time.sleep_ms(1000)
    else:
        _lcd.write_line(1, 'Hold card...')
        time.sleep_ms(100)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
