# ESP32-S3 uPython Curses 3

Reads temperature, humidity and pressure from **AHT20+BMP280** sensors and displays live readings on a **LCD1602 I2C** screen. The two sensor modules share I2C bus 0 (GP13/GP14); the LCD runs on a separate I2C bus 1 (GP33/GP34) to keep wiring clean.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB OPI PSRAM)
- MicroPython firmware flashed (use the MyCastle Flash tool)
- AHT20 temperature & humidity sensor module (I2C addr 0x38)
- BMP280 barometric pressure sensor module (I2C addr 0x76 or 0x77)
- LCD1602 display with PCF8574 I2C backpack (I2C addr 0x27)
- Jumper wires

## Skill level

⭐⭐ Beginner–Intermediate — basic MicroPython experience required. Builds on the AHT20+BMP280 lesson from Part 2 (Esp32S3uPythonCurses2) by adding a physical LCD display.

## What's included

| Lesson     | Topic                                                                         |
|------------|-------------------------------------------------------------------------------|
| `Lesson31` | AHT20+BMP280 + LCD1602 - dual I2C bus, live display of temp/humidity/pressure |

## Quick start — Lesson 31

1. Wire sensors to I2C bus 0: SDA → GP13, SCL → GP14, VCC → 3.3 V, GND → GND (AHT20 and BMP280 in parallel).
2. Wire LCD1602 to I2C bus 1: SDA → GP33, SCL → GP34, VCC → 5 V (or 3.3 V), GND → GND.
3. Make sure BMP280 SDO matches the address in the sketch (0x77 = SDO to 3.3 V; 0x76 = SDO to GND).
4. Open `Lesson31` and click **Upload → Run only**.
5. The LCD shows `AHT20+BMP280 / ready...` briefly, then updates every 2 s with live readings.

## LCD output format

```
Line 1:  AHT:22.4C 51.3%
Line 2:  BMP:22.7C 1013h
```

## Wiring overview

```
ESP32-S3 Pico    I2C0 (sensors)          I2C1 (display)
   GP13 ─────── SDA (AHT20 + BMP280)
   GP14 ─────── SCL (AHT20 + BMP280)
   GP33 ──────────────────────────────── SDA (LCD1602)
   GP34 ──────────────────────────────── SCL (LCD1602)
   3V3  ─────── VCC (AHT20 + BMP280)
   5V   ──────────────────────────────── VCC (LCD1602)
   GND  ─────── GND (all modules) ───────────────────
```

> **Why two I2C buses?** All three modules can share one bus since their addresses don't clash — but running sensors and display on separate buses keeps wiring cleaner and makes it trivial to add more modules later without worrying about conflicts.
