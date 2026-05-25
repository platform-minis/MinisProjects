# ESP32-S3 Zero uPython Audio

Naucz się generować dźwięk na **ESP32-S3 Zero** (Waveshare) przy użyciu wzmacniacza I2S **MAX98357A**. Trzy lekcje prowadzą od prostego sygnału dźwiękowego przez gamę muzyczną aż do pełnej melodii — bez żadnych zewnętrznych bibliotek.

## Potrzebujesz

- ESP32-S3 Zero (Waveshare) z MicroPython
- Moduł MAX98357A (wzmacniacz klasy D, I2S)
- Głośnik 4–8 Ω, 0.5–3 W
- 4 przewody połączeniowe

## Połączenia

```
ESP32-S3 Zero   MAX98357A
GP4  ────────►  BCLK
GP5  ────────►  LRC
GP6  ────────►  DIN
GND  ────────►  GND
3.3V ────────►  VIN
```

> LED wbudowany w ESP32-S3 Zero (GP21) nie koliduje z pinami audio.

## Lekcje

### 🔊 Lesson 40 — Beep
Trzy sygnały 440 Hz (nuta A4) po 500 ms każdy.  
Idealny pierwszy test — jeśli słyszysz dźwięk, połączenie działa.

### 🎵 Lesson 41 — Gama C-dur
Osiem nut od C4 do C5 odgrywanych kolejno.  
Poznaj tablicę nut i funkcję `_note(name, ms)`.

### 🎶 Lesson 42 — Twinkle Twinkle
Pełna melodia „Twinkle Twinkle Little Star" z ćwierćnutami i półnutami.  
Melodia jest zakodowana jako lista krotek `(nuta, czas_ms)`.

## Jak działa generator dźwięku

MAX98357A odbiera cyfrowy sygnał audio przez magistralę I2S:
- **BCLK** — zegar bitowy (GP4)
- **LRC** — zegar ramki (GP5)
- **DIN** — dane PCM 16-bit mono 22 050 Hz (GP6)

ESP32-S3 Zero oblicza próbki fali sinusoidalnej i wysyła je przez sprzętowy kontroler I2S. MAX98357A zamienia sygnał cyfrowy na analogowy i wzmacnia go bezpośrednio do głośnika.
