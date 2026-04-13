# ESP32-S3 Zero Test (Arduino)

A minimal Arduino "Hello World" for the ultra-compact **Waveshare ESP32-S3 Zero** — the smallest ESP32-S3 board (23 × 18 mm). Prints system info over USB-CDC and blinks the built-in RGB LED on GPIO 21.

## What you need

- **Waveshare ESP32-S3 Zero** (4 MB Flash, 2 MB OPI PSRAM, native USB)
- USB cable (USB-C)
- Arduino IDE 2 with the `esp32` board package

## Skill level

⭐ Beginner — ideal first sketch for a new ESP32-S3 Zero board.

## What's included

| Sketch | Description |
|--------|-------------|
| `Test1` | Prints CPU speed, flash/PSRAM info, blinks LED GPIO 21 |

## Quick start

1. Open the `Test1` sketch.
2. Board settings:
   - **Board:** ESP32S3 Dev Module
   - **USB Mode:** Hardware CDC and JTAG
   - **CDC On Boot:** Enabled
   - **PSRAM:** OPI PSRAM
   - **Flash Size:** 4MB
3. Compile and flash — hold BOOT if the port is not detected.
4. Open Serial Monitor. Expected output:
   ```
   ESP32-S3 Zero - Hello!
   CPU freq:   240 MHz
   Flash size: 4 MB
   Free heap:  327680 bytes
   PSRAM size: 2 MB
   Running...
   ```

## Board specifics

| Feature | Value |
|---------|-------|
| Size | 23 × 18 mm |
| Flash | 4 MB |
| PSRAM | 2 MB OPI (required!) |
| USB | Native HWCDC (no USB-UART chip) |
| Built-in LED | GPIO 21 |
| Flash address | `0x0` (not `0x1000` like classic ESP32) |
