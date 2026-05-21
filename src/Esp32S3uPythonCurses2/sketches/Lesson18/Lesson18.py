print('Lesson18: loading...')
from machine import SPI, Pin
import time

print('Lesson18: init SPI...')
try:
    _spi_r = SPI(1, baudrate=1000000, polarity=0, phase=0,
                 sck=Pin(18), mosi=Pin(11), miso=Pin(16))
    _cs_r = Pin(17, Pin.OUT, value=1)
    _rst_r = Pin(15, Pin.OUT, value=1)
    print('Lesson18: SPI OK')
except Exception as e:
    print('SPI init error:', e)
    raise


def _rrd(a):
    _cs_r.value(0)
    _spi_r.write(bytes([0xff & (((a << 1) & 0x7e) | 0x80)]))
    val = _spi_r.read(1)
    _cs_r.value(1)
    return val[0]


def _rwr(a, v):
    _cs_r.value(0)
    _spi_r.write(bytes([0xff & ((a << 1) & 0x7e)]))
    _spi_r.write(bytes([0xff & v]))
    _cs_r.value(1)


def _rset(a, m):
    _rwr(a, _rrd(a) | m)


def _rclr(a, m):
    _rwr(a, _rrd(a) & (~m))


def _tocard(cmd, data):
    recv = []
    bits = 0
    irq_en = 0x77
    wait_irq = 0x30

    _rwr(0x02, irq_en | 0x80)
    _rclr(0x04, 0x80)
    _rset(0x0A, 0x80)
    _rwr(0x01, 0x00)

    for c in data:
        _rwr(0x09, c)
    _rwr(0x01, cmd)

    if cmd == 0x0C:
        _rset(0x0D, 0x80)

    i = 2000
    while True:
        n = _rrd(0x04)
        i -= 1
        if ~((i != 0) and ~(n & 0x01) and ~(n & wait_irq)):
            break

    _rclr(0x0D, 0x80)

    stat = 2
    if i:
        if (_rrd(0x06) & 0x1B) == 0x00:
            stat = 0
            if n & irq_en & 0x01:
                stat = 1
            elif cmd == 0x0C:
                n = _rrd(0x0A)
                lbits = _rrd(0x0C) & 0x07
                if lbits != 0:
                    bits = (n - 1) * 8 + lbits
                else:
                    bits = n * 8
                if n == 0:
                    n = 1
                elif n > 16:
                    n = 16
                for _ in range(n):
                    recv.append(_rrd(0x09))
        else:
            stat = 2

    return stat, recv, bits


def _init_reader():
    _rst_r.value(0)
    time.sleep_ms(50)
    _rst_r.value(1)
    time.sleep_ms(50)
    _rwr(0x01, 0x0F)
    time.sleep_ms(50)
    _rwr(0x2A, 0x8D)
    _rwr(0x2B, 0x3E)
    _rwr(0x2D, 30)
    _rwr(0x2C, 0)
    _rwr(0x15, 0x40)
    _rwr(0x11, 0x3D)
    _rset(0x14, 0x03)
    ver = _rrd(0x37)
    print('RC-522 version: 0x{:02X}  (expect 0x91 or 0x92, 0x00/0xFF = no SPI)'.format(ver))


def read_uid():
    _rwr(0x0D, 0x07)
    stat, recv, bits = _tocard(0x0C, [0x26])
    if stat != 0 or bits != 0x10:
        return None
    _rwr(0x0D, 0x00)
    stat, recv, bits = _tocard(0x0C, [0x93, 0x20])
    if stat == 0 and len(recv) == 5:
        chk = 0
        for b in recv[:4]:
            chk ^= b
        if chk == recv[4]:
            return ':'.join('{:02X}'.format(b) for b in recv[:4])
    return None


print('Lesson18: starting reader...')
try:
    _init_reader()
    print('Hold a card near the reader...')
    while True:
        uid = read_uid()
        if uid:
            print('Card UID: ' + uid)
            time.sleep_ms(1000)
        else:
            time.sleep_ms(100)
except KeyboardInterrupt:
    print('Stopped.')
except Exception as e:
    print('Error:', type(e).__name__, str(e))
