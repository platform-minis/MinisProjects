# Project: MinisProjects

## Overview
Kolekcja projektów DIY. Głównie z zakresu elektroniki.

## Key Objectives / Current Focus
Baza wiedzy o moich projektach DIY.

## Directory Structure
- `src/` : Katalog z projektami
    - src/{NazwaProjektu} - katalog z projektem o nazwie NazwaProjektu
        - `src/` - kody źródłowe projektu
        - `tests/`: Automated tests
        - `docs/`: dokumentacja projektu
        - `configs/`: pliki konfiguracyjne (YAML, JSON, .env, etc.)
        - `assets/`: Images, fonts, or other static resources

### Language/Stack Specific
W zależności od projektu

## Projekty

### 1. SimpleCassettePlayer
Minimalista odtwarzacz kasetowy z wzmacniaczem LM386. Projekt edukacyjny do nauki elektroniki analogowej i zasad magnetyzmu.
- **Poziom:** ⭐ Początkujący
- **Koszt:** 40–80 zł
- **Czas:** 2–4 h
- **Docs:** [SimpleCassettePlayer.md](src/SimpleCassettePlayer/docs/SimpleCassettePlayer.md)

### 2. SimpleCassettePlayerRecorder
Rozszerzenie SimpleCassettePlayer o funkcjonalność **nagrywania**. Pełny rejestrator/odtwarzacz kasetowy z preamp dla mikrofonu, oscilatorem bias i mieszaczem audio.
- **Poziom:** ⭐⭐ Średniozaawansowany
- **Koszt:** 80–150 zł
- **Czas:** 4–6 h
- **Docs:** [SimpleCassettePlayerRecorder.md](src/SimpleCassettePlayerRecorder/docs/SimpleCassettePlayerRecorder.md)

### 3. Esp32VinylCaster
Bezprzewodowy digitizer audio na ESP32. Zamienia sygnał analogowy (gramofon, magnetofon, wzmacniacz) na strumień cyfrowy przez WiFi lub zapis WAV/FLAC na kartę SD. Zewnętrzny ADC I2S, biblioteka arduino-audio-tools.
- **Poziom:** ⭐⭐⭐ Zaawansowany
- **Koszt:** 80–200 zł
- **Platforma:** ESP32 / ESP32-S3 (Arduino Framework)
- **Docs:** [Esp32VinylCaster.md](src/Esp32VinylCaster/docs/Esp32VinylCaster.md)

### 4. Esp32TapeForge
Urządzenie na ESP32 nagrywające na kasety magnetofonowe — dane cyfrowe kompatybilne z komputerami retro (C64, ZX Spectrum, MSX) oraz zwykłe audio z plików WAV na karcie microSD.
- **Poziom:** ⭐⭐⭐ Zaawansowany
- **Koszt:** 100–200 zł (bez magnetofonu)
- **Czas:** 6–12 h
- **Platforma:** ESP32 (Arduino Framework)
- **Docs:** [Esp32TapeForge.md](src/Esp32TapeForge/docs/Esp32TapeForge.md)

### 5. Esp32AudioPlayer
Modułowa platforma audio na ESP32 — od odtwarzacza plików z karty SD, przez radio internetowe, głośnik Bluetooth, aż po stację efektów dźwiękowych i rejestrator audio. Bazuje na bibliotekach open-source.
- **Poziom:** ⭐⭐ Średniozaawansowany
- **Koszt:** 50–150 zł
- **Platforma:** ESP32 / ESP32-S3 (Arduino Framework)
- **Docs:** [ESP32AudioPlayer.md](src/Esp32AudioPlayer/docs/ESP32AudioPlayer.md)

### 6. Esp32StreamingAudioPlayer
Platforma do strumieniowania audio przez WiFi w obu kierunkach — ESP32 jako odbiornik (radio internetowe, Snapcast client, DLNA renderer) i nadajnik (web serwer audio, RTSP server, UDP multicast). Biblioteka arduino-audio-tools.
- **Poziom:** ⭐⭐⭐ Zaawansowany
- **Koszt:** 60–130 zł
- **Platforma:** ESP32 / ESP32-S3 (Arduino Framework)
- **Docs:** [ESP32StreamingAudioPlayer.md](src/Esp32StreamingAudioPlayer/docs/ESP32StreamingAudioPlayer.md)
