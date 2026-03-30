# uPython CheapYellowDisplay (CYD)

MicroPython examples for the **ESP32-2432S028** ("CheapYellowDisplay") — a popular all-in-one ESP32 board from AliExpress featuring a 2.8" ILI9341 TFT, XPT2046 resistive touch, RGB LED, LDR, and SD card slot.

## Hardware pinout

| Peripheral | Signal | GPIO |
|-----------|--------|------|
| **Display** ILI9341 | MOSI | 13 |
| | MISO | 12 |
| | CLK  | 14 |
| | CS   | 15 |
| | DC   | 2  |
| | BL (backlight) | 21 |
| **Touch** XPT2046 | MOSI | 32 |
| | MISO | 39 *(input-only → SoftSPI)* |
| | CLK  | 25 |
| | CS   | 33 |
| | IRQ  | 36 |
| **RGB LED** (active LOW) | R | 4 |
| | G | 16 |
| | B | 17 |
| **LDR** (light sensor) | ADC | 34 |

> **Note:** GPIO 39 is input-only on ESP32.
> The touch bus **must** use `machine.SoftSPI` — hardware SPI will not work.

## Included libraries

Deployed automatically by MyCastle to every sketch in this project:

| File | Description |
|------|-------------|
| `ili9341.py` | ILI9341 display driver — fill, rect, pixel, hline, vline, text (8×8 built-in font with integer scaling) |
| `xpt2046.py` | XPT2046 touch driver — `read()` returns `(x, y)` or `None`, linear calibration |

## Sketches

### hello_display
Basic demo: cycles through background colours, draws text and a rainbow bar of rectangles, reads the LDR, blinks the RGB LED to match the current colour.

### iot_dashboard
IoT dashboard integrated with MyCastle:
- Reads the LDR every 10 s and sends `light` telemetry via MQTT
- Displays the reading with a proportional bar, connection status badge, and sent counter
- Exposes the device filesystem over MQTT (VFS extension)
- RGB LED: green = online, red = offline

Requires `minis_iot.py` and `minis_vfs.py` — add the [Esp32S3uPythonIot](../Esp32S3uPythonIot) project libraries or deploy them separately.

### touch_demo
Touchscreen paint app:
- Draw anywhere on the canvas with your finger
- Tap the colour palette row at the top to change pen colour
- Tap the first (red) button to clear the canvas

## Quick start

1. In MyCastle, create a new device with module **ESP32 DevKit-C**
2. Open the **uPython CheapYellowDisplay** project and pick a sketch
3. Click **Deploy** — `ili9341.py` and `xpt2046.py` are uploaded automatically
4. Open the serial monitor to watch connection logs

## Calibrating the touchscreen

If touch coordinates are off, measure the raw values at two corners in the REPL:

```python
from xpt2046 import XPT2046
import machine

tspi = machine.SoftSPI(baudrate=1_000_000,
                       sck=machine.Pin(25),
                       mosi=machine.Pin(32),
                       miso=machine.Pin(39))
t = XPT2046(tspi, cs=machine.Pin(33, machine.Pin.OUT),
            irq=machine.Pin(36, machine.Pin.IN))

# Hold finger in top-left corner, run:
print(t._raw())   # note as tl_raw

# Hold finger in bottom-right corner, run:
print(t._raw())   # note as br_raw

# Update calibration:
t.calibrate(tl_raw=(x_min, y_min), br_raw=(x_max, y_max))
```
