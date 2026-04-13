# uPython Cheap Yellow Display (CYD)

MicroPython examples for the popular **ESP32-2432S028** — an all-in-one ~$10 board nicknamed the "Cheap Yellow Display". It packs a 2.8″ ILI9341 TFT (320×240), XPT2046 resistive touchscreen, RGB LED, light sensor and SD card slot onto a single PCB.

## What you need

- **ESP32-2432S028** ("CYD") board — available from AliExpress / Aliexpress
- MicroPython firmware for ESP32 (standard, not S3)
- USB cable (Micro-USB)

## Skill level

⭐⭐ Intermediate — drivers are included, basic MicroPython knowledge needed.

## What's included

| Sketch | Description |
|--------|-------------|
| `hello_display` | Fills screen with colour bands, prints "Hello CYD!" — verifies display wiring |
| `iot_dashboard` | Connects to MyCastle via MQTT, shows live sensor telemetry on screen |
| `touch_demo` | Finger-paint app — tap and drag to draw, tap RGB LED button to change colour |

## Quick start

1. Flash standard MicroPython for ESP32 via the MyCastle Flash tool.
2. Import this project (uPython platform, module: ESP32 DevKit-C).
3. Open `hello_display` and click **Run**.
4. The screen should show coloured horizontal bands — display is working.
5. Try `touch_demo` next: tap anywhere to draw, tap the LED area to change pen colour.

## Board pinout (built-in)

| Peripheral | Pins |
|------------|------|
| ILI9341 display | SPI: MOSI 13 · CLK 14 · CS 15 · DC 2 · BL 21 |
| XPT2046 touch | SPI: MOSI 32 · MISO 39 · CLK 25 · CS 33 · IRQ 36 |
| RGB LED | R 4 · G 16 · B 17 |
| LDR light sensor | GPIO 34 (ADC) |
| SD card | SPI: MOSI 23 · MISO 19 · CLK 18 · CS 5 |

## Key features

- Bundled ILI9341 and XPT2046 drivers — no extra libraries to install
- Touch calibration built into the XPT2046 driver
- IoT dashboard sketch connects directly to MyCastle for live data
- All-in-one form factor — display, touch, LED and sensors in one board
