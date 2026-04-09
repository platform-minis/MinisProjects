# MicroPython Course — ESP32-S3 Pico

A hands-on programming course for the **ESP32-S3** microcontroller using **MicroPython**. Each lesson is a separate sketch — a program uploaded directly to the board. Lessons are ordered from simplest to more advanced, gradually introducing new electronic components and programming techniques.

---

## Prerequisites

- **ESP32-S3 Pico** board with MicroPython firmware flashed
- **MyCastle** environment (or any REPL client e.g. Thonny / mpremote)
- Basic knowledge of Python (variables, loops, conditionals)

---

## Glossary

| Term | Description |
| ---- | ----------- |
| **GPIO** | General Purpose Input/Output — a microcontroller pin that can act as a digital input or output |
| **Pin.OUT** | Output mode — the microcontroller drives the voltage (0 V or 3.3 V) |
| **Pin.IN** | Input mode — the microcontroller reads the state of an external signal |
| **ADC** | Analog-to-Digital Converter — converts a voltage (0–3.3 V) into an integer value |
| **ATTN_11DB** | ADC attenuation setting that allows measuring voltages up to ~3.9 V instead of the default ~1.1 V |
| **LED** | Light Emitting Diode — emits light when current flows through it |
| **Current-limiting resistor** | A resistor in series with an LED that prevents it from burning out — typically 220–330 Ω |
| **Tactile button** | A mechanical switch that closes a circuit when pressed |
| **Pull-down resistor** | A resistor (usually 10 kΩ) connecting a pin to GND, ensuring a LOW state when the button is released |
| **LDR (photoresistor)** | Light Dependent Resistor — resistance decreases as light increases; darker = higher resistance |
| **Voltage divider** | Two resistors in series between power and GND; the voltage at their junction is proportional to the ratio of their values |
| **setup()** | Initialization function called once at program start |
| **loop()** | Main loop function called repeatedly |
| **sleep_ms(n)** | Pauses program execution for *n* milliseconds |
| **REPL** | Read-Eval-Print Loop — the interactive MicroPython console |

---

## Pin Overview

| Pin | Mode | Component | Lessons |
| --- | ---- | --------- | ------- |
| 4 | Digital output | ULN2003 IN1 (stepper motor) | Lesson 8 |
| 5 | Digital output | ULN2003 IN2 (stepper motor) | Lesson 8 |
| 6 | Digital output | ULN2003 IN3 (stepper motor) | Lesson 8 |
| 7 | ADC input | Photoresistor (LDR) | Lesson 6 |
| 11 | Digital output | LED | Lesson 1, 2, 4 |
| 12 | Digital output | LED (red) | Lesson 3 |
| 13 | Digital output | LED (yellow) | Lesson 3 |
| 14 | Digital output | LED (green) | Lesson 3 |
| 16 | Digital input | Tactile button | Lesson 4 |
| 17 | Digital output | ULN2003 IN4 (stepper motor) | Lesson 8 |

---

## Wiring Diagrams

### LED connection (Lessons 1, 2, 4)

```text
ESP32-S3 Pico
┌──────────────┐
│         GND  ├──────────────────────────────────┐
│              │                                  │
│        GP11  ├──[ R 330Ω ]──┤▶├──(cathode)    GND
│              │               LED    (anode → R)
└──────────────┘
```

> **Important:** Always connect an LED through a current-limiting resistor (220–330 Ω). Without it, the current may exceed the safe limit and damage both the LED and the GPIO pin.

Practical view:

```text
GP11 ──── 330Ω resistor ──── LED anode (+)
                                   │
                              LED cathode (−)
                                   │
GND ───────────────────────────────┘
```

---

### Three LEDs connection (Lesson 3)

```text
GP14 ── R 330Ω ──┤▶├── GND    (green)
GP13 ── R 330Ω ──┤▶├── GND    (yellow)
GP12 ── R 330Ω ──┤▶├── GND    (red)
```

Timing diagram:

```text
Green  (GP14) ─── 3 s ON ─── OFF ───────────────────────────▶
Yellow (GP13) ───────────── 1 s ON ─── OFF ─────────────────▶
Red    (GP12) ──────────────────────── 3 s ON ─── OFF ──────▶
              0s              3s         4s          7s   ...
```

---

### Button connection (Lesson 4)

```text
3.3 V ──── Button ──── GP16
                  │
              R 10kΩ (pull-down)
                  │
                 GND
```

Button **released**: GP16 pulled to GND through resistor → LOW (0)
Button **pressed**: GP16 connected to 3.3 V → HIGH (1)

```text
               ┌─────────┐
3V3 ──[BTN]──┤  GP16   │  ESP32-S3
              │         │
             [10kΩ]     │
              │         │
GND ──────────┘         │
                        │
                   read .value()
```

---

### Photoresistor — voltage divider (Lesson 6)

```text
3.3 V ──── LDR (photoresistor) ──┬──── GP7 (ADC)
                                 │
                             R 10kΩ (fixed)
                                 │
                                GND
```

How it works: the LDR and fixed resistor form a voltage divider. In the **dark**, LDR resistance rises → voltage at GP7 drops → low ADC reading. In **bright light**, LDR resistance falls → voltage at GP7 rises → high ADC reading.

```text
Light level:  │░░░░░░░░░│▒▒▒▒▒▒▒▒▒│█████████│
ADC reading:  0       1000       2500      4095
Category:    DARK     NORMAL      BRIGHT
```

---

## Lessons

Each lesson is shown in two forms: **Blockly blocks** (visual editor) and **MicroPython code** (auto-generated).

---

### Lesson 1 — Turn on an LED

**Goal:** Understand basic pin output configuration and setting a HIGH state once at startup.

**What happens:**
The program configures pin 11 as an output, then in `setup()` sets it HIGH (3.3 V). The LED turns on and stays on for the entire runtime. The `loop()` function is empty — nothing changes after initialization.

**Wiring:** see *LED connection* section, pin GP11.

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════╗
║  [Pin Init]  pin=11  mode=OUT           ║
║  [Pin Set]   pin=11  → 1               ║
║  [Print]     "Led is On"               ║
╚═════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════╗
║  (empty)                                ║
╚═════════════════════════════════════════╝
```

**MicroPython code:**

```python
from machine import Pin

_pin_11 = Pin(11, mode=Pin.OUT)

def setup():
    _pin_11.value(1)
    print('Led is On')

def loop():
    pass
```

**What you learn:**

- Importing the `machine` module
- Creating a `Pin` object in `Pin.OUT` mode
- The `.value(1)` method — setting a HIGH state
- The `setup()` / `loop()` structure

---

### Lesson 2 — Blinking LED

**Goal:** Introduce time delays and cyclic pin state changes.

**What happens:**
The LED on pin 11 turns on for 1 second, then off for 1 second — repeating indefinitely. This produces the classic blinking effect.

**Timing diagram:**

```text
GP11:  ___________         ___________         _____
      |           |       |           |       |
      |   1000ms  |       |   1000ms  |       |
______|           |_______|           |_______|
      ↑ ON        ↑ OFF   ↑ ON        ↑ OFF
```

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════╗
║  [Pin Init]  pin=11  mode=OUT           ║
╚═════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════╗
║  [Pin Set]   pin=11  → 1               ║
║  [Sleep]     1000 ms                   ║
║  [Pin Set]   pin=11  → 0               ║
║  [Sleep]     1000 ms                   ║
╚═════════════════════════════════════════╝
```

**MicroPython code:**

```python
from machine import Pin
import time

_pin_11 = Pin(11, mode=Pin.OUT)

def setup():
    pass

def loop():
    _pin_11.value(1)
    time.sleep_ms(1000)
    _pin_11.value(0)
    time.sleep_ms(1000)
```

**What you learn:**

- The `time` module and `sleep_ms()` function
- Running code repeatedly in the `loop()` function
- Toggling a pin between 0 and 1

---

### Lesson 3 — Traffic light with three LEDs

**Goal:** Control multiple output pins, sequence with different timings, visually simulate a traffic light.

**What happens:**
Three LEDs turn on one after another in a fixed order:

1. Green (GP14) on for 3 seconds
2. Yellow (GP13) on for 1 second
3. Red (GP12) on for 3 seconds
4. Cycle repeats from step 1

**Wiring:** see *Three LEDs connection* section.

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════╗
║  [Pin Init]  pin=12  mode=OUT           ║
║  [Pin Init]  pin=13  mode=OUT           ║
║  [Pin Init]  pin=14  mode=OUT           ║
╚═════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════╗
║  [Pin Set]   pin=14  → 1   (green)     ║
║  [Sleep]     3000 ms                   ║
║  [Pin Set]   pin=14  → 0               ║
║  [Pin Set]   pin=13  → 1   (yellow)    ║
║  [Sleep]     1000 ms                   ║
║  [Pin Set]   pin=13  → 0               ║
║  [Pin Set]   pin=12  → 1   (red)       ║
║  [Sleep]     3000 ms                   ║
║  [Pin Set]   pin=12  → 0               ║
╚═════════════════════════════════════════╝
```

**MicroPython code:**

```python
from machine import Pin
import time

_pin_12 = Pin(12, mode=Pin.OUT)
_pin_13 = Pin(13, mode=Pin.OUT)
_pin_14 = Pin(14, mode=Pin.OUT)

def setup():
    pass

def loop():
    _pin_14.value(1)
    time.sleep_ms(3000)
    _pin_14.value(0)
    _pin_13.value(1)
    time.sleep_ms(1000)
    _pin_13.value(0)
    _pin_12.value(1)
    time.sleep_ms(3000)
    _pin_12.value(0)
```

**What you learn:**

- Declaring multiple output pins
- Sequentially controlling multiple components
- Timing design inside a loop

---

### Lesson 4 — Button controlling an LED

**Goal:** Read a digital input state and react to user interaction.

**What happens:**
Every 100 ms the program checks the state of pin 16 (button). If the pin is HIGH (button pressed) — the LED on pin 11 turns on and `Button pressed` is printed to the REPL console. If the button is released — the LED turns off.

**Logic diagram:**

```text
GP16 state:  0  0  0  1  1  1  1  0  0  1  0
GP11 state:  0  0  0  1  1  1  1  0  0  1  0
Console:               ↑ "Button pressed" × 4    ↑
```

**Wiring:** see *Button connection* section, plus LED on GP11.

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════╗
║  [Pin Init]  pin=11  mode=OUT           ║
║  [Pin Init]  pin=16  mode=IN            ║
╚═════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════════════════╗
║  ╔══ If  [Pin Get pin=16] == 1 ═══════════════════╗  ║
║  ║  [Pin Set]   pin=11  → 1                       ║  ║
║  ║  [Print]     "Button pressed"                  ║  ║
║  ╠══ Else ══════════════════════════════════════════╣  ║
║  ║  [Pin Set]   pin=11  → 0                       ║  ║
║  ╚═════════════════════════════════════════════════╝  ║
║  [Sleep]     100 ms                                   ║
╚══════════════════════════════════════════════════════╝
```

**MicroPython code:**

```python
from machine import Pin
import time

_pin_11 = Pin(11, mode=Pin.OUT)
_pin_16 = Pin(16, mode=Pin.IN)

def setup():
    pass

def loop():
    if _pin_16.value() == 1:
        _pin_11.value(1)
        print('Button pressed')
    else:
        _pin_11.value(0)
    time.sleep_ms(100)
```

**What you learn:**

- Creating a pin in `Pin.IN` mode
- The `.value()` method for reading state
- `if/else` conditional inside a loop
- 100 ms delay as a simple debounce

---

### Lesson 6 — Light level measurement (ADC)

**Goal:** Read an analog signal, classify the value, and print results to the console.

**What happens:**
Every 500 ms the program reads a value from the ADC on pin 7 (photoresistor). The result is an integer in the range 0–4095. Based on the value, one of three categories is printed:

| ADC reading | Category |
| ----------- | -------- |
| < 1000 | DARK |
| 1000 – 2499 | NORMAL |
| ≥ 2500 | BRIGHT |

**Example REPL output:**

```text
Light level2341
NORMAL
Light level891
DARK
Light level3102
BRIGHT
```

**Wiring:** see *Photoresistor* section.

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════╗
║  [ADC Init]  pin=7  attenuation=11dB   ║
╚═════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════════════════╗
║  [Set]    Light = [ADC Read pin=7]                  ║
║  [Print]  "Light level" + Light                     ║
║  ╔══ If  Light < 1000 ════════════════════════════╗  ║
║  ║  [Print]  "DARK"                               ║  ║
║  ╠══ Else if  Light < 2500 ═══════════════════════╣  ║
║  ║  [Print]  "NORMAL"                             ║  ║
║  ╠══ Else ══════════════════════════════════════════╣  ║
║  ║  [Print]  "BRIGHT"                             ║  ║
║  ╚═════════════════════════════════════════════════╝  ║
║  [Sleep]  500 ms                                      ║
╚══════════════════════════════════════════════════════╝
```

**MicroPython code:**

```python
from machine import Pin, ADC
import time

_adc_7 = ADC(Pin(7), atten=ADC.ATTN_11DB)
Light = None

def setup():
    pass

def loop():
    global Light
    Light = _adc_7.read()
    print('Light level' + str(Light))
    if Light < 1000:
        print('DARK')
    elif Light < 2500:
        print('NORMAL')
    else:
        print('BRIGHT')
    time.sleep_ms(500)
```

**What you learn:**

- The `ADC` class and the `atten` parameter
- The difference between digital and analog signals
- Using global variables (`global`)
- Classifying continuous values with thresholds (`elif`)

---

### Lesson 8 — Stepper motor (fan)

**Goal:** Drive a 28BYJ-48 stepper motor through a ULN2003 driver — continuous rotation simulating a fan blade.

**What happens:**
The program sends successive voltage combinations to 4 pins following the full-step sequence. Each of the 4 excitation patterns rotates the motor by one step — repeated without interruption this produces continuous shaft rotation. A 3 ms delay between steps sets the speed (~83 RPM with the 28BYJ-48 64:1 gear ratio).

**Components used:**

- **28BYJ-48** stepper motor (5 V, unipolar)
- **ULN2003** driver board (4 transistors + motor connector)

**Wiring:**

```text
ESP32-S3 Pico          ULN2003            28BYJ-48
┌────────────┐       ┌──────────┐        ┌────────┐
│       GP4  ├──────►│ IN1      │        │        │
│       GP5  ├──────►│ IN2      ├───────►│ coils  │
│       GP6  ├──────►│ IN3      │        │        │
│      GP17  ├──────►│ IN4      │        └────────┘
│            │       │          │
│       GND  ├──────►│ GND      │
└────────────┘       │ 5V  ◄────┼── 5V (USB or external)
                     └──────────┘
```

> **Important:** The 28BYJ-48 requires **5 V** power — connect the `5V` pin of the ESP32-S3 board (directly from USB) to the `5V` input of the ULN2003, not `3V3`. The 3.3 V logic signals on GP4–GP17 are fully compatible with ULN2003 inputs.

**Full-step sequence (4 steps):**

```text
Step │ IN1(GP4) │ IN2(GP5) │ IN3(GP6) │ IN4(GP17)
─────┼──────────┼──────────┼──────────┼──────────
  1  │    1     │    0     │    1     │    0
  2  │    0     │    1     │    1     │    0
  3  │    0     │    1     │    0     │    1
  4  │    1     │    0     │    0     │    1
```

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════╗
║  [Pin Init]  pin=4   mode=OUT           ║
║  [Pin Init]  pin=5   mode=OUT           ║
║  [Pin Init]  pin=6   mode=OUT           ║
║  [Pin Init]  pin=17  mode=OUT           ║
╚═════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════╗
║  -- step 1 --                           ║
║  [Pin Set]   pin=4   → 1               ║
║  [Pin Set]   pin=5   → 0               ║
║  [Pin Set]   pin=6   → 1               ║
║  [Pin Set]   pin=17  → 0               ║
║  [Sleep]     3 ms                       ║
║  -- step 2 --                           ║
║  [Pin Set]   pin=4   → 0               ║
║  [Pin Set]   pin=5   → 1               ║
║  [Pin Set]   pin=6   → 1               ║
║  [Pin Set]   pin=17  → 0               ║
║  [Sleep]     3 ms                       ║
║  -- step 3 --                           ║
║  [Pin Set]   pin=4   → 0               ║
║  [Pin Set]   pin=5   → 1               ║
║  [Pin Set]   pin=6   → 0               ║
║  [Pin Set]   pin=17  → 1               ║
║  [Sleep]     3 ms                       ║
║  -- step 4 --                           ║
║  [Pin Set]   pin=4   → 1               ║
║  [Pin Set]   pin=5   → 0               ║
║  [Pin Set]   pin=6   → 0               ║
║  [Pin Set]   pin=17  → 1               ║
║  [Sleep]     3 ms                       ║
╚═════════════════════════════════════════╝
```

**MicroPython code:**

```python
from machine import Pin
import time

_pin_4  = Pin(4,  mode=Pin.OUT)   # IN1
_pin_5  = Pin(5,  mode=Pin.OUT)   # IN2
_pin_6  = Pin(6,  mode=Pin.OUT)   # IN3
_pin_17 = Pin(17, mode=Pin.OUT)   # IN4

def setup():
    pass

def loop():
    # Step 1
    _pin_4.value(1); _pin_5.value(0); _pin_6.value(1); _pin_17.value(0)
    time.sleep_ms(3)
    # Step 2
    _pin_4.value(0); _pin_5.value(1); _pin_6.value(1); _pin_17.value(0)
    time.sleep_ms(3)
    # Step 3
    _pin_4.value(0); _pin_5.value(1); _pin_6.value(0); _pin_17.value(1)
    time.sleep_ms(3)
    # Step 4
    _pin_4.value(1); _pin_5.value(0); _pin_6.value(0); _pin_17.value(1)
    time.sleep_ms(3)
```

> **Speed adjustment:** Change the `sleep_ms(3)` value — lower = faster, higher = slower. Below 2 ms the motor may miss steps. Above 10 ms rotation will be noticeably slow.

**What you learn:**

- Driving a stepper motor through a transistor driver (ULN2003)
- Full-step sequence — the concept of a step and a coil
- How delay between steps affects rotational speed
- Powering 5 V components from the microcontroller board

---

### Lesson 7 — Project template

**Goal:** A starting point for your own experiments.

**What happens:**
The lesson contains only a clean template with empty `setup()` and `loop()` functions and a `KeyboardInterrupt` handler. No functionality is implemented — this is the place for your own program.

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════╗
║  (empty — add initialization here)      ║
╚═════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════╗
║  (empty — add program logic here)       ║
╚═════════════════════════════════════════╝
```

**MicroPython code:**

```python
def setup():
    pass

def loop():
    pass
```

**What you learn:**

- The standard structure of every MicroPython program in this course
- Handling `KeyboardInterrupt` (Ctrl+C) — safe program termination

---

## Program structure

Every sketch in this course follows the same pattern:

```python
from machine import Pin   # hardware module imports
import time               # time module

# --- Pin configuration (global) ---
_pin_XX = Pin(XX, mode=Pin.OUT)

# --- One-time initialization ---
def setup():
    pass  # runs once at startup

# --- Main loop ---
def loop():
    pass  # runs repeatedly

# --- Entry point ---
if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
```

> The `try/except` block at the end ensures that pressing Ctrl+C in the REPL console safely stops the program instead of raising an unhandled exception.

---

## Course progress

```text
Lesson 1  ──  Digital output (LED ON)
Lesson 2  ──  Timing and cycle (LED blink)
Lesson 3  ──  Multiple outputs + sequence (3× LED)
Lesson 4  ──  Digital input + control (button → LED)
Lesson 6  ──  Analog ADC input (photoresistor)
Lesson 7  ──  Project template
Lesson 8  ──  Stepper motor 28BYJ-48 (fan)
```

---

## Platform

- **Microcontroller:** ESP32-S3 (module `esp32-s3-pico`)
- **Language:** MicroPython
- **Environment:** MyCastle / Thonny / mpremote
- **Logic voltage:** 3.3 V
- **Power:** USB 5 V → on-board regulator → 3.3 V
