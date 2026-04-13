# ESP32-S3 Display VGA

Drives a **640×480 @ 60 Hz VGA monitor** from an ESP32-S3 using the hardware LCD\_CAM block with DMA — no CPU cycles wasted on pixel output. Delivers full **8-bit colour** (R3G3B2, 256 colours) through the luniVGA library.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB OPI PSRAM — required)
- **Waveshare VGA PS2 Board** (or custom resistor DAC)
- VGA monitor
- 10 jumper wires

## Skill level

⭐⭐⭐ Advanced — requires understanding of DMA-driven peripherals and VGA timing.

## What's included

| Sketch | Description |
|--------|-------------|
| `luniVGA` | Full 8-bit colour VGA demo with animated pixel patterns |

## Quick start

1. Connect 8 colour pins (R2-R0, G2-G0, B1-B0), HSYNC and VSYNC to the VGA board.  
   Default pinout: GPIO 4–11 for data, GPIO 2 HSYNC, GPIO 3 VSYNC.
2. Open the `luniVGA` sketch and verify the pin definitions match your wiring.
3. Select board **ESP32-S3** with **PSRAM: OPI** in Arduino IDE.
4. Compile and flash — a colour test pattern appears immediately.

## Key API

```cpp
vgaStart();           // initialise DMA + LCD_CAM
vgaDot(x, y, color);  // draw a pixel (R3G3B2)
vgaShow();            // push framebuffer to screen
```

## Key features

- Hardware VGA generation — LCD\_CAM + DMA, no bit-banging
- 640×480 resolution, 60 Hz refresh rate
- 256-colour palette (R3G3B2 encoding)
- Double-buffered framebuffer to eliminate tearing
- Based on the open-source [luniVGA](https://github.com/bitluni/ESP32-S3-VGA) library
