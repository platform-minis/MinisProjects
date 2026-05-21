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
| **WS2812B** | Addressable RGB LED (NeoPixel) — receives colour data over a single wire using a precise timing protocol; on the ESP32-S3 Pico it is the on-board LED on GP47 |
| **NeoPixel** | Common name for WS2812B-compatible addressable LEDs; MicroPython provides the built-in `neopixel` module to drive them |
| **HSV** | Hue-Saturation-Value colour model — hue (0–360°) selects the colour, saturation controls vividness, value controls brightness |
| **Active LOW** | A signal that is "active" (button pressed, LED on) when the voltage is 0 V; the BOOT button on the ESP32-S3 Pico reads 0 when pressed, 1 when released |
| **Pull-up resistor** | A resistor (usually internal to the MCU) connecting a pin to 3.3 V, ensuring a HIGH state when the button is released |
| **Short press / Long press** | Two distinct button gestures distinguished by duration; the sketch uses 600 ms as the threshold |
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
| **DHT11** | Digital temperature and humidity sensor — communicates over a single-wire protocol; built-in MicroPython `dht` module handles the protocol automatically |
| **HC-SR04** | Ultrasonic distance sensor — emits a 40 kHz burst and measures the echo travel time to calculate distance (2–400 cm range) |
| **PIR sensor** | Passive Infrared sensor — detects movement by sensing changes in infrared radiation emitted by warm objects; output goes HIGH on motion |
| **TRIG** | Trigger pin of HC-SR04 — a 10 µs HIGH pulse starts a measurement |
| **ECHO** | Echo pin of HC-SR04 — remains HIGH for a duration proportional to the measured distance |
| **time_pulse_us()** | MicroPython function that measures the duration (in µs) of a pulse on a pin |
| **setup()** | Initialization function called once at program start |
| **loop()** | Main loop function called repeatedly |
| **sleep_ms(n)** | Pauses program execution for *n* milliseconds |
| **REPL** | Read-Eval-Print Loop — the interactive MicroPython console |

---

## Microcontroller Pinout

![Alt text](https://raw.githubusercontent.com/platform-minis/MinisProjects/refs/heads/main/docs/Esp32S3Pico/esp32s3pico_pinout.gif "Esp32 S3 Pico Pinout Top")

![Alt text](https://raw.githubusercontent.com/platform-minis/MinisProjects/refs/heads/main/docs/Esp32S3Pico/esp32s3pico_pinout_buttom.jpg "Esp32 S3 Pico Pinout Bottom")

## Pin Overview

| Pin | Mode | Component | Lessons |
| --- | ---- | --------- | ------- |
| 0 | Digital input | BOOT button (active LOW, built-in) | Lesson0 |
| 3 | Digital in/out | DHT11 DATA | Lesson 12 |
| 4 | Digital input | PIR motion sensor (OUT) | Lesson 11 |
| 11 | Digital output | LED | Lesson 1, 2, 4 |
| 12 | Digital output | LED (red) | Lesson 3 |
| 13 | Digital output | LED (yellow) | Lesson 3 |
| 14 | Digital output | LED (green) | Lesson 3 |
| 16 | Digital input | Tactile button | Lesson 4 |
| 18 | PWM output | Passive buzzer | Lesson 9 |
| 47 | Digital output | WS2812B on-board RGB LED (built-in) | Lesson0 |

---

## Wiring Diagrams

### On-board RGB LED and BOOT button (Lesson0)

No external wiring needed — both components are soldered onto the board.

```text
ESP32-S3 Pico — built-in components
┌──────────────────────────────────────────┐
│  GP47 ──── WS2812B (RGB LED)  ←on-board │
│  GP0  ──── BOOT button        ←on-board │
│            (active LOW — reads 0 when    │
│             pressed, 1 when released)    │
└──────────────────────────────────────────┘
```

> **No external components needed for Lesson0.** The WS2812B and BOOT button are mounted directly on the PCB.
> If the LED does not light up, check that your MicroPython firmware was compiled with NeoPixel support (all standard ESP32-S3 builds include it). If the colour is wrong or the LED is very dim, try changing `_RGB_PIN` to 48 — some board revisions use GP48 instead of GP47.

**BOOT button behaviour:**

```text
Button released:  GP0 ──[internal pull-up]── 3.3 V   → reads 1
Button pressed:   GP0 ──[switch]── GND                → reads 0
```

---

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

### Passive buzzer connection (Lesson 9)

```text
ESP32-S3 Pico
┌──────────────┐
│        GP18  ├──── (+) Buzzer (−) ──── GND
│         GND  ├──────────────────────────┘
└──────────────┘
```

> **Passive vs active buzzer:** A **passive** buzzer has no internal oscillator — it needs a PWM signal to vibrate at a specific frequency (pitch). An **active** buzzer has an internal oscillator and only needs power to beep. Lesson 9 uses a **passive** buzzer so you can control the pitch via PWM. No resistor is needed — the buzzer's internal coil already limits current.

```text
GP18 ──── (+) terminal of passive buzzer
GND  ──── (−) terminal of passive buzzer
```


---

## Breadboard Wiring Guide

### How a breadboard works

A breadboard lets you build circuits without soldering. Components and wires plug into holes that are connected internally in a fixed pattern.

```text
       Columns →
       a    b    c    d    e    GAP    f    g    h    i    j
  1  [ ]──[ ]──[ ]──[ ]──[ ]         [ ]──[ ]──[ ]──[ ]──[ ]
  2  [ ]──[ ]──[ ]──[ ]──[ ]         [ ]──[ ]──[ ]──[ ]──[ ]
  3  [ ]──[ ]──[ ]──[ ]──[ ]         [ ]──[ ]──[ ]──[ ]──[ ]
  4  [ ]──[ ]──[ ]──[ ]──[ ]         [ ]──[ ]──[ ]──[ ]──[ ]
  5  [ ]──[ ]──[ ]──[ ]──[ ]         [ ]──[ ]──[ ]──[ ]──[ ]
R
o
w
s
↓
```

**Rules:**

- All 5 holes in the same **row** on one side (a–e or f–j) are **connected** to each other
- The **gap** in the middle separates the two halves — no connection across it
- Holes in the same **column** but different rows are **not** connected
- The **+** and **−** rails running along the long edges are each one continuous wire — use them for 3.3 V and GND

```text
+ ════════════════════════════════════════════════ +  ← all connected (3.3 V)
- ════════════════════════════════════════════════ -  ← all connected (GND)

  a   b   c   d   e       f   g   h   i   j
[ ]─[ ]─[ ]─[ ]─[ ]   [ ]─[ ]─[ ]─[ ]─[ ]   row 1
[ ]─[ ]─[ ]─[ ]─[ ]   [ ]─[ ]─[ ]─[ ]─[ ]   row 2
[ ]─[ ]─[ ]─[ ]─[ ]   [ ]─[ ]─[ ]─[ ]─[ ]   row 3
```

> **Tip:** Use a short wire (jumper) from the ESP32-S3 `3V3` pin to the `+` rail, and from `GND` to the `−` rail. Then use those rails as your power source throughout the board.

---

### LED + resistor on breadboard (Lessons 1, 2, 4)

**Components needed:**

- 1× LED (any color)
- 1× resistor 330 Ω (orange–orange–brown stripes)
- 2× jumper wire (male-to-male)

**Identifying LED legs:**

```text
      Anode (+)   Cathode (−)
          │            │
          │            │
         longer       shorter
          leg          leg
           \          /
            \        /
             [  LED  ]
                │
          flat side = cathode (−)
```

**Step-by-step breadboard layout:**

```text
+ ══════════════════════════════ +
- ══════════════════════════════ -

      a     b     c     d     e
 5  [GP11]─[GP11]─[ ]──[ ]──[ ]     ← wire from GP11 lands here (e.g. row 5a)
 6  [RES ]─[RES ]─[ ]──[ ]──[ ]     ← resistor: one leg in 5a, other leg in 6a
 7  [LED+]─[LED+]─[ ]──[ ]──[ ]     ← LED anode (+, longer leg) in 6a, cathode in 7a
 8  [GND ]─[GND ]─[ ]──[ ]──[ ]     ← wire from 7a to − rail
```

Clearer layout (each row = one breadboard row, letters = columns):

```text
        a        b        c        d        e
 1    [===]    [   ]    [   ]    [   ]    [   ]    ← 1a: wire from GP11
 2    [===]    [   ]    [   ]    [   ]    [   ]    ← 2a: resistor leg 1
 3    [===]    [   ]    [   ]    [   ]    [   ]    ← 3a: resistor leg 2 + LED anode (+)
 4    [===]    [   ]    [   ]    [   ]    [   ]    ← 4a: LED cathode (−)
 5    [===]    [   ]    [   ]    [   ]    [   ]    ← 5a: wire to GND rail

 Connections:
   GP11 pin  →  wire  →  row 1a
   row 1a    ────────────  row 2a   (same row: connected)
   row 2a  ──[330Ω]──  row 3a
   row 3a  ──  LED anode (+, long leg)  ──  row 4a  (LED cathode, short leg)
   row 4a    →  wire  →  − rail (GND)
```

**Actual connection sequence:**

```text
Step 1: plug a jumper wire from ESP32-S3 GP11 → breadboard row 1, column a
Step 2: insert resistor (330 Ω) between row 1a and row 3a
        (resistor spans 2 rows — that's fine, it bridges them)
Step 3: insert LED — long leg (anode +) into row 3a,
                     short leg (cathode −) into row 4a
Step 4: plug a jumper wire from row 4a → − (GND) rail
Step 5: connect ESP32-S3 GND pin → − rail  (if not done already)
```

**Side view (one column):**

```text
GP11 ──wire──[row 1]──[330Ω]──[row 3]──LED+  LED−──[row 4]──wire── GND rail
```

---

### Three LEDs on breadboard (Lesson 3)

**Components needed:**

- 3× LED (green, yellow, red)
- 3× resistor 330 Ω
- 6× jumper wire

Each LED + resistor pair follows the same pattern as above, placed on separate rows:

```text
        a         b         c         d         e
  1   [GP14]    [   ]    [   ]    [   ]    [   ]   ← wire from GP14
  2   [ R  ]    [   ]    [   ]    [   ]    [   ]   ← 330 Ω resistor
  3   [LED+]    [   ]    [   ]    [   ]    [   ]   ← green LED anode
  4   [LED−]────────────────────────────────wire──► GND rail

  6   [GP13]    [   ]    [   ]    [   ]    [   ]   ← wire from GP13
  7   [ R  ]    [   ]    [   ]    [   ]    [   ]   ← 330 Ω resistor
  8   [LED+]    [   ]    [   ]    [   ]    [   ]   ← yellow LED anode
  9   [LED−]────────────────────────────────wire──► GND rail

 11   [GP12]    [   ]    [   ]    [   ]    [   ]   ← wire from GP12
 12   [ R  ]    [   ]    [   ]    [   ]    [   ]   ← 330 Ω resistor
 13   [LED+]    [   ]    [   ]    [   ]    [   ]   ← red LED anode
 14   [LED−]────────────────────────────────wire──► GND rail
```

> Leave a gap of at least one empty row between each LED group to avoid accidentally connecting components.

---

### Tactile button on breadboard (Lesson 4)

**Understanding the button legs:**

A tactile (push) button has 4 legs arranged in two pairs. The two legs on the **same side** are always connected internally. Pressing the button connects the **left pair** to the **right pair**.

```text
     Top view of button

     leg A ──┐     ┌── leg C
             │     │
     leg B ──┘     └── leg D
      (A─B always connected)  (C─D always connected)
      pressing button connects A/B to C/D
```

**Orienting on the breadboard:**

Place the button so it **straddles the centre gap** — one pair of legs on the left half (columns a–e), the other pair on the right half (columns f–j). This way the two sides are only connected when the button is pressed.

```text
         a    b    c    d    e         f    g    h    i    j
  5    [   ] [   ] [A ] [   ] [   ] | [C ] [   ] [   ] [   ] [   ]
  6    [   ] [   ] [B ] [   ] [   ] | [D ] [   ] [   ] [   ] [   ]
                    ↑                   ↑
               left pair            right pair
               (A─B joined)         (C─D joined)
               press → A─B─C─D all joined
```

**Full button + pull-down resistor layout:**

```text
        a     b     c     d     e           f     g     h     i     j
  5   [   ] [   ] [ A ] [   ] [   ]  |  [ C ] [   ] [   ] [   ] [   ]
  6   [   ] [   ] [ B ] [   ] [   ]  |  [ D ] [   ] [   ] [   ] [   ]

  Connections:
   3.3V rail  →  wire  →  row 5c  (leg A / left-top of button)
   row 6c  (leg B / left-bottom)  →  wire  →  GP16
   row 6c                         →  10 kΩ  →  GND rail   (pull-down)
```

**Step-by-step:**

```text
Step 1: press button into breadboard so it straddles the centre gap
        left legs in columns c (rows 5 and 6), right legs in column f (rows 5 and 6)
Step 2: wire from + rail (3.3 V) → row 5c  (top-left leg of button)
Step 3: wire from row 6c         → GP16 on ESP32-S3
Step 4: insert 10 kΩ resistor between row 6c and − rail (GND)
        this is the pull-down — it keeps GP16 LOW when button is not pressed
Step 5: connect ESP32-S3 3V3 → + rail,  GND → − rail  (if not done)
```

**Signal logic:**

```text
Button released:  3.3V ──[BTN open]── GP16 ──[10kΩ]── GND   → GP16 reads 0
Button pressed:   3.3V ──[BTN closed]─ GP16 ──[10kΩ]── GND  → GP16 reads 1
```

> **Why pull-down?** Without the resistor, GP16 would be "floating" when the button is released — it could read random 0s and 1s from electrical noise. The pull-down resistor anchors it firmly to 0 (LOW).

---

## Communication Protocols

Modern microcontrollers like ESP32-S3 communicate with sensors, displays, and other peripherals using standard serial protocols. The three most common are **UART**, **SPI**, and **I2C**. Each has different wiring, speed, and use cases.

---

### UART — Universal Asynchronous Receiver-Transmitter

UART is the simplest serial protocol — two devices talk directly to each other using just two wires.

**Wires:**

| Signal | Direction | Description |
| ------ | --------- | ----------- |
| TX | → | Transmit — data sent by this device |
| RX | ← | Receive — data received by this device |
| GND | — | Common ground (always required) |

**Key rules:**

- TX of one device connects to RX of the other (cross-wired)
- Both sides must agree on the same **baud rate** (e.g. 9600, 115200 bps)
- No clock wire — timing is derived from the agreed baud rate
- Point-to-point only: one sender, one receiver

```text
ESP32-S3                Peripheral
  TX  ────────────────►  RX
  RX  ◄────────────────  TX
  GND ─────────────────  GND
```

**Typical use cases:** GPS modules, Bluetooth serial, debug console, communication between two microcontrollers.

**MicroPython example:**

```python
from machine import UART
uart = UART(1, baudrate=9600, tx=17, rx=18)
uart.write('Hello\n')
data = uart.read(32)
```

---

### I2C — Inter-Integrated Circuit

I2C uses only **two wires** and supports **multiple devices** on the same bus. Each device has a unique 7-bit address so the master can address them individually.

**Wires:**

| Signal | Description |
| ------ | ----------- |
| SDA | Serial Data — bidirectional data line |
| SCL | Serial Clock — clock generated by the master |
| GND | Common ground |

**Key rules:**

- One master (ESP32-S3), many slaves (sensors, displays…)
- Both SDA and SCL require **pull-up resistors** to 3.3 V (typically 4.7 kΩ) — many breakout boards include them
- Typical speeds: 100 kHz (standard), 400 kHz (fast)
- Each slave has a fixed address (printed in its datasheet, e.g. `0x3C` for SSD1306 OLED)

```text
ESP32-S3        Sensor A       Sensor B       Display
  SDA ──────────── SDA ───────── SDA ────────── SDA
  SCL ──────────── SCL ───────── SCL ────────── SCL
  GND ──────────── GND ───────── GND ────────── GND
  3V3 ──[4.7kΩ]── SDA
  3V3 ──[4.7kΩ]── SCL
```

**Typical use cases:** temperature/humidity sensors (DHT, BMP280, SHT31), OLED displays (SSD1306), real-time clocks (DS3231), accelerometers (MPU6050).

**MicroPython example:**

```python
from machine import I2C, Pin
i2c = I2C(0, scl=Pin(22), sda=Pin(21), freq=400_000)
devices = i2c.scan()          # returns list of found addresses
print([hex(d) for d in devices])
data = i2c.readfrom(0x3C, 4) # read 4 bytes from device at address 0x3C
```

---

### SPI — Serial Peripheral Interface

SPI is the fastest of the three protocols. It uses **4 wires** and is full-duplex (sends and receives simultaneously). Like I2C it supports multiple slaves, but each slave needs its own **CS** (Chip Select) wire.

**Wires:**

| Signal | Alternative name | Description |
| ------ | ---------------- | ----------- |
| MOSI | SDO, TX | Master Out Slave In — data from master to slave |
| MISO | SDI, RX | Master In Slave Out — data from slave to master |
| SCK | CLK | Clock — generated by the master |
| CS | SS, CE | Chip Select — one wire per slave, active LOW |

```text
ESP32-S3       Slave A          Slave B
  MOSI ──────── MOSI ─────────── MOSI
  MISO ──────── MISO ─────────── MISO
  SCK  ──────── SCK  ─────────── SCK
  GP10 ──────── CS               (high = inactive)
  GP11 ───────────────────────── CS
  GND  ──────── GND  ─────────── GND
```

**Key rules:**

- No pull-up resistors needed
- Typical speeds: 1–50 MHz (much faster than I2C)
- Each additional slave needs one extra CS pin on the master
- Full-duplex: master and slave exchange data simultaneously

**Typical use cases:** SD card readers, TFT displays (ILI9341, ST7789), flash memory, high-speed sensors.

**MicroPython example:**

```python
from machine import SPI, Pin
spi = SPI(1, baudrate=10_000_000, polarity=0, phase=0,
          sck=Pin(18), mosi=Pin(23), miso=Pin(19))
cs = Pin(10, Pin.OUT, value=1)   # CS idle HIGH

cs.value(0)                      # select slave
spi.write(b'\x9F')               # send command
result = spi.read(3)             # read 3 bytes response
cs.value(1)                      # deselect slave
```

---

### Protocol comparison

| Feature | UART | I2C | SPI |
| ------- | ---- | --- | --- |
| Wires | 2 (+ GND) | 2 (+ GND) | 4 (+ 1 per slave) |
| Max devices | 2 (point-to-point) | ~127 | unlimited (1 CS per slave) |
| Speed | 115 kbps typical | up to 400 kHz | up to 50 MHz |
| Pull-ups needed | No | Yes (4.7 kΩ) | No |
| Full-duplex | No | No | Yes |
| Complexity | Simplest | Medium | Medium |
| Typical use | Debug, GPS, BT | Sensors, OLED | Displays, SD card |

---

## Lessons

Each lesson is shown in two forms: **Blockly blocks** (visual editor) and **MicroPython code** (auto-generated).

---

### Lesson0 — On-board RGB LED animations

**Goal:** Drive the WS2812B RGB LED built into the ESP32-S3 Pico board through five different light animations, switching between them with the BOOT button — no external components required.

**What happens:**
The sketch runs one of five animations on the on-board RGB LED (GP47). Pressing the BOOT button (GP0) cycles forward through animations; holding it for more than 600 ms cycles backward. The active animation name is printed to the REPL each time it changes.

| # | Animation | Description |
| - | --------- | ----------- |
| 0 | **Rainbow** | Continuously rotates through the full colour wheel (HSV hue 0–360°) |
| 1 | **Breathing** | Fades white in and out using a sine-wave brightness curve |
| 2 | **Heartbeat** | Red double-pulse with a long pause — mimics a heartbeat rhythm |
| 3 | **Color shift** | Smoothly transitions between colours sampled at golden-angle (137°) steps |
| 4 | **Strobe** | Rapid white flashes with a dark gap — classic strobe effect |

**Button gestures:**

```text
Short press  (< 600 ms):  next animation  →
Long press   (≥ 600 ms):  prev animation  ←
```

> **Note:** This lesson requires **Code mode** for the full implementation.
> The Blockly view shows a simplified 3-colour version using the RGB and Button blocks.
> Open the sketch in Code mode to see and run all five animations.

**No wiring needed** — see the *On-board RGB LED and BOOT button* section.

**Key concepts:**

```text
neopixel.NeoPixel(Pin(47), 1)   # 1 addressable LED on GP47
_np[0] = (R, G, B)              # set colour as (0-255, 0-255, 0-255)
_np.write()                     # push data to the LED
Pin(0, Pin.IN, Pin.PULL_UP)     # BOOT button with internal pull-up
_btn.value() == 0               # True when button is pressed (active LOW)
time.ticks_ms()                 # millisecond timestamp for long-press detection
time.ticks_diff(now, start)     # elapsed time, safe across 32-bit rollover
```

**HSV → RGB conversion (used by Rainbow and Color shift):**

```text
Hue (H):        0°=red  60°=yellow  120°=green  180°=cyan  240°=blue  300°=magenta
Saturation (S): 0.0=white  →  1.0=pure colour
Value (V):      0.0=off   →  1.0=full brightness

Example:
  _hsv(0,   1.0, 1.0) → (255,  0,   0)   red
  _hsv(120, 1.0, 1.0) → (0,  255,   0)   green
  _hsv(240, 1.0, 1.0) → (0,    0, 255)   blue
  _hsv(60,  1.0, 0.5) → (127, 127,  0)   dark yellow
```

**Blockly blocks (simplified — 3 colours):**

```text
╔══ ▶ START ══════════════════════════════════════════════════╗
║  [Init RGB]    rgb                                          ║
║  [Init Button] Btn1  pin=0  active_low=True  pullup=True   ║
║  [Set]         anim = 0                                     ║
║  [RGB fill]    rgb  color=OFF                               ║
║  [Print]       "RGB ready — short press = next, hold = prev"║
╚═════════════════════════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════════════════════════╗
║  [Button tick]  Btn1                                        ║
║  ╔══ If  Btn1  was clicked ═══════════════════════════════╗  ║
║  ║  [Set]  anim = (anim + 1) % 3                         ║  ║
║  ╚════════════════════════════════════════════════════════╝  ║
║  ╔══ If  Btn1  was hold ══════════════════════════════════╗  ║
║  ║  [Set]  anim = (anim + 2) % 3   [= prev mod 3]        ║  ║
║  ╚════════════════════════════════════════════════════════╝  ║
║  ╔══ If anim == 0 ════════════════════════════════════════╗  ║
║  ║  [RGB fill]  rgb  red                                  ║  ║
║  ╠══ Else if anim == 1 ══════════════════════════════════╣  ║
║  ║  [RGB fill]  rgb  green                                ║  ║
║  ╠══ Else ════════════════════════════════════════════════╣  ║
║  ║  [RGB fill]  rgb  blue                                 ║  ║
║  ╚════════════════════════════════════════════════════════╝  ║
║  [Sleep]  20 ms                                             ║
╚═════════════════════════════════════════════════════════════╝
```

**MicroPython code (full 5-animation version — Code mode):**

```python
import neopixel
from machine import Pin
import time
import math

_RGB_PIN = 47   # WS2812B on-board LED
_BTN_PIN = 0    # BOOT button, active LOW

_np  = neopixel.NeoPixel(Pin(_RGB_PIN), 1)
_btn = Pin(_BTN_PIN, Pin.IN, Pin.PULL_UP)

_ANIM_COUNT = 5
_anim_idx   = 0
_NAMES      = ('Rainbow', 'Breathing', 'Heartbeat', 'Color shift', 'Strobe')

_btn_down, _btn_pressed_at = False, 0
_LONG_MS = 600

def _hsv(h, s, v):
    i      = int(h / 60) % 6
    f      = (h / 60) - int(h / 60)
    p, q, t = v*(1-s), v*(1-s*f), v*(1-s*(1-f))
    r, g, b = ((v,t,p),(q,v,p),(p,v,t),(p,q,v),(t,p,v),(v,p,q))[i]
    return (int(r*255), int(g*255), int(b*255))

# Animation 0 — Rainbow
_rb_hue = 0
def _rainbow():
    global _rb_hue
    _rb_hue = (_rb_hue + 3) % 360
    _np[0] = _hsv(_rb_hue, 1.0, 1.0); _np.write(); time.sleep_ms(20)

# Animation 1 — Breathing
_br_deg = 0
def _breathing():
    global _br_deg
    c = int((math.sin(math.radians(_br_deg)) + 1.0) * 0.5 * 230) + 5
    _np[0] = (c, c, c); _np.write()
    _br_deg = (_br_deg + 3) % 360; time.sleep_ms(15)

# Animation 2 — Heartbeat
_PULSE = (0, 30, 120, 220, 255, 220, 80, 0, 0, 60, 160, 80, 0, 0, 0, 0)
_hb_i  = 0
def _heartbeat():
    global _hb_i
    _np[0] = (_PULSE[_hb_i % len(_PULSE)], 0, 0); _np.write()
    _hb_i += 1; time.sleep_ms(75)

# Animation 3 — Color shift (golden-angle steps)
_cs_hue, _cs_target = 0, 137
def _colorshift():
    global _cs_hue, _cs_target
    diff = (_cs_target - _cs_hue) % 360
    if diff > 180: diff -= 360
    if abs(diff) <= 2:
        _cs_hue = _cs_target; _cs_target = (_cs_target + 137) % 360
    else:
        _cs_hue = (_cs_hue + (2 if diff > 0 else -2)) % 360
    _np[0] = _hsv(_cs_hue, 1.0, 0.9); _np.write(); time.sleep_ms(18)

# Animation 4 — Strobe
_ST = (1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0); _st_i = 0
def _strobe():
    global _st_i
    _np[0] = (255,255,255) if _ST[_st_i % len(_ST)] else (0,0,0)
    _np.write(); _st_i += 1; time.sleep_ms(55)

_ANIMS = (_rainbow, _breathing, _heartbeat, _colorshift, _strobe)

def _poll_btn():
    global _anim_idx, _btn_down, _btn_pressed_at
    pressed = _btn.value() == 0
    now = time.ticks_ms()
    if pressed and not _btn_down:
        _btn_down = True; _btn_pressed_at = now
    elif not pressed and _btn_down:
        _btn_down = False
        held = time.ticks_diff(now, _btn_pressed_at) >= _LONG_MS
        _anim_idx = (_anim_idx + (-1 if held else 1)) % _ANIM_COUNT
        print(('Prev' if held else 'Next') + ':', _NAMES[_anim_idx])

def setup():
    _np[0] = (0,0,0); _np.write()
    print('RGB LED Animations — 5 modes')
    print('Short press BOOT = next   |   Long press BOOT = prev')
    print('Active:', _NAMES[_anim_idx])

def loop():
    _poll_btn(); _ANIMS[_anim_idx]()
```

**What you learn:**

- Driving a WS2812B (NeoPixel) with the built-in `neopixel` module
- HSV → RGB colour conversion — separating hue from brightness
- Reading an active-LOW button using `Pin.PULL_UP`
- Short-press vs long-press detection using `ticks_ms()` and `ticks_diff()`
- Python function tables (`_ANIMS` tuple) as a simple state machine
- Multiple independent animation state variables (hue counter, sine angle, sequence index…)

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

### Lesson 9 — Passive buzzer (melody)

**Goal:** Generate musical tones using PWM — play a repeating three-note arpeggio (C–E–G) on a passive buzzer.

**What happens:**
PWM is initialized on GP18. Each loop iteration sets three successive frequencies — 262 Hz (C4), 330 Hz (E4), 392 Hz (G4) — each held for 200 ms, then the duty cycle is set to 0 to produce a 500 ms silence before the next cycle. The ESP32-S3 hardware PWM timer generates the square wave signal directly, so the CPU is not busy during each tone.

**Components used:**

- **Passive buzzer** (3–5 V rated, any impedance 8–32 Ω)

**Wiring:**

```text
ESP32-S3 Pico
┌──────────────┐
│        GP18  ├──── (+) Passive buzzer (−) ──── GND
│         GND  ├─────────────────────────────────┘
└──────────────┘
```

**Note frequencies used:**

```text
Note │ Frequency │ Description
─────┼───────────┼────────────
C4   │  262 Hz   │ Middle C
E4   │  330 Hz   │ Major third above C
G4   │  392 Hz   │ Perfect fifth above C
```

**Blockly blocks:**

```text
╔══ ▶ START ══════════════════════════════════╗
║  [PWM Init]  pin=18  freq=262  duty=0       ║
╚═════════════════════════════════════════════╝

╔══ 🔁 FOREVER ═══════════════════════════════╗
║  [PWM Set freq]  pin=18  262 Hz             ║
║  [PWM Set duty]  pin=18  512  (50%)         ║
║  [Sleep]  200 ms                            ║
║  [PWM Set freq]  pin=18  330 Hz             ║
║  [Sleep]  200 ms                            ║
║  [PWM Set freq]  pin=18  392 Hz             ║
║  [Sleep]  200 ms                            ║
║  [PWM Set duty]  pin=18  0  (silent)        ║
║  [Sleep]  500 ms                            ║
╚═════════════════════════════════════════════╝
```

**MicroPython code:**

```python
from machine import Pin, PWM
import time

# Passive buzzer on GP18 — duty=0 on init keeps it silent
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
```

> **duty vs freq:** `duty(512)` sets the PWM duty cycle to ~50 % (512 out of 1023), which gives the loudest signal for a square wave. `freq()` changes only the pitch — you don't need to call `duty()` again between notes. To silence the buzzer use `duty(0)` rather than changing the frequency.
>
> **Experimenting:** Try changing the three frequencies to other values from the table below to compose your own melody. Any integer between 20 Hz and 20 000 Hz will work.

```text
Common note frequencies (octave 4):
C4=262  D4=294  E4=330  F4=349  G4=392  A4=440  B4=494  C5=523
```

**What you learn:**

- Generating audio signals with hardware PWM
- Difference between passive and active buzzers
- Controlling pitch with `freq()` and volume/silence with `duty()`
- Building a simple melody loop in MicroPython


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
Lesson0 ──  On-board RGB LED (WS2812B) + BOOT button animations
Lesson 1  ──  Digital output (LED ON)
Lesson 2  ──  Timing and cycle (LED blink)
Lesson 3  ──  Multiple outputs + sequence (3× LED)
Lesson 4  ──  Digital input + control (button → LED)
Lesson 9  ──  Passive buzzer (melody)
Lesson 11 ──  PIR motion sensor
Lesson 12 ──  DHT11 temperature and humidity
```

---

## Using the Terminal in MyCastle Minis

MyCastle provides a built-in MicroPython REPL terminal accessible directly from the browser — no external tools needed. You can upload sketches, run code interactively, and monitor output all from one place.

---

### Opening a project

1. Log in to MyCastle and go to **Electronics → MicroPython** in the left menu
2. Find the **ESP32-S3 uPython Curses** project and click it
3. The project page opens with the Blockly editor on the left and the code editor on the right
4. Select a lesson from the **sketch list** at the top (e.g. `Lesson1`)

---

### Uploading and running a sketch

Click the **Upload** button (top right of the project page). The upload dialog opens:

```text
┌─────────────────────────────────────────────────────┐
│  Upload to Device                                   │
│                                                     │
│  [SERIAL REPL]   [WEBREPL]                         │
│                                                     │
│  Device: Esp32S3Pico-XXXX  ·  WiFi: ···           │
│  Baud rate:  [ 115200 ▼ ]                          │
│                                                     │
│  Upload mode:                                       │
│  ● Run only    ○ Save as main.py                   │
│                                                     │
│  ┌───────────────────────────────────────────────┐  │
│  │ REPL terminal output                          │  │
│  │ > OK                                          │  │
│  │   Led is On                                   │  │
│  │   Done.                                       │  │
│  └───────────────────────────────────────────────┘  │
│                                          [ UPLOAD ]  │
└─────────────────────────────────────────────────────┘
```

**Connection tabs:**

| Tab | When to use |
| --- | ----------- |
| **Serial REPL** | Board connected via USB cable to the computer running MyCastle |
| **WebREPL** | Board connected over WiFi (must be configured first on the device) |

**Upload modes:**

| Mode | What it does |
| ---- | ------------ |
| **Run only** | Sends the code to the board and runs it immediately. Nothing is saved — on reset the board returns to its previous state. Use this for quick testing. |
| **Save as main.py** | Saves the code as `main.py` on the board's filesystem. The code will run automatically every time the board powers on. Use this when the sketch is ready. |

> Libraries (e.g. `minis_iot.py`, `minis_display.py`) are always written to the filesystem even in **Run only** mode — they are required for `import` statements to work.

---

### Serial REPL — keyboard shortcuts

The REPL terminal at the bottom of the upload dialog is a live MicroPython console. Use these key combinations to control the board:

| Shortcut | Action |
| -------- | ------ |
| `Ctrl + C` | **Interrupt** — stops the currently running program (sends KeyboardInterrupt) |
| `Ctrl + D` | **Soft reset** — restarts MicroPython without a hardware reset; runs `main.py` if it exists |
| `Ctrl + B` | **Exit raw REPL** — returns to the normal interactive prompt `>>>` |
| `Ctrl + E` | **Paste mode** — lets you paste multiple lines of code at once; end with `Ctrl + D` |
| `↑ / ↓` | Navigate **command history** (previous / next command) |
| `Tab` | **Auto-complete** — press after a partial name to complete it (e.g. `Pi` + Tab → `Pin`) |

---

### Interactive REPL — testing code line by line

After the sketch finishes (or after pressing `Ctrl + C`), the `>>>` prompt appears. You can type Python directly:

```text
>>> from machine import Pin
>>> led = Pin(11, Pin.OUT)
>>> led.value(1)          # LED turns on
>>> led.value(0)          # LED turns off
>>> import time
>>> time.sleep_ms(500)
>>> led.value(1)
```

This is useful for:

- **Testing a single command** before putting it in a sketch
- **Checking pin state** after a program runs
- **Exploring modules** — type `help()` or `help('modules')` to see what is available
- **Debugging** — print variable values, check ADC readings, etc.

```text
>>> from machine import ADC, Pin
>>> adc = ADC(Pin(7), atten=ADC.ATTN_11DB)
>>> adc.read()
1823
>>> adc.read()
2104
```

---

### Filesystem management via REPL

MicroPython has a small built-in filesystem (LittleFS) on the board's flash memory. You can manage files from the REPL:

```text
>>> import os
>>> os.listdir('/')          # list files in root
['boot.py', 'main.py', 'minis_iot.py']

>>> os.remove('main.py')     # delete a file

>>> f = open('notes.txt', 'w')
>>> f.write('Hello')
>>> f.close()

>>> f = open('notes.txt', 'r')
>>> print(f.read())
Hello
```

---

### Common workflow

```text
1. Open project page in MyCastle Minis
        ↓
2. Select a lesson sketch from the list
        ↓
3. Edit code in the editor (right panel) or blocks (left panel)
        ↓
4. Click Upload → select Serial REPL → mode: Run only
        ↓
5. Watch output in the REPL terminal
        ↓
6. Press Ctrl+C to stop, adjust code, upload again
        ↓
7. When satisfied → Upload → mode: Save as main.py
        ↓
8. Unplug USB — board runs the sketch automatically on power-up
```

---

### Troubleshooting

| Symptom | Likely cause | Fix |
| ------- | ------------ | --- |
| Port not found | USB cable not connected or wrong cable (charge-only) | Use a data USB cable; check device manager |
| `OSError: [Errno 16]` | Port busy (another tool has it open) | Close Thonny, mpremote, or other serial monitors |
| Upload hangs | Board stuck in running program | Press `Ctrl + C` in terminal first, then upload |
| `ImportError` after save | Library not on board | Upload with a sketch that includes the library |
| Board not responding | Firmware crash | Press reset button on the board, then try again |

---

## Platform

- **Microcontroller:** ESP32-S3 (module `esp32-s3-pico`)
- **Language:** MicroPython
- **Environment:** MyCastle / Thonny / mpremote
- **Logic voltage:** 3.3 V
- **Power:** USB 5 V → on-board regulator → 3.3 V
