# ESP32-S3 uPython Curses

A **12-lesson MicroPython course** for the ESP32-S3 Pico, structured like a programming tutorial. Each lesson introduces new concepts — GPIO control, loops, timers, UART communication and more — using the MyCastle visual Blockly editor or raw Python code.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB OPI PSRAM)
- MicroPython firmware flashed (use the MyCastle Flash tool)
- LEDs, buttons, resistors for later lessons (optional)

## Skill level

⭐ Beginner — no prior MicroPython experience required. Lessons build on each other progressively.

## What's included

| Lesson | Topic |
|--------|-------|
| `Lesson1` | GPIO output — turn an LED on |
| `Lesson2` | GPIO input — read a button |
| `Lesson3` | Loops and counters |
| `Lesson4` | Variables and print output |
| `Lesson5` | Timers and delays |
| `Lesson6` | Functions |
| `Lesson7` | Lists and iteration |
| `Lesson8` | Conditional logic |
| `Lesson9` | PWM — LED brightness control |
| `Lesson10` | Analog input (ADC) |
| `Lesson11` | UART communication |
| `Lesson12` | Putting it all together |

## Quick start

1. Flash MicroPython firmware using the **Flash** button on the project page.
2. Open `Lesson1` in the Blockly editor.
3. Connect an LED through a 330 Ω resistor from GPIO 11 to GND.
4. Click **Run** — the LED turns on.
5. Modify the code in subsequent lessons by dragging blocks or editing Python directly.

## How the course works

Each lesson comes with both a `.blockly` file (visual blocks) and a generated `.py` file. Edit visually in MyCastle, run directly on the device over Serial or WebREPL, and view the output in the built-in terminal.
