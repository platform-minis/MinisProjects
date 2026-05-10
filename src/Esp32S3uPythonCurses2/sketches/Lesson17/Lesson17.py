import os, sys, io
from machine import Pin, ADC
import time

# TCRT5000 IR reflective sensor
#
# Wiring:
#   VCC → 3.3 V
#   GND → GND
#   D0  → GP2   (digital out — LOW (0) when object detected)
#   A0  → GP10  (analog out — lower value = closer / more reflection)
#
# Adjust the onboard potentiometer to set the digital detection threshold.

_tcrt_d = Pin(2, Pin.IN)
_tcrt_a = ADC(Pin(10))
_tcrt_a.atten(ADC.ATTN_11DB)   # 0–3.3 V range → 0–4095


def setup():
    print('TCRT5000 ready   D0=GP2  A0=GP10')


def loop():
    detected = not _tcrt_d.value()   # D0 LOW → object detected
    adc_val = _tcrt_a.read()
    if detected:
        print('Object detected   ADC=' + str(adc_val))
    else:
        print('--   ADC=' + str(adc_val))
    time.sleep_ms(300)


if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
