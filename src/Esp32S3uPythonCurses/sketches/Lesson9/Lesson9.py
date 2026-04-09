import os, sys, io
from machine import Pin, PWM
import time

# Passive buzzer on GP18 via PWM
# duty=0 on init → silent until loop starts
_pwm_18 = PWM(Pin(18), freq=262, duty=0)

def setup():
    pass

def loop():
    # C4 = 262 Hz
    _pwm_18.freq(262)
    _pwm_18.duty(512)
    time.sleep_ms(200)
    # E4 = 330 Hz
    _pwm_18.freq(330)
    time.sleep_ms(200)
    # G4 = 392 Hz
    _pwm_18.freq(392)
    time.sleep_ms(200)
    # Silence
    _pwm_18.duty(0)
    time.sleep_ms(500)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        _pwm_18.duty(0)
        print(e)
