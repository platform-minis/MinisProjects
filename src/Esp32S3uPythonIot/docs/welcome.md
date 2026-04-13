# ESP32-S3 uPython IoT

The go-to **MyCastle IoT starter project** for MicroPython. The device connects to WiFi, registers with the MyCastle platform, streams sensor telemetry every 10 seconds, and exposes its filesystem over MQTT — all with ~50 lines of Python.

## What you need

- **ESP32-S3 Pico** (Waveshare) with MicroPython firmware
- MyCastle server running on your local network
- **ST7789 TFT display** (optional — shows live readings on the device)
  - SCK GPIO 12 · MOSI GPIO 11 · CS GPIO 10 · DC GPIO 9 · RST GPIO 8 · BL GPIO 2

## Skill level

⭐⭐ Intermediate — basic MicroPython familiarity helpful.

## What's included

| Sketch | Description |
|--------|-------------|
| `basic_sensor` | Connects to MyCastle, sends temperature & humidity, exposes VFS, drives ST7789 display |

## Quick start

1. Flash MicroPython and import this project in MyCastle.
2. Select your device in the Config panel.
3. Click **Run** — the device connects to WiFi and starts publishing telemetry.
4. Open **IoT → Dashboard** in MyCastle to see live sensor graphs.
5. Open **IoT → Device** page — click the VFS icon to browse the device filesystem.

## What the `basic_sensor` sketch does

```
Boot → WiFi connect → MinisIoT hello
     → every 10 s: read sensors → publish telemetry
     → VFS server: respond to file system requests over MQTT
     → ST7789: display current readings
```

The sensor values in the default sketch are **simulated** (random within realistic ranges). Replace the `read_sensors()` function with real hardware reads (DHT22, SHT31, BMP280, etc.).

## Key features

- Automatic device registration with MyCastle on first connect
- Real-time telemetry visible in IoT Dashboard and device graphs
- Remote filesystem access (read, write, delete files) via the MyCastle VFS explorer
- Display support: ST7789 TFT shows temperature and humidity
- WiFi credentials injected automatically by MyCastle on deploy
