# ESP32-S3 Display VGA 1-bit

The simplest possible VGA output from an ESP32-S3 — **monochrome 640×480 @ 60 Hz** using only **3 data pins** plus HSYNC and VSYNC. Perfect for text terminals, oscilloscope traces, or learning how VGA timing works.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB OPI PSRAM — required)
- **Waveshare VGA PS2 Board** (or custom resistor network)
- VGA monitor
- 5 jumper wires (vs 10 for the 8-bit version)

## Skill level

⭐⭐⭐ Advanced — same DMA/LCD\_CAM architecture as the 8-bit version, great starting point to understand VGA generation.

## What's included

| Sketch | Description |
|--------|-------------|
| `luniVGA` | Monochrome VGA demo — 8×8 checkerboard pattern |

## Quick start

1. Connect only the **MSB** of each colour channel to the VGA board:  
   GPIO 4 → R (MSB), GPIO 7 → G (MSB), GPIO 9 → B (MSB), GPIO 2 → HSYNC, GPIO 3 → VSYNC.
2. Tie remaining colour pins to GND on the VGA board.
3. Select board **ESP32-S3** with **PSRAM: OPI**.
4. Compile, flash — a black-and-white checkerboard confirms correct timing.

## How it differs from the 8-bit version

| Feature | VGA 1-bit | VGA 8-bit |
|---------|-----------|-----------|
| Data pins | 3 | 8 |
| Colours | 2 (black/white) | 256 (R3G3B2) |
| Wiring | Minimal | Full DAC resistors |

## Key features

- Uses the same hardware LCD\_CAM + DMA pipeline as the 8-bit version
- Minimal wiring — ideal for breadboard experiments
- Identical framebuffer API — easy upgrade path to colour by adding 5 pins
