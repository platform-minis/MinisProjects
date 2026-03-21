# Project: MinisProjects

## Overview
Kolekcja projektów DIY. Głównie z zakresu elektroniki.

## Key Objectives / Current Focus
Baza wiedzy o moich projektach DIY.

## Directory Structure
- `src/` : Katalog z projektami
    - `src/{NazwaProjektu}/` - katalog projektu
        - `project.json` — metadane projektu (id, name, softwarePlatform, moduleId, libraries, ...)
        - `src/` — kody źródłowe Arduino / cmake
        - `sketches/` — szkice uPython
        - `docs/` — dokumentacja projektu
- `libs/` : Biblioteki współdzielone
    - `libs/MinisLib/` — Arduino (C++): biblioteka IoT dla platformy MyCastle (MQTT over TCP)
    - `libs/uMinisLib/` — MicroPython: biblioteka IoT dla platformy MyCastle (MQTT over TCP)
    - `libs/MinisLib2/` — lokalna (nie w repozytorium)
    - `libs/arduino_lib_pubsubclient/` — git submodule: Arduino PubSubClient
- `scripts/generate-index.js` — generuje `index.json` z `project.json` każdego projektu; uruchom przez `npm run index`
- `index.json` — indeks wszystkich projektów i modułów, używany przez MyCastle
- `modules.json` — definicje modułów sprzętowych (esp32-s3-pico, esp32-devkitc, rp2350-pico2, ...)

### project.json — struktura

```json
{
  "id": "NazwaProjektu",
  "name": "Czytelna nazwa",
  "softwarePlatform": "Arduino | uPython | cmake | hardware",
  "moduleId": "esp32-s3-pico | esp32-devkitc | rp2350-pico2",
  "libraries": [
    { "name": "ArduinoJson", "version": "7.4.3" },
    { "url": "https://github.com/platform-minis/MinisLib", "remoteName": "minis_iot.py" }
  ]
}
```

Dla bibliotek Arduino: `name` + `version` (z managera) lub `url` (git-url install).
Dla bibliotek uPython: `url` + `remoteName` (nazwa pliku na urządzeniu). Pole `url` może być raw GitHub URL lub embedded base64 (`data:text/plain;base64,...` — zawartość zakodowana inline, używane gdy projekt ma być self-contained lub podczas dev).

Każda biblioteka uPython to osobny wpis w `libraries`. Jeśli projekt używa rozszerzeń (`vkbd.py`, `vmouse.py`), każde z nich musi mieć własny wpis z `url` i `remoteName`.

### Language/Stack Specific
W zależności od projektu.

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

### 7. Esp32S3Test

Projekt testowy ESP32-S3 (Arduino). Demonstracja Serial USB CDC, odczytu częstotliwości CPU i rozmiaru Flash. Biblioteka: ArduinoJson 7.4.3.

- **Platforma:** ESP32-S3 (Arduino Framework)
- **Moduł:** esp32-s3-pico
- **FQBN:** `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi`

### 8. Esp32S3DisplayVGA

ESP32-S3 generuje sygnał VGA 640×480 @ 60Hz, 8-bit kolor (R3G3B2) przez PIO/DMA. Biblioteka luniVGA.

- **Platforma:** ESP32-S3 (Arduino Framework)
- **Moduł:** esp32-s3-pico

### 9. Esp32S3DisplayVGA1bit

ESP32-S3 generuje sygnał VGA monochromatyczny 640×480 @ 60Hz. Wersja 1-bit biblioteki luniVGA.

- **Platforma:** ESP32-S3 (Arduino Framework)
- **Moduł:** esp32-s3-pico

### 10. Esp32S3uPythonTest

Projekt testowy ESP32-S3 w MicroPython.

- **Platforma:** ESP32-S3 (MicroPython)
- **Moduł:** esp32-s3-pico

### 11. Esp32S3uPythonCurses

ESP32-S3 MicroPython — interfejs tekstowy (curses-like) na ESP32-S3.

- **Platforma:** ESP32-S3 (MicroPython)
- **Moduł:** esp32-s3-pico

### 12. Esp32S3uPythonIot

ESP32-S3 MicroPython IoT dla platformy MyCastle. Łączy się przez MQTT over plain TCP, wysyła telemetrię, obsługuje komendy. Biblioteka: uMinisLib (`minis_iot.py`).

- **Platforma:** ESP32-S3 (MicroPython)
- **Moduł:** esp32-s3-pico
- **Biblioteki:** `libs/uMinisLib/minis_iot.py` (wymagana zawsze). Rozszerzenia (`vkbd.py`, `vmouse.py`) dodawane jako oddzielne wpisy w `libraries[].url+remoteName` gdy projekt ich używa.

### 13. RPiPico2TestPlatform

Platforma testowa GPIO, PWM i protokołów komunikacji na Raspberry Pi Pico 2 (RP2350). Obsługuje sekwencje komend przez UART/USB.

- **Platforma:** cmake (RP2350)
- **Moduł:** rp2350-pico2

### 14. RPiPico2DisplayVGA

Raspberry Pi Pico 2 (RP2350) wyświetla tekst na monitorze VGA 640×480 1bit. Terminal 80×60 znaków z czcionką 8×8 px i obsługą klawiatury PS/2.

- **Platforma:** cmake (RP2350)
- **Moduł:** rp2350-pico2

### 15. RPiPico2DisplayDVI

Raspberry Pi Pico 2 (RP2350) wyświetla obraz DVI/HDMI przez PIO i DMA bez obciążenia CPU.

- **Platforma:** cmake (RP2350)
- **Moduł:** rp2350-pico2

## Biblioteki (`libs/`)

### MinisLib (Arduino / C++)

Biblioteka IoT dla platformy MyCastle. MQTT 3.1.1 over TCP, telemetria, heartbeat, komendy z ACK, rozszerzenia (extensions).

- Zależności: ArduinoJson >= 7.x, PubSubClient (git submodule)
- Repo: `https://github.com/platform-minis/MinisLib` (lokalnie w `libs/MinisLib/`, gitignored — oddzielne repo)

**Extension API (`MinisIoT`):**

```cpp
// Rejestracja handlera rozszerzenia (w konstruktorze klasy rozszerzenia)
minis.addExtension("vkbd", [this](const char* id, const char* op, JsonObjectConst params) {
    _onRequest(id, op, params);
});

// Publikacja odpowiedzi
minis.extRespond("vkbd", id, ok, dataJson, errCode, errMsg);
```

- `addExtension()` — rejestruje callback + buduje tematy req/res, zapisuje do `send_hello`
- `extRespond()` — publikuje JSON `{id, ok, data?, error?}` na temat `minis/{user}/{device}/ext/{type}/res`
- Po połączeniu MQTT: subskrybuje wszystkie tematy `ext/*/req` i wysyła `hello` z listą extensions

**Klasy rozszerzeń:**

- `MinisVirtualKeyboard` (`EXT_TYPE = "vkbd"`) — wewnętrzny stan wirtualnej klawiatury
  - Publiczne: `isKeyDown(key)`, `modifiers()` (bitmask `VKBD_MOD_*`), `lastText()`
  - Ops: `key_press`, `key_down`, `key_up`, `type_text`, `hotkey`
- `MinisVirtualMouse` (`EXT_TYPE = "vmouse"`) — wewnętrzny stan wirtualnej myszy
  - Publiczne: `x()`, `y()`, `buttons()` (bitmask `VMOUSE_BTN_*`), `isPressed(btn)`, `lastScrollDy()`
  - Ops: `move`, `move_rel`, `click`, `double_click`, `press`, `release`, `scroll`, `drag`, `get_pos`

### uMinisLib (MicroPython)

Odpowiednik MinisLib dla MicroPython. Brak zewnętrznych zależności — MQTT 3.1.1 over plain TCP (`umqtt.simple`).

- Plik: `libs/uMinisLib/minis_iot.py`
- Repo: `https://github.com/platform-minis/MinisProjects` (w tym repo, ścieżka `libs/uMinisLib/`)
- Deploy: MyCastle pobiera plik z GitHub raw URL i wgrywa na urządzenie automatycznie

**Extension API (`MinisIoT`):**

```python
# Rejestracja (np. w konstruktorze klasy rozszerzenia)
minis.add_extension("vkbd", self._on_request)
# callback: callback(req_id: str, op: str, params: dict)

# Publikacja odpowiedzi
minis.ext_respond("vkbd", req_id, ok, data=None, error=None)
```

- `add_extension()` — rejestruje callback + buduje temat req, dołącza extension do `send_hello`
- `ext_respond()` — publikuje JSON `{id, ok, data?, error?}` na `minis/{user}/{device}/ext/{type}/res`
- Po połączeniu MQTT: subskrybuje wszystkie zarejestrowane tematy `ext/*/req`, wysyła `send_hello()` z listą extensions
- `send_hello()` można też wywołać ręcznie

**Pliki rozszerzeń:**

- `libs/uMinisLib/vkbd.py` — `VirtualKeyboard` (`EXT_TYPE = "vkbd"`)
  - Wewnętrzny stan klawiatury: `pressed_keys` (set), `modifiers` (bitmask, stałe `MOD_*`), `last_text`
  - Ops: `key_press`, `key_down`, `key_up`, `type_text`, `hotkey`
- `libs/uMinisLib/vmouse.py` — `VirtualMouse` (`EXT_TYPE = "vmouse"`)
  - Wewnętrzny stan myszy: `x`, `y`, `buttons` (bitmask, stałe `BTN_*`), `last_click`, `last_scroll_dy`
  - Ops: `move`, `move_rel`, `click`, `double_click`, `press`, `release`, `scroll`, `drag`, `get_pos`
