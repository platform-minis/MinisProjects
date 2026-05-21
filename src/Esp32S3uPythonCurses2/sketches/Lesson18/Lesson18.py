import os, sys, io
from machine import SPI, Pin
import time

# RC-522 RFID reader — read card / tag UID over SPI (inline driver, no external library)
#
# Wiring:
#   SCK  → GP18
#   MOSI → GP11
#   MISO → GP16
#   SDA  → GP17  (CS / chip select)
#   RST  → GP15
#   3V3  → 3.3 V
#   GND  → GND

_spi_r = SPI(1, baudrate=1_000_000, polarity=0, phase=0, sck=Pin(18), mosi=Pin(11), miso=Pin(16))
_cs_r  = Pin(17, Pin.OUT, value=1)
_rst_r = Pin(15, Pin.OUT, value=1)


def _rrd(a):
    _cs_r.value(0)
    _spi_r.write(bytes([((a << 1) & 0x7E) | 0x80]))
    v = _spi_r.read(1)[0]
    _cs_r.value(1)
    return v


def _rwr(a, v):
    _cs_r.value(0)
    _spi_r.write(bytes([(a << 1) & 0x7E, v]))
    _cs_r.value(1)


def _rset(a, m): _rwr(a, _rrd(a) | m)
def _rclr(a, m): _rwr(a, _rrd(a) & ~m)


def _transceive(data):
    _rwr(0x02, 0x77); _rclr(0x04, 0x80)
    _rset(0x0A, 0x80); _rwr(0x01, 0x00)
    for b in data: _rwr(0x09, b)
    _rwr(0x01, 0x0C); _rset(0x0D, 0x80)
    for _ in range(2000):
        if _rrd(0x04) & 0x31: break
    _rclr(0x0D, 0x80)
    if _rrd(0x06) & 0x1B: return None
    n = _rrd(0x0A) & 0x7F
    return [_rrd(0x09) for _ in range(min(n, 16))]


def _init_reader():
    _rst_r.value(0); time.sleep_ms(1); _rst_r.value(1); time.sleep_ms(50)
    _rwr(0x01, 0x0F); time.sleep_ms(50)   # SoftReset
    _rwr(0x2A, 0x8D); _rwr(0x2B, 0x3E)    # timer mode + prescaler
    _rwr(0x2C, 30); _rwr(0x2D, 0)          # reload
    _rwr(0x15, 0x40); _rwr(0x11, 0x3D)     # TxASK + ModeReg
    _rset(0x14, 0x03)                       # RF on


def read_uid():
    _rwr(0x0D, 0x07)
    r = _transceive([0x26])   # REQA
    if not r or len(r) < 2:
        return None
    _rwr(0x0D, 0x00)
    r = _transceive([0x93, 0x20])   # AntiColl
    if r and len(r) == 5 and (r[0] ^ r[1] ^ r[2] ^ r[3]) == r[4]:
        return ':'.join('{:02X}'.format(b) for b in r[:4])
    return None


def setup():
    _init_reader()
    print('RC-522 ready   SCK=GP18 MOSI=GP11 MISO=GP16 SDA=GP17 RST=GP15')
    print('Hold a card or tag near the reader...')


def loop():
    uid = read_uid()
    if uid:
        print('Card UID: ' + uid)
        time.sleep_ms(1000)  # debounce
    else:
        time.sleep_ms(100)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
