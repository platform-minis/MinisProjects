# CYDTest

MicroPython [micro-gui](https://github.com/peterhinch/micropython-micro-gui) demo for the **ESP32-2432S028** (Cheap Yellow Display).

## Hardware

| Component | Chip | Interface |
|-----------|------|-----------|
| 2.8" TFT 320×240 | ILI9341 | SPI1 (VSPI) |
| Resistive touch | XPT2046 | SoftSPI |
| RGB LED | — | GPIO 17/4/16 (active-low) |
| LDR | — | GPIO 34 (ADC) |

### Display pinout (ILI9341)

| Signal | GPIO |
|--------|------|
| SCK | 14 |
| MOSI | 13 |
| MISO | 12 |
| CS | 15 |
| DC | 2 |
| Backlight | 21 |

## Library

[peterhinch/micropython-micro-gui](https://github.com/peterhinch/micropython-micro-gui) — a GUI framework for MicroPython colour displays. This project uses the **nanogui** (display-only) subset: no input drivers, just rendering.

Files deployed automatically by MyCastle:

```
drivers/display/ili9341.py   ← micro-gui ILI9341 driver
gui/core/nanogui.py          ← framebuf rendering engine
gui/core/writer.py           ← proportional font writer
gui/widgets/label.py         ← Label widget
gui/widgets/button.py        ← Button widget (for future use)
gui/fonts/freesans20.py      ← 20 px FreeSans font
```

## Sketches

### CYDTest

Basic widget demo — shows labels at different colours on the display.

Files deployed to device:
- `CYDTest.py` — main script
- `color_setup.py` — hardware setup (SPI, display, backlight)

## Flashing MicroPython

Download the latest ESP32 MicroPython firmware from [micropython.org/download/ESP32_GENERIC](https://micropython.org/download/ESP32_GENERIC/) and flash with:

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash -z 0x1000 ESP32_GENERIC-*.bin
```
