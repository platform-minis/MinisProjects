import neopixel
from machine import Pin
import time
import random

# WS2812 ring — twinkle: each LED decays every frame; random LEDs flash to full
# Wiring: DIN → GP3, VCC → VBUS (5V), GND → GND

NUM_LEDS   = 17
DATA_PIN   = 3
SPEED_MS   = 40   # ms per frame
DECAY      = 30   # brightness subtracted each frame
SPAWN_PROB = 4    # ~1-in-N chance a given LED flashes each frame

_np   = neopixel.NeoPixel(Pin(DATA_PIN), NUM_LEDS)
_bright = [0] * NUM_LEDS


def _draw():
    for i in range(NUM_LEDS):
        if random.randint(0, SPAWN_PROB - 1) == 0:
            _bright[i] = 255
        else:
            _bright[i] = max(0, _bright[i] - DECAY)
        _np[i] = (_bright[i], 0, 0)
    _np.write()


def setup():
    for i in range(NUM_LEDS):
        _bright[i] = 0
        _np[i] = (0, 0, 0)
    _np.write()
    print('Twinkle started')


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
