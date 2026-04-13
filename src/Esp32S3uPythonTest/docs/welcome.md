# ESP32-S3 uPython Test

A **MicroPython test project** for the ESP32-S3 Pico that covers core language features through runnable sketches. Each sketch demonstrates one concept — variables, loops, logic, math, bits — using the MyCastle Blockly editor. Good for verifying your MicroPython setup and learning the Blockly-to-Python workflow.

## What you need

- **ESP32-S3 Pico** (Waveshare) with MicroPython firmware
- LED + 330 Ω resistor (GPIO 11) for sketches that blink

## Skill level

⭐ Beginner — each sketch is self-contained and independently runnable.

## What's included

| Sketch | Description |
|--------|-------------|
| `test1` | Blink GPIO 11, print counter over Serial |
| `Logic` | Boolean logic operators and `if/else` branching |
| `Loop` | `while` and `for` loop patterns |
| `Math` | Arithmetic, modulo, and `math` module functions |
| `Bits` | Bitwise operators: AND, OR, XOR, shift |

## Quick start

1. Flash MicroPython firmware using the **Flash** button in MyCastle.
2. Open `test1` in the Blockly editor.
3. Connect an LED (GPIO 11 → 330 Ω → GND).
4. Click **Run** — LED blinks and counter prints in the REPL terminal.
5. Explore other sketches in any order.

## The Blockly ↔ Python workflow

Every sketch stores both a `.blockly` file (the visual block program) and the generated `.py` file. You can:

- **Edit visually** — drag blocks in the MyCastle editor, code regenerates automatically
- **Edit code** — switch to the Code view, edit Python directly, changes are preserved
- **Run** — uploads to the device via Serial or WebREPL and executes immediately

## Key features

- Each sketch is < 30 lines of Python — easy to read and modify
- Covers all fundamental programming structures used in larger MicroPython projects
- Blockly XML preserved alongside Python for easy visual editing later
