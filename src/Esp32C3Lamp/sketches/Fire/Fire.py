import neopixel
from machine import Pin
import time
import random

# WS2812 ring — fire effect: random red/orange flicker per LED
# Wiring: DIN → GP3, VCC → VBUS (5V), GND → GND

NUM_LEDS = 17
DATA_PIN  = 3
SPEED_MS  = 60   # ms per frame

_np = neopixel.NeoPixel(Pin(DATA_PIN), NUM_LEDS)


def _draw():
    for i in range(NUM_LEDS):
        r = random.randint(120, 255)
        g = random.randint(0, 30)
        _np[i] = (r, g, 0)
    _np.write()


def setup():
    for i in range(NUM_LEDS):
        _np[i] = (0, 0, 0)
    _np.write()
    print('Fire started')


def loop():
    _draw()
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
