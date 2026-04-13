# ESP32-S3 uPython Virtual Display

Stream live graphics from your ESP32-S3 to the **MyCastle Virtual Display** widget in the browser. The device draws into a framebuffer using a familiar API (fill, rect, hline, text) and transmits each frame over MQTT — no physical display wired to the microcontroller required.

## What you need

- **ESP32-S3 Pico** (Waveshare) with MicroPython firmware
- MyCastle server running and reachable over WiFi
- `minis_iot.py` and `minis_display.py` libraries (deployed automatically via MyCastle)

## Skill level

⭐⭐ Intermediate — requires a running MyCastle IoT setup.

## What's included

| Sketch | Description |
|--------|-------------|
| `hello_display` | Border, title and live counter — minimal display example |
| `primitives` | Demonstrates all drawing primitives: lines, rects, circles, text |
| `clock_display` | Real-time clock face driven by the device RTC |
| `sensor_bars` | Horizontal bar chart of simulated sensor readings + IoT telemetry |
| `animation` | Simple sprite animation loop |

## Quick start

1. Import this project in MyCastle (uPython platform).
2. In the project Config, select your ESP32-S3 device.
3. Open the `hello_display` sketch and click **Run**.
4. Navigate to **IoT → Virtual Display** for your device in MyCastle.
5. You should see the live frame updating every 3 seconds.

## How it works

```python
display = Display(width=128, height=64, format='MONO_VLSB')
display.fill(0)
display.text("Hello!", 10, 10)
display.show()   # transmits the frame via MQTT
```

The `show()` call encodes the framebuffer and publishes it to `minis/{user}/{device}/ext/display/res`. The MyCastle browser decodes and renders the frame in real time, supporting RGB565, MONO\_VLSB, MONO\_HLSB, GS4\_HMSB, and GS8 pixel formats.

## Key features

- No physical display required — everything rendered in the browser
- Supports greyscale and monochrome framebuffer modes
- Publishes IoT telemetry and display frames simultaneously
- Zoom 1×–8× and background colour toggle in the Virtual Display viewer
