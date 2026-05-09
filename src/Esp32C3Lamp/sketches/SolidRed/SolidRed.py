import neopixel
from machine import Pin
import time

# WS2812 ring — 17 red LEDs
# Wiring:
#   DIN (data) → GP3
#   VCC        → 5 V  (VBUS pin)
#   GND        → GND

NUM_LEDS  = 17
DATA_PIN  = 3
BRIGHTNESS = 80   # 0–255; 80 ≈ 31 % — comfortable indoor brightness

_np = neopixel.NeoPixel(Pin(DATA_PIN), NUM_LEDS)


def lamp_on(brightness=BRIGHTNESS):
    for i in range(NUM_LEDS):
        _np[i] = (brightness, 0, 0)
    _np.write()


def lamp_off():
    for i in range(NUM_LEDS):
        _np[i] = (0, 0, 0)
    _np.write()


def setup():
    lamp_on()
    print('Lamp ON  brightness=' + str(BRIGHTNESS))


def loop():
    time.sleep_ms(1000)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        lamp_off()
        print(e)
