# Esp32DashPro

Real-time web dashboard for the **ESP32-2432S028** (Cheap Yellow Display) using the free [ESP-DASH](https://github.com/ayushsharma82/ESP-DASH) library.

The ESP32 serves a dashboard at its IP address — no cloud, no phone app, just open the URL in any browser on the same WiFi.

## Hardware

**ESP32-2432S028 (CYD)**

| Component | Details |
|-----------|---------|
| MCU | ESP32 (Xtensa dual-core 240 MHz) |
| Display | ILI9341 2.8" IPS 320×240 (SPI1) |
| Touch | XPT2046 resistive (unused here) |
| RGB LED | GPIO 17/4/16, active-low |

## Libraries

| Library | Source |
|---------|--------|
| ESP-DASH (free) | https://github.com/ayushsharma82/ESP-DASH |
| ESPAsyncWebServer | https://github.com/me-no-dev/ESPAsyncWebServer |
| AsyncTCP | https://github.com/me-no-dev/AsyncTCP |
| Adafruit ILI9341 | Arduino Library Manager |
| Adafruit GFX | Arduino Library Manager |

## Sketch: CYDDashboard

**src/CYDDashboard/CYDDashboard.ino**

1. Connects to WiFi (credentials from `MinisConfig.h` or hardcoded fallback)
2. Starts ESP-DASH server on port 80
3. Shows `http://<IP>` on the TFT in landscape mode
4. Updates four live cards every 2 s:
   - Temperature & Humidity — simulated (replace with DHT/BME sensor)
   - WiFi RSSI — real
   - Uptime — real

## Flashing

Flash with the standard `esp32:esp32:esp32dev` board definition.  
No extra `User_Setup.h` needed — ILI9341 pins are passed directly to `Adafruit_ILI9341`.

## Extending

Add a real sensor (e.g. DHT22 on GPIO 27) and swap the simulated values:

```cpp
#include <DHT.h>
DHT dht(27, DHT22);
// ...
temperature.update(dht.readTemperature());
humidity.update(dht.readHumidity());
```
