import neopixel
from machine import Pin
import time
import math

# ── Hardware ──────────────────────────────────────────────────────────────────
_RGB_PIN = 21   # WS2812B on-board LED — Waveshare ESP32-S3-Pico = GP21
_BTN_PIN = 16   # External button on GP16, active HIGH; wire: GP16 → button → 3.3 V, 10 kΩ pull-down to GND

_np  = neopixel.NeoPixel(Pin(_RGB_PIN), 1)
_btn = Pin(_BTN_PIN, Pin.IN)   # no internal pull — external pull-down resistor required

# ── Animation registry ────────────────────────────────────────────────────────
_ANIM_COUNT = 5
_anim_idx   = 0
_frame      = 0

_NAMES = ('Rainbow', 'Breathing', 'Heartbeat', 'Color shift', 'Strobe')

# ── Button state machine ──────────────────────────────────────────────────────
_btn_down       = False
_btn_pressed_at = 0
_LONG_MS        = 600   # ms — threshold between short press and long press

# ── Color helper ──────────────────────────────────────────────────────────────
def _hsv(h, s, v):
    """Convert HSV (h 0-360, s/v 0.0-1.0) to (R, G, B) tuple 0-255."""
    i      = int(h / 60) % 6
    f      = (h / 60) - int(h / 60)
    p, q, t = v*(1-s), v*(1-s*f), v*(1-s*(1-f))
    r, g, b = ((v,t,p),(q,v,p),(p,v,t),(p,q,v),(t,p,v),(v,p,q))[i]
    return (int(r*255), int(g*255), int(b*255))

# ── Animation 0 — Rainbow ─────────────────────────────────────────────────────
# Cycles through the full hue wheel (red → yellow → green → cyan → blue → magenta → red).
_rb_hue = 0
def _rainbow():
    global _rb_hue
    _rb_hue = (_rb_hue + 3) % 360
    _np[0] = _hsv(_rb_hue, 1.0, 1.0)
    _np.write()
    time.sleep_ms(20)

# ── Animation 1 — Breathing ───────────────────────────────────────────────────
# Fades the LED in and out using a sine wave — smooth white pulse.
_br_deg = 0
def _breathing():
    global _br_deg
    v = (math.sin(math.radians(_br_deg)) + 1.0) * 0.5
    c = int(v * 230) + 5   # minimum glow avoids fully dark
    _np[0] = (c, c, c)
    _np.write()
    _br_deg = (_br_deg + 3) % 360
    time.sleep_ms(15)

# ── Animation 2 — Heartbeat ───────────────────────────────────────────────────
# Double red pulse followed by a pause — mimics a heartbeat rhythm.
_PULSE = (0, 30, 120, 220, 255, 220, 80, 0, 0, 60, 160, 80, 0, 0, 0, 0)
_hb_i  = 0
def _heartbeat():
    global _hb_i
    v = _PULSE[_hb_i % len(_PULSE)]
    _np[0] = (v, 0, 0)
    _np.write()
    _hb_i += 1
    time.sleep_ms(75)

# ── Animation 3 — Color shift ─────────────────────────────────────────────────
# Smoothly transitions between colors sampled at golden-angle steps so no two
# consecutive colors are visually similar — always visits the whole hue wheel.
_cs_hue    = 0
_cs_target = 137   # golden angle ≈ 137.5°
def _colorshift():
    global _cs_hue, _cs_target
    diff = (_cs_target - _cs_hue) % 360
    if diff > 180:
        diff -= 360
    if abs(diff) <= 2:
        _cs_hue    = _cs_target
        _cs_target = (_cs_target + 137) % 360
    else:
        _cs_hue = (_cs_hue + (2 if diff > 0 else -2)) % 360
    _np[0] = _hsv(_cs_hue, 1.0, 0.9)
    _np.write()
    time.sleep_ms(18)

# ── Animation 4 — Strobe ─────────────────────────────────────────────────────
# Quick white flashes with a variable-length dark gap.
_ST   = (1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0)
_st_i = 0
def _strobe():
    global _st_i
    _np[0] = (255, 255, 255) if _ST[_st_i % len(_ST)] else (0, 0, 0)
    _np.write()
    _st_i += 1
    time.sleep_ms(55)

# ── Dispatch ──────────────────────────────────────────────────────────────────
_ANIMS = (_rainbow, _breathing, _heartbeat, _colorshift, _strobe)

# ── Button polling ────────────────────────────────────────────────────────────
def _poll_btn():
    global _anim_idx, _btn_down, _btn_pressed_at, _frame
    pressed = _btn.value() == 1   # external button is active HIGH
    now     = time.ticks_ms()
    if pressed and not _btn_down:
        _btn_down       = True
        _btn_pressed_at = now
    elif not pressed and _btn_down:
        _btn_down = False
        held      = time.ticks_diff(now, _btn_pressed_at) >= _LONG_MS
        delta     = -1 if held else 1
        _anim_idx = (_anim_idx + delta) % _ANIM_COUNT
        _frame    = 0
        print(('Prev' if held else 'Next') + ':', _NAMES[_anim_idx])

# ── Lifecycle ─────────────────────────────────────────────────────────────────
def setup():
    _np[0] = (0, 0, 0)
    _np.write()
    print('RGB LED Animations — 5 modes')
    print('Short press GP16 = next   |   Long press GP16 = prev')
    print('Active:', _NAMES[_anim_idx])

def loop():
    global _frame
    _poll_btn()
    _ANIMS[_anim_idx]()
    _frame += 1

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        _np[0] = (0, 0, 0)
        _np.write()
        print(e)
