# ESP32-S3 uPython Curses 3

A **continuation of the ESP32-S3 MicroPython course** — the same seven sensors and modules as Part 2, but all output is now displayed on an **LCD1602 I2C display** (SDA=GP21, SCL=GP22) instead of the terminal. If you haven't completed part 2 (Esp32S3uPythonCurses2), start there first.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB OPI PSRAM)
- MicroPython firmware flashed (use the MyCastle Flash tool)
- **LCD1602 display with PCF8574 I2C backpack** (address 0x27)
- MAX7219 8×8 LED matrix module
- KY-018 photoresistor module
- DHT11 temperature & humidity sensor module
- HC-SR04 ultrasonic distance sensor
- HC-SR501 PIR motion sensor module
- RC-522 RFID reader module + RFID card or key fob
- PS2 joystick module (KY-023 or similar)
- Jumper wires

## Skill level

⭐⭐ Beginner–Intermediate — basic MicroPython experience required. Completing Parts 1 and 2 is recommended.

## What's included

| Lesson      | Topic                                                                    |
|-------------|--------------------------------------------------------------------------|
| `Lesson13`  | MAX7219 + LCD — pattern cycling, pattern name on LCD1602                 |
| `Lesson14`  | KY-018 + LCD — raw ADC value and category on LCD1602                     |
| `Lesson15`  | DHT11 + LCD — temperature and humidity on LCD1602                        |
| `Lesson16`  | HC-SR04 + LCD — distance measurement on LCD1602                          |
| `Lesson17`  | HC-SR501 + LCD — PIR motion status (DETECTED / waiting) on LCD1602       |
| `Lesson18`  | RC-522 + LCD — RFID card UID on LCD1602                                  |
| `Lesson19`  | Joystick + LCD — axes, direction and button state on LCD1602             |

## LCD1602 wiring (all lessons)

The LCD1602 I2C backpack uses the same two wires in every lesson:

```
ESP32-S3 Pico         LCD1602 I2C
┌──────────────┐     ┌──────────────────┐
│        GP21  ├─────┤ SDA              │
│        GP22  ├─────┤ SCL              │
│         3V3  ├─────┤ VCC              │
│         GND  ├─────┤ GND              │
└──────────────┘     └──────────────────┘
```

## Quick start — Lesson 13

1. Wire the LCD1602: SDA → GP21, SCL → GP22, VCC → 3.3 V, GND → GND.
2. Wire the MAX7219: CLK → GP18, DIN → GP19, CS → GP5, VCC → 3.3 V, GND → GND.
3. Open `Lesson13` and click **Upload → Run only**.
4. The matrix cycles through four patterns; the LCD shows the pattern name.

## Quick start — Lesson 15

1. Wire the LCD1602 as above.
2. Wire DHT11: DATA → GP3, VCC → 3.3 V, GND → GND.
3. Open `Lesson15` and click **Upload → Run only**.
4. The LCD shows temperature on line 1 and humidity on line 2, updated every 2 s.

## Quick start — Lesson 17

1. Wire the LCD1602 as above.
2. Wire HC-SR501: OUT → GP2, VCC → 5V (VBUS), GND → GND.
3. Open `Lesson17` and click **Upload → Run only**.
4. Wait ~30 s for the sensor to stabilise. Line 2 shows `DETECTED!` on motion, `waiting...` otherwise.

## Quick start — Lesson 18

1. Wire the LCD1602 as above.
2. Wire RC-522: SCK → GP18, MOSI → GP19, MISO → GP16, SDA → GP17, RST → GP15, 3V3 → 3.3 V, GND → GND.
3. Open `Lesson18` and click **Upload → Run only**.
4. Hold an RFID card near the reader — the UID appears on line 2 of the LCD.

## Quick start — Lesson 19

1. Wire the LCD1602 as above.
2. Wire joystick: VRx → GP1, VRy → GP6, SW → GP8, VCC → 3.3 V, GND → GND.
3. Open `Lesson19` and click **Upload → Run only**.
4. Line 1 shows live X/Y values; line 2 shows the direction and `[BTN]` when pressed.
