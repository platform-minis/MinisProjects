# ESP32 DashPro — CYD Dashboard

Real-time web dashboard hosted on the **ESP32-2432S028** (Cheap Yellow Display) using the free [ESP-DASH](https://github.com/ayushsharma82/ESP-DASH) library.

## How it works

1. The ESP32 connects to your WiFi network
2. It starts an HTTP server on port **80** with the ESP-DASH web UI
3. The TFT display shows the URL — open it in any browser on the same network
4. Live cards update every **2 seconds** via WebSocket

## Dashboard cards

| Card | Source |
|------|--------|
| Temperature (°C) | Simulated drift ±0.1°C |
| Humidity (%) | Simulated drift |
| WiFi Signal (dBm) | Real — `WiFi.RSSI()` |
| Uptime (s) | Real — `millis()` |

## CYD pinout used

| Signal | GPIO |
|--------|------|
| TFT SCK | 14 |
| TFT MOSI | 13 |
| TFT CS | 15 |
| TFT DC | 2 |
| Backlight | 21 |
| RGB LED (R/G/B) | 17 / 4 / 16 |

Green LED lights up when the dashboard is ready.

## Extending the sketch

Replace the simulated values with real sensors — connect a DHT22 to GPIO 27 (free pin on CYD) and swap in `dht.readTemperature()` / `dht.readHumidity()`.
