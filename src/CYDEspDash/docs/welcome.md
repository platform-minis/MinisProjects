# CYD ESP-DASH Dashboard

Real-time web dashboard na **ESP32-2432S028** (Cheap Yellow Display) z biblioteką [ESP-DASH](https://github.com/ayushsharma82/ESP-DASH).

## Jak to działa

ESP32 hostuje dashboard na porcie **80** — otwórz `http://<IP>` w przeglądarce na tej samej sieci WiFi. TFT pokazuje adres URL zaraz po połączeniu.

## Karty dashboardu

| Karta | Typ | Wartość |
|-------|-----|---------|
| Temperature | `TEMPERATURE_CARD` | symulowany drift ±0.1°C |
| Humidity | `HUMIDITY_CARD` | symulowany drift |
| WiFi Signal | `GENERIC_CARD` | realny `WiFi.RSSI()` |
| Uptime | `GENERIC_CARD` | realny `millis()` |
| Board | `STATUS_CARD` | "Online" zielony |
| Green LED | `BUTTON_CARD` | toggle zielonej diody |
| Backlight | `SLIDER_CARD` | jasność TFT 10–100% (PWM) |
| Temperature History | `BAR_CHART` | ostatnie 10 odczytów |

## Interakcja

- **LED button** — tapnięcie w przeglądarce włącza/wyłącza zieloną diodę (GPIO 4)
- **Backlight slider** — suwak w przeglądarce steruje jasnością podświetlenia TFT przez LEDC PWM

## Pinout TFT (ILI9341)

| Sygnał | GPIO |
|--------|------|
| SCK | 14 |
| MOSI | 13 |
| CS | 15 |
| DC | 2 |
| Backlight (PWM) | 21 |

## Biblioteki

- **ESP-DASH** — `https://github.com/ayushsharma82/ESP-DASH`
- **ESPAsyncWebServer** — `https://github.com/mathieucarbou/ESPAsyncWebServer`
- **AsyncTCP** — `https://github.com/mathieucarbou/AsyncTCP`
- **Adafruit ILI9341** + **Adafruit GFX** — Arduino Library Manager
