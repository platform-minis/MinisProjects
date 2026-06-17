# CYDEspDash

Real-time web dashboard for **ESP32-2432S028** (Cheap Yellow Display) using the [ESP-DASH](https://github.com/ayushsharma82/ESP-DASH) library (free version).

## Dashboard cards

| Card | Type | Notes |
|------|------|-------|
| Temperature | `TEMPERATURE_CARD` | simulated drift |
| Humidity | `HUMIDITY_CARD` | simulated drift |
| WiFi Signal | `GENERIC_CARD` | real RSSI |
| Uptime | `GENERIC_CARD` | real millis() |
| Board | `STATUS_CARD` | "Online" green |
| Green LED | `BUTTON_CARD` | toggles GPIO 4 via `attachCallback` |
| Backlight | `SLIDER_CARD` | PWM 10–100 % via LEDC |
| Temp History | `BAR_CHART` | rolling 10-sample bar chart |

## CYD pinout used

| Signal | GPIO |
|--------|------|
| TFT SCK | 14 |
| TFT MOSI | 13 |
| TFT MISO | 12 |
| TFT CS | 15 |
| TFT DC | 2 |
| Backlight (PWM) | 21 |
| RGB LED R/G/B | 17 / 4 / 16 (active-low) |

## Libraries

| Library | Install |
|---------|---------|
| Adafruit ILI9341 | Arduino Library Manager |
| Adafruit GFX Library | Arduino Library Manager |
| ESP-DASH | git-url: `https://github.com/ayushsharma82/ESP-DASH` |
| AsyncTCP | git-url: `https://github.com/mathieucarbou/AsyncTCP` |
| ESPAsyncWebServer | git-url: `https://github.com/mathieucarbou/ESPAsyncWebServer` |

## Extending

Swap the simulated sensor values for real ones (DHT22 on GPIO 27, free pin on CYD):

```cpp
#include <DHT.h>
DHT dht(27, DHT22);
// in loop:
cardTemp.update(dht.readTemperature());
cardHum.update(dht.readHumidity());
```
