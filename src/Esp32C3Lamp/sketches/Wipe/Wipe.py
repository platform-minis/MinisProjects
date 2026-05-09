import neopixel
from machine import Pin
import time

# WS2812 ring — color wipe: fill LEDs one by one, then clear one by one
# Wiring: DIN → GP3, VCC → VBUS (5V), GND → GND

NUM_LEDS    = 17
DATA_PIN    = 3
STEP_MS     = 60    # ms per LED step
PAUSE_MS    = 300   # pause when ring is fully lit or fully dark
BRIGHTNESS  = 180

_np = neopixel.NeoPixel(Pin(DATA_PIN), NUM_LEDS)


def setup():
    for i in range(NUM_LEDS):
        _np[i] = (0, 0, 0)
    _np.write()
    print('Wipe started')


def loop():
    # fill clockwise
    for i in range(NUM_LEDS):
        _np[i] = (BRIGHTNESS, 0, 0)
        _np.write()
        time.sleep_ms(STEP_MS)

    time.sleep_ms(PAUSE_MS)

    # clear clockwise
    for i in range(NUM_LEDS):
        _np[i] = (0, 0, 0)
        _np.write()
        time.sleep_ms(STEP_MS)

    time.sleep_ms(PAUSE_MS)


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
