# ESP32-S3 uPython Curses

A **MicroPython course** for the ESP32-S3 Pico, structured like a programming tutorial. Each lesson introduces new concepts — GPIO control, button input, PWM, and RGB LED animations — using the MyCastle visual Blockly editor or raw Python code.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB OPI PSRAM)
- MicroPython firmware flashed (use the MyCastle Flash tool)
- LEDs, resistors, tactile button for later lessons

## Skill level

⭐ Beginner — no prior MicroPython experience required. Lessons build on each other progressively.

## What's included

| Lesson    | Topic                                                            |
|-----------|------------------------------------------------------------------|
| `Lesson1` | GPIO output — turn an LED on                                     |
| `Lesson2` | GPIO timing — blink an LED                                       |
| `Lesson3` | Multiple outputs — traffic light with 3 LEDs                     |
| `Lesson4` | Digital input — external button controls an LED                  |
| `Lesson5` | RGB LED animations (5 modes) — ext button GP16                   |
| `Lesson9` | PWM — passive buzzer melody                                      |

## Quick start

1. Flash MicroPython firmware using the **Flash** button on the project page.
2. Open `Lesson1` in the Blockly editor.
3. Connect an LED through a 330 Ω resistor from GP11 to GND.
4. Click **Upload → Run only** — the LED turns on.
5. Work through the lessons in order; each one builds on the previous.

## Lesson 5 wiring

Lesson5 combines the on-board WS2812B RGB LED (GP21) with an external button on GP16. Wire the button as in Lesson4:

```text
3.3 V ──── Button ──── GP16
                  │
              R 10 kΩ (pull-down)
                  │
                 GND
```

Short press = next animation · Long press (≥ 500 ms) = previous animation.

## How the course works

Each lesson comes with both a `.blockly` file (visual blocks) and a generated `.py` file. Edit visually in MyCastle, run directly on the device over Serial or WebREPL, and view the output in the built-in terminal.
