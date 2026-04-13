# ESP32-S3 Zero uPython Test

A minimal MicroPython "Hello World" for the ultra-compact **Waveshare ESP32-S3 Zero**. Blinks the built-in LED on GPIO 21 and prints a counter over USB Serial — the perfect first sketch to verify your Zero board and MicroPython firmware.

## What you need

- **Waveshare ESP32-S3 Zero** (4 MB Flash, 2 MB OPI PSRAM, native USB)
- MicroPython firmware: `ESP32_GENERIC_S3-SPIRAM_OCT` (required for OPI PSRAM)
- USB cable (USB-C)

## Skill level

⭐ Beginner — just flash and run.

## What's included

| Sketch | Description |
|--------|-------------|
| `test1` | Blinks LED GPIO 21, prints counter over USB Serial |

## Quick start

1. Flash MicroPython firmware using the MyCastle **Flash** tool.  
   Select firmware: `ESP32_GENERIC_S3-SPIRAM_OCT` · Flash address: `0x0`
2. Import this project in MyCastle (uPython platform, module: ESP32-S3 Zero).
3. Open `test1` and click **Run**.
4. Open the REPL terminal — you should see:
   ```
   ESP32-S3 Zero - Hello!
   0
   1
   2
   ...
   ```
   The built-in LED blinks once per second.

## Important: Zero-specific details

| Item | Value |
|------|-------|
| Built-in LED | GPIO 21 (not GPIO 2 like classic ESP32) |
| Flash firmware | `ESP32_GENERIC_S3-SPIRAM_OCT` (SPIRAM required) |
| Flash address | `0x0` (S3 bootloader differs from classic ESP32) |
| USB | Native HWCDC — no USB-UART chip |

> **Note:** Using the wrong firmware (without SPIRAM\_OCT) will cause the board to crash on boot due to the OPI PSRAM initialisation failing.
