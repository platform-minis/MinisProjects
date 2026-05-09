import neopixel
from machine import Pin
import time

# WS2812 ring — single red LED chasing around the ring with a fading trail
# Wiring: DIN → GP3, VCC → VBUS (5V), GND → GND

NUM_LEDS = 17
DATA_PIN  = 3
SPEED_MS  = 60   # ms per frame — lower = faster

# Trail brightness levels (head → tail)
_TRAIL = [255, 90, 25, 6]

_np = neopixel.NeoPixel(Pin(DATA_PIN), NUM_LEDS)


def _draw(head):
    for i in range(NUM_LEDS):
        dist = (head - i) % NUM_LEDS
        b = _TRAIL[dist] if dist < len(_TRAIL) else 0
        _np[i] = (b, 0, 0)
    _np.write()


def setup():
    for i in range(NUM_LEDS):
        _np[i] = (0, 0, 0)
    _np.write()
    print('Chase started')


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
