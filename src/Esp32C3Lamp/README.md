# ESP32-C3 Lamp — WS2812 Ring (17 × Red)

A MicroPython lamp project for the **ESP32-C3 Super Mini** board. Drives a WS2812 LED ring with 17 addressable RGB LEDs configured as a red lamp. Two sketches are included: a static solid-red light and a smooth breathing effect.

---

## Hardware

| Part | Quantity |
| ---- | -------- |
| ESP32-C3 Super Mini | 1 |
| WS2812 ring (17 LEDs) | 1 |
| Jumper wires | 3 |

---

## Microcontroller Pinout

![ESP32-C3 Super Mini pinout](https://raw.githubusercontent.com/platform-minis/MinisProjects/refs/heads/main/docs/Esp32C3SuperMini/ESP32-C3-Super-Mini-pinout-low.jpg.webp "ESP32-C3 Super Mini Pinout")

---

## Pin Overview

| Pin | Mode | Component |
| --- | ---- | --------- |
| GP3 | Digital output | WS2812 DIN (data) |
| 5V (VBUS) | Power | WS2812 VCC |
| GND | Ground | WS2812 GND |

---

## Wiring

```text
ESP32-C3 Super Mini      WS2812 ring (17 LEDs)
┌──────────────────┐     ┌──────────────────┐
│             GP3  ├─────┤ DIN  (data in)   │
│            VBUS  ├─────┤ VCC  (5 V)       │
│             GND  ├─────┤ GND              │
└──────────────────┘     └──────────────────┘
```

> **VCC at 5 V:** WS2812 LEDs operate reliably at 5 V. Use the **VBUS** pin (USB 5 V passthrough) on the ESP32-C3 Super Mini. The data signal at 3.3 V from GP3 is accepted by the WS2812 — no level shifter is needed for short cable runs (< 30 cm).

> **Current draw:** Each WS2812 LED can draw up to 60 mA at full white brightness. At brightness 80 red-only the ring draws ≈ 17 × 80/255 × 20 mA ≈ **107 mA** — well within USB 500 mA budget. At full red (255) it draws ≈ **340 mA**; keep this in mind if powering from a low-current source.

**Wiring order:**

```text
Step 1: GND  (ESP32-C3)  →  GND  (WS2812)
Step 2: VBUS (ESP32-C3)  →  VCC  (WS2812)
Step 3: GP3  (ESP32-C3)  →  DIN  (WS2812)
```

> **⚠ DIN vs DOUT:** The WS2812 ring has two data pads — **DIN** (input, connect here) and **DOUT** (output, for daisy-chaining a second ring). Always connect to DIN.

---

## How WS2812 Works

Each WS2812 LED contains a built-in driver IC. Data is sent as a serial stream of 24-bit colour packets (8-bit Green, 8-bit Red, 8-bit Blue — GRB order internally, but MicroPython's `neopixel` module accepts standard RGB tuples and handles the byte reordering automatically).

```text
Data stream:  [LED 0: G R B] [LED 1: G R B] … [LED 16: G R B]  →  RESET (≥ 50 µs LOW)
```

MicroPython uses the **RMT** peripheral (or bit-banging fallback) to generate the precise 800 kHz signal. No external library is needed — `neopixel` is built into the ESP32-C3 MicroPython firmware.

---

## Sketches

### Sketch 1 — SolidRed

**Goal:** Turn on all 17 LEDs at a fixed red brightness.

**What happens:** `setup()` fills all LEDs with `(80, 0, 0)` and calls `write()`. The `loop()` sleeps — the lamp stays on until interrupted with Ctrl+C, which turns the ring off cleanly.

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════╗
║  [WS2812 fill red  brightness 80]       ║
║  [Print]  "Lamp ON"                     ║
╚═════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════╗
║  [Sleep]  1000 ms                       ║
╚═════════════════════════════════════════╝
```

**MicroPython code:**

```python
import neopixel
from machine import Pin
import time

NUM_LEDS   = 17
DATA_PIN   = 3
BRIGHTNESS = 80

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
```

**What you learn:**

- Initialising the `neopixel.NeoPixel` object with a pin and LED count
- Setting individual pixel colours as `(R, G, B)` tuples
- Calling `np.write()` to push the buffer to the ring
- Clean shutdown in the `except` block

---

### Sketch 2 — Breathing

**Goal:** Create a smooth pulsing (breathing) effect using a sine wave.

**What happens:** `setup()` sets a dim starting brightness. `loop()` runs 100 steps of a full sine cycle. At each step the sine value (−1 … +1) is mapped to the range `MIN_BRIGHT … MAX_BRIGHT`, set on all LEDs, and a 20 ms delay gives a 2-second period per breath.

**Cycle timing:**

```text
100 steps × 20 ms = 2 000 ms = 2 s per breath
```

**Brightness envelope:**

```text
Brightness
  180 │        ╭────╮
      │      ╭╯    ╰╮
   80 │    ──╯        ╰──
    4 │ ──╯                ╰──
      └─────────────────────────→ time (2 s)
```

**Blockly blocks:**

```text
╔══ ▶ START ════════════════════════════════╗
║  [WS2812 fill red  brightness 4]          ║
║  [Print]  "Breathing started"             ║
╚═══════════════════════════════════════════╝

╔══ 🔁 FOREVER ════════════════════════════════════════════════════════╗
║  for step = 0 to 99 by 1                                             ║
║    set t = (sin(2 × π × step / 100) + 1) / 2                        ║
║    set brightness = 4 + t × 176                                      ║
║    [WS2812 fill red  brightness]                                      ║
║    [Sleep]  20 ms                                                     ║
╚══════════════════════════════════════════════════════════════════════╝
```

**MicroPython code:**

```python
import neopixel
from machine import Pin
import time, math

NUM_LEDS   = 17
DATA_PIN   = 3
MAX_BRIGHT = 180
MIN_BRIGHT = 4
STEP_MS    = 20

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
    for step in range(100):
        t = (math.sin(2 * math.pi * step / 100) + 1) / 2
        _set_brightness(MIN_BRIGHT + t * (MAX_BRIGHT - MIN_BRIGHT))
        time.sleep_ms(STEP_MS)
```

**What you learn:**

- Using `math.sin()` to generate a smooth periodic signal
- Mapping a normalised 0–1 value to a brightness range
- Why `MIN_BRIGHT = 4` instead of 0 — keeps the ring glowing like an ember at the bottom of each cycle
- How to tune animation speed by adjusting `STEP_MS` and the number of steps

---

## WS2812 Block Reference

| Block | Type | Description |
| ----- | ---- | ----------- |
| `WS2812 fill R G B` | Statement | Fill all 17 LEDs with RGB colour and write immediately |
| `WS2812 fill red brightness` | Statement | Fill all LEDs with red at the given brightness (0–255) and write |
| `WS2812 clear (all off)` | Statement | Turn off all 17 LEDs |
| `WS2812 set pixel i R G B` | Statement | Set one pixel (0–16); call **WS2812 write** afterwards |
| `WS2812 write` | Statement | Push the pixel buffer to the hardware |
| `WS2812 LED count` | Expression | Returns 17 |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
| ------- | ------------ | --- |
| No LEDs light up | DIN and DOUT swapped | Connect GP3 to **DIN** (input side of ring), not DOUT |
| First LED works, rest don't | Broken LED in ring | WS2812 LEDs are chained — one dead LED breaks the chain downstream |
| Colours look wrong | GRB vs RGB confusion | MicroPython `neopixel` accepts RGB — `(255, 0, 0)` is red |
| Flickering | Loose DIN connection | Reseat jumper; keep DIN wire short (< 30 cm) |
| LEDs very dim | Using 3.3 V for VCC | Use VBUS (5 V) for VCC |
| Upload hangs | Board in running program | Press Ctrl+C in terminal first, then upload |

---

## Platform

- **Microcontroller:** ESP32-C3 Super Mini
- **Language:** MicroPython
- **Environment:** MyCastle / Thonny / mpremote
- **Logic voltage:** 3.3 V
- **LED supply:** 5 V (VBUS)
