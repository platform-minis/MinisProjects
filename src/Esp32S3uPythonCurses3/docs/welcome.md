# ESP32-S3 uPython Curses 3

Two interactive lessons combining an **LCD1602 I2C** display with different input modules. **Lesson 31** reads live temperature, humidity and pressure from **AHT20+BMP280** sensors (I2C bus 0) and shows the data on the LCD (I2C bus 1). **Lesson 32** adds a **PS joystick** and turns the LCD into a 2×16 canvas editor — navigate a blinking cursor and place or erase `#` marks with the button.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB OPI PSRAM)
- MicroPython firmware flashed (use the MyCastle Flash tool)
- AHT20 temperature & humidity sensor module (I2C addr 0x38) — Lesson 31
- BMP280 barometric pressure sensor module (I2C addr 0x76 or 0x77) — Lesson 31
- LCD1602 display with PCF8574 I2C backpack (I2C addr 0x27) — both lessons
- PS joystick module (VRX/VRY/SW breakout) — Lesson 32
- Jumper wires

## Skill level

⭐⭐ Beginner–Intermediate — basic MicroPython experience required. Builds on the AHT20+BMP280 lesson from Part 2 (Esp32S3uPythonCurses2) by adding a physical LCD display.

## What's included

| Lesson     | Topic                                                                         |
|------------|-------------------------------------------------------------------------------|
| `Lesson31` | AHT20+BMP280 + LCD1602 — dual I2C bus, live display of temp/humidity/pressure |
| `Lesson32` | PS Joystick + LCD1602 — 2×16 canvas editor, joystick moves cursor, btn marks  |

## Quick start — Lesson 31

1. Wire sensors to I2C bus 0: SDA → GP13, SCL → GP14, VCC → 3.3 V, GND → GND (AHT20 and BMP280 in parallel).
2. Wire LCD1602 to I2C bus 1: SDA → GP43, SCL → GP44, VCC → 5 V (or 3.3 V), GND → GND.
3. Make sure BMP280 SDO matches the address in the sketch (0x77 = SDO to 3.3 V; 0x76 = SDO to GND).
4. Open `Lesson31` and click **Upload → Run only**.
5. The LCD shows `AHT20+BMP280 / ready...` briefly, then updates every 2 s with live readings.

## LCD output format (Lesson 31)

```text
Line 1:  AHT:22.4C 51.3%
Line 2:  BMP:22.7C 1013h
```

## Quick start — Lesson 32

1. Keep the LCD1602 wired to GP43/GP44 from Lesson 31.
2. Connect the joystick: VRX → GP1, VRY → GP2, SW → GP4, VCC → **3.3 V** (not 5 V), GND → GND.
3. Open `Lesson32` and click **Upload → Run only**.
4. Keep the joystick centred while the sketch starts — it calibrates the neutral position on the first run.
5. Push the stick **left/right** to move the cursor column, **up/down** to switch rows, **press** the button to place or erase a `#` mark.

## LCD output format (Lesson 32)

```text
Line 1:  HELLO WORLD_____   (edited text, 16 chars)
Line 2:  Col:12 [D]          (cursor position + current char)
```

## Wiring overview

```text
Lesson 31 — I2C sensors + display
   GP13 ─────── SDA (AHT20 + BMP280)      I2C bus 0
   GP14 ─────── SCL (AHT20 + BMP280)
   GP43 ──────────────────────────────── SDA (LCD1602)   I2C bus 1
   GP44 ──────────────────────────────── SCL (LCD1602)
   3V3  ─────── VCC (AHT20 + BMP280)
   5V   ──────────────────────────────── VCC (LCD1602)
   GND  ─────── GND (all modules) ─────────────────────

Lesson 32 — joystick + display
   GP1  ─────── VRX (joystick X axis)
   GP2  ─────── VRY (joystick Y axis)
   GP4  ─────── SW  (joystick button)
   GP43 ──────────────────────────────── SDA (LCD1602)   I2C bus 1 (same as L31)
   GP44 ──────────────────────────────── SCL (LCD1602)
   3V3  ─────── VCC (joystick)
   5V   ──────────────────────────────── VCC (LCD1602)
   GND  ─────── GND (all modules) ─────────────────────
```

> **Why two I2C buses in Lesson 31?** All three modules can share one bus since their addresses don't clash — but running sensors and display on separate buses keeps wiring cleaner and makes it trivial to add more modules later without worrying about conflicts.
