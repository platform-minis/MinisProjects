import os, sys, io
from machine import Pin
import time
import mfrc522

# RC-522 RFID reader — read card / tag UID over SPI
#
# Wiring:
#   SCK  → GP18
#   MOSI → GP19
#   MISO → GP16
#   SDA  → GP17  (CS / chip select)
#   RST  → GP15
#   3V3  → 3.3 V
#   GND  → GND

_reader = mfrc522.MFRC522(sck=18, mosi=19, miso=16, rst=15, cs=17)


def read_uid():
    stat, _ = _reader.request(_reader.REQIDL)
    if stat != _reader.OK:
        return None
    stat, uid = _reader.SelectTagSN()
    if stat != _reader.OK:
        return None
    return ':'.join('{:02X}'.format(b) for b in uid)


def setup():
    print('RC-522 ready   SCK=GP18 MOSI=GP19 MISO=GP16 SDA=GP17 RST=GP15')
    print('Hold a card or tag near the reader...')


def loop():
    uid = read_uid()
    if uid:
        print('Card UID: ' + uid)
        time.sleep_ms(1000)  # debounce — avoids printing the same card repeatedly
    else:
        time.sleep_ms(100)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
