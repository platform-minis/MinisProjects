import neopixel
from machine import Pin
import time
import math

# WS2812 ring — 17 red LEDs, breathing effect
# Wiring:
#   DIN (data) → GP3
#   VCC        → 5 V  (VBUS pin)
#   GND        → GND

NUM_LEDS   = 17
DATA_PIN   = 3
MAX_BRIGHT = 180   # peak brightness (0–255)
MIN_BRIGHT = 4     # valley brightness — not fully off, gives a glowing ember look
STEP_MS    = 20    # ms between brightness steps (lower = faster cycle)

_np = neopixel.NeoPixel(Pin(DATA_PIN), NUM_LEDS)


def _set_brightness(value):
    v = min(255, max(0, int(value)))
    for i in range(NUM_LEDS):
        _np[i] = (v, 0, 0)
    _np.write()


def setup():
    _set_brightness(MIN_BRIGHT)
    print('Breathing started  peak=' + str(MAX_BRIGHT))


def loop():
    # One full sine cycle: 0 → 2π mapped to MIN_BRIGHT → MAX_BRIGHT
    steps = 100
    for step in range(steps):
        angle = 2 * math.pi * step / steps
        # sin goes -1..1; shift and scale to MIN_BRIGHT..MAX_BRIGHT
        t = (math.sin(angle) + 1) / 2   # 0.0 .. 1.0
        brightness = MIN_BRIGHT + t * (MAX_BRIGHT - MIN_BRIGHT)
        _set_brightness(brightness)
        time.sleep_ms(STEP_MS)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        _set_brightness(0)
        print(e)
