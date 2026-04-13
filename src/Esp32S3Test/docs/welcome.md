# ESP32-S3 Test (Arduino)

A minimal Arduino "Hello World" for the **ESP32-S3 Pico** that prints system information over USB-CDC Serial. Use it to verify your toolchain, board config and PSRAM settings before tackling any real project.

## What you need

- **ESP32-S3 Pico** (Waveshare) — or any ESP32-S3 board
- USB cable (USB-C)
- Arduino IDE 2 with the `esp32` board package

## Skill level

⭐ Beginner — flash and read Serial output.

## What's included

| Sketch | Description |
|--------|-------------|
| `Test1` | Prints CPU speed, flash size and free heap over Serial every second |

## Quick start

1. Open the `Test1` sketch.
2. Select board **ESP32-S3** → set **USB Mode: Hardware CDC**, **PSRAM: OPI**.
3. Compile and flash.
4. Open Serial Monitor at **115200 baud** (or any baud — it's USB CDC).
5. You should see:
   ```
   ESP32-S3 Test - Hello!
   CPU freq: 240 MHz
   Flash size: 8 MB
   Free heap: 367824 bytes
   Running...
   ```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| No Serial output | Wait up to 3 s after flashing for USB CDC to enumerate |
| Crash on boot | Make sure **PSRAM: OPI** is selected |
| "Port not found" | Hold BOOT button while plugging in, then flash |
