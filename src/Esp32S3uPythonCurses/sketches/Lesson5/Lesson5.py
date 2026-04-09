import os, sys, io
from machine import Pin, time_pulse_us
import time

# VS1838B IR receiver — data pin (idle HIGH, active LOW)
_ir = Pin(19, Pin.IN)

# LED on GP11 — blinks when a button is received
_led = Pin(11, Pin.OUT)

# NEC remote button map: cmd byte → button label
# Compatible with common 21-key mini IR remote (address 0x00)
_IR_KEYS = {
    0x45: 'CH-',  0x46: 'CH',   0x47: 'CH+',
    0x44: 'PREV', 0x40: 'NEXT', 0x43: 'PLAY',
    0x07: 'VOL-', 0x15: 'VOL+', 0x09: 'EQ',
    0x16: '0',    0x19: '100+', 0x0D: '200+',
    0x0C: '1',    0x18: '2',    0x5E: '3',
    0x08: '4',    0x1C: '5',    0x5A: '6',
    0x42: '7',    0x52: '8',    0x4A: '9',
}

def _recv_nec():
    """Decode one NEC IR frame. Returns (addr, cmd) or None on error/timeout."""
    # Start burst: ~9 ms LOW
    t = time_pulse_us(_ir, 0, 14000)
    if not 7500 < t < 10500:
        return None
    # Start space: ~4.5 ms HIGH
    t = time_pulse_us(_ir, 1, 6000)
    if not 3500 < t < 5500:
        return None
    # 32 data bits (LSB first)
    bits = 0
    for i in range(32):
        t = time_pulse_us(_ir, 0, 2000)   # ~562 µs LOW mark
        if not 200 < t < 900:
            return None
        t = time_pulse_us(_ir, 1, 2500)   # space: 562 µs = bit 0, 1687 µs = bit 1
        if t < 200:
            return None
        bits |= (1 if t > 1000 else 0) << i
    # NEC frame layout: addr | ~addr | cmd | ~cmd
    addr = bits & 0xFF
    cmd  = (bits >> 16) & 0xFF
    return addr, cmd

def setup():
    _led.value(0)
    print('IR receiver ready — aim remote at VS1838B and press a button')

def loop():
    result = _recv_nec()
    if result:
        addr, cmd = result
        key = _IR_KEYS.get(cmd, f'0x{cmd:02X}')
        _led.value(1)
        print(key)
        time.sleep_ms(80)
        _led.value(0)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        _led.value(0)
        print(e)
