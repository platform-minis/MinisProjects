from machine import Pin, I2S
import math, struct, time
_RATE = 22050
_i2s = None
def _play(freq, ms, vol=0.5):
    global _i2s
    if _i2s is None:
        return
    if freq <= 0:
        _i2s.write(bytearray(int(_RATE * ms / 1000) * 2))
        return
    period = int(_RATE / freq)
    one = bytearray(period * 2)
    amp = int(32767 * vol)
    for i in range(period):
        struct.pack_into('<h', one, i * 2, int(amp * math.sin(6.2832 * i / period)))
    total = int(_RATE * ms / 1000)
    written = 0
    while written < total:
        chunk = min(period, total - written)
        _i2s.write(one[:chunk * 2])
        written += chunk
def setup():
    global _i2s
    _i2s = I2S(0, sck=Pin(4), ws=Pin(5), sd=Pin(6), mode=I2S.TX, bits=16, format=I2S.MONO, rate=_RATE, ibuf=4096)
    print('MAX98357A ready  BCLK=GP4 LRCLK=GP5 DIN=GP6')
def loop():
    for _ in range(3):
        _play(440, 500)
        _play(0, 200)
    time.sleep_ms(2000)
if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
