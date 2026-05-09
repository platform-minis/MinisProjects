import neopixel
from machine import Pin
import time

# WS2812 ring — comet with a long exponentially-decaying tail
# Wiring: DIN → GP3, VCC → VBUS (5V), GND → GND

NUM_LEDS    = 17
DATA_PIN    = 3
SPEED_MS    = 50   # ms per frame
TAIL_LENGTH = 8    # pixels behind the head

_np = neopixel.NeoPixel(Pin(DATA_PIN), NUM_LEDS)


def _draw(head):
    for i in range(NUM_LEDS):
        dist = (head - i) % NUM_LEDS
        if dist == 0:
            b = 255
        elif dist < TAIL_LENGTH:
            b = int(220 * (1 - dist / TAIL_LENGTH) ** 2)
        else:
            b = 0
        _np[i] = (b, 0, 0)
    _np.write()


def setup():
    for i in range(NUM_LEDS):
        _np[i] = (0, 0, 0)
    _np.write()
    print('Comet started')


_head = 0

def loop():
    global _head
    _draw(_head)
    _head = (_head + 1) % NUM_LEDS
    time.sleep_ms(SPEED_MS)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        for i in range(NUM_LEDS):
            _np[i] = (0, 0, 0)
        _np.write()
        print(e)
