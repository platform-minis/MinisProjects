# ESP32-S3 uPython Curses 2

A **continuation of the ESP32-S3 MicroPython course** introducing the MAX7219 8×8 LED matrix (SPI) and KY-018 light sensor (ADC). This is part 2 — if you haven't completed part 1 (Esp32S3uPythonCurses), start there first.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB OPI PSRAM)
- MicroPython firmware flashed (use the MyCastle Flash tool)
- MAX7219 8×8 LED matrix module
- KY-018 photoresistor module
- DHT11 temperature & humidity sensor module
- HC-SR04 ultrasonic distance sensor
- HC-SR501 PIR motion sensor module
- RC-522 RFID reader module + RFID card or key fob
- PS2 joystick module (KY-023 or similar)
- Jumper wires (29 wires total)

## Skill level

⭐⭐ Beginner–Intermediate — basic MicroPython experience required (variables, loops, functions). No prior SPI or ADC experience needed.

## What's included

| Lesson      | Topic                                                              |
|-------------|--------------------------------------------------------------------|
| `Lesson13`  | MAX7219 8×8 LED matrix — SPI init, pixel patterns, animation loop  |
| `Lesson14`  | KY-018 — light-level ADC reading, categories (DARK/NORMAL/BRIGHT)  |
| `Lesson15`  | DHT11 — temperature & humidity sensor, threshold warnings          |
| `Lesson16`  | HC-SR04 — ultrasonic distance, trigger/echo, timeout handling      |
| `Lesson17`  | HC-SR501 — PIR motion detection, digital input                     |
| `Lesson18`  | RC-522 — RFID card/tag UID reading via SPI                         |
| `Lesson19`  | Joystick — dual ADC axes, button, direction labels                 |

## Quick start — Lesson 13

1. Wire the MAX7219: CLK → GP18, DIN → GP19, CS → GP5, VCC → 3.3 V, GND → GND.
2. Open `Lesson13` and click **Upload → Run only**.
3. The matrix cycles through Smiley, Heart, Cross and Arrow patterns every 1.5 s.

## Quick start — Lesson 14

1. Wire KY-018: S → GP7, + → 3.3 V, − → GND.
2. Open `Lesson14` and click **Upload → Run only**.
3. Watch the REPL — cover the sensor with your hand (DARK), expose it to a lamp (BRIGHT).

## Quick start — Lesson 17

1. Wire HC-SR501: OUT → GP2, VCC → 5V, GND → GND.
2. Open `Lesson17` and click **Upload → Run only**.
3. Wait ~30 s for the sensor to stabilise, then walk in front of it — `Motion detected!` appears in the terminal.

## Quick start — Lesson 18

1. Wire RC-522: SCK → GP18, MOSI → GP19, MISO → GP16, SDA → GP17, RST → GP15, 3V3 → 3.3 V, GND → GND.
2. Open `Lesson18` and click **Upload → Run only**.
3. Hold an RFID card or key fob near the reader — `Card UID: XX:XX:XX:XX` appears in the terminal.

## Quick start — Lesson 19

1. Wire joystick: VRx → GP1, VRy → GP6, SW → GP8, VCC → 3.3 V, GND → GND.
2. Open `Lesson19` and click **Upload → Run only**.
3. Move the stick — direction labels appear in the terminal. Press the shaft down to see `[BTN]`.

## How to create custom patterns

Each pattern is a list of 8 bytes — one per row. Bit 7 (leftmost) to bit 0 (rightmost): `1` = LED on, `0` = LED off.

```python
MY_PATTERN = [
    0b00000000,
    0b00111100,
    0b01000010,
    0b10000001,
    0b10000001,
    0b01000010,
    0b00111100,
    0b00000000,
]
```
