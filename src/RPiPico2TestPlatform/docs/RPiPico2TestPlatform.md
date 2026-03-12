# RPiPico2TestPlatform — Stimulus Controller

**Platforma do automatycznego testowania układów mikrokontrolerowych.**
Raspberry Pi Pico 2 (RP2350) działa jako precyzyjny stimulus controller — steruje i odczytuje sygnały cyfrowe na pinach testowanego układu (DUT) przez USB-CDC z dowolnego komputera hosta.

---

## Spis treści

1. [Architektura](#1-architektura)
2. [Protokół komend](#2-protokół-komend)
3. [Podłączenie sprzętu](#3-podłączenie-sprzętu)
4. [Budowanie i wgrywanie firmware](#4-budowanie-i-wgrywanie-firmware)
5. [Uruchamianie testów](#5-uruchamianie-testów)
6. [Dokładność czasowa](#6-dokładność-czasowa)
7. [Rozszerzanie platformy](#7-rozszerzanie-platformy)
8. [Struktura kodu](#8-struktura-kodu)

---

## 1. Architektura

```
┌─────────────────────────────────────────────────────────────┐
│  HOST (PC / Raspberry Pi / CI runner)                       │
│                                                             │
│  pytest ──► SerialAdapter  ──► USB-CDC  ──► Pico 2         │
│  pytest ──► Simulator.py   (bez sprzętu, CI)               │
└─────────────────────────────────────────────────────────────┘
                                      │ USB (115200 baud)
                                      ▼
┌─────────────────────────────────────────────────────────────┐
│  Raspberry Pi Pico 2 (RP2350) — STIMULUS CONTROLLER        │
│                                                             │
│  USB-CDC → CommandHandler → GpioController  → GPIO HAL     │
│                           → CaptureController → PIO HAL    │
│                           → SequenceController              │
│                           → PwmController   → PWM HAL      │
│                                                             │
│  GP0–GP7:  wyjścia stimulus (SET / PULSE / SEQUENCE / PWM) │
│  GP8–GP15: wejścia capture  (CAPTURE)                      │
│  GP16–GP29: dodatkowe GPIO (dowolna konfiguracja)           │
└─────────────────────────────────────────────────────────────┘
                  │ przewody + bufory 74LVC245
                  ▼
┌─────────────────────────────────────────────────────────────┐
│  DUT — Device Under Test (ESP32, STM32, własne PCB, …)     │
└─────────────────────────────────────────────────────────────┘
```

### Warstwy oprogramowania

| Warstwa | Plik | Opis |
|---|---|---|
| Protokół | `protocol.hpp/.cpp` | Parser komend ASCII, formatowanie odpowiedzi |
| GPIO | `gpio_ctrl.hpp/.cpp` | SET, READ, PULSE — sterowanie pinami |
| Capture | `capture.hpp/.cpp` | Detekcja krawędzi (filtr RISING/FALLING/BOTH) |
| Sequence | `sequence.hpp/.cpp` | Wielokrokowe sekwencje z opóźnieniami |
| PWM | `pwm_ctrl.hpp/.cpp` | Generowanie PWM przez hardware_pwm |
| HAL | `hal.hpp` | Interfejs abstrakcji sprzętu (umożliwia testy na hoście) |
| Pico HAL | `pico_hal.hpp/.cpp` | Implementacja HAL dla RP2350 |
| PIO | `capture.pio` | Program PIO do detekcji krawędzi @ 150 MHz |

---

## 2. Protokół komend

Każda komenda to jedna linia ASCII zakończona znakiem `\n`.
Każda odpowiedź to jedna linia ASCII zakończona `\n`.

### Formaty odpowiedzi

| Prefix | Znaczenie |
|---|---|
| `OK\n` | Sukces bez danych |
| `OK:<wartość>\n` | Sukces z wartością tekstową |
| `DATA:<json>\n` | Sukces z danymi JSON |
| `ERR:<KOD>:<opis>\n` | Błąd z kodem i opisem |

### Kody błędów

| Kod | Znaczenie |
|---|---|
| `INVALID_CMD` | Nieznana nazwa komendy |
| `INVALID_PIN` | Numer pinu poza zakresem (0–29) |
| `INVALID_STATE` | Argument stanu nie jest HIGH ani LOW |
| `INVALID_EDGE` | Argument krawędzi nie jest RISING/FALLING/BOTH |
| `INVALID_PARAM` | Parametr numeryczny poza zakresem lub nie jest liczbą |
| `PIN_BUSY` | Na pinie aktywne jest PWM |
| `TIMEOUT` | Operacja nie zakończyła się w zadanym czasie |
| `OVERFLOW` | Sekwencja zbyt długa lub przepełnienie bufora |

---

### Komendy GPIO

#### `SET GP<n> HIGH|LOW`

Ustawia stan logiczny pinu wyjściowego.

```
→ SET GP0 HIGH
← OK

→ SET GP30 HIGH
← ERR:INVALID_PIN:pin 30 out of range

→ SET GP0 MAYBE
← ERR:INVALID_STATE:expected HIGH or LOW
```

**Ważne:** Alternatywne formy argumentu stanu: `H`, `L`, `1`, `0`, `TRUE`, `FALSE` (bez rozróżniania wielkości liter).
Numer pinu można podać jako `GP0`, `gp0` lub po prostu `0`.

---

#### `READ GP<n>`

Odczytuje stan logiczny pinu.

```
→ READ GP0
← OK:HIGH

→ READ GP5
← OK:LOW

→ READ GP29
← OK:UNDEFINED   (pin nigdy nie był ustawiony)
```

---

#### `PULSE GP<n> <duration_us>`

Generuje jeden impuls: odwraca stan pinu, czeka `duration_us` mikrosekund, przywraca oryginalny stan.

```
→ PULSE GP0 100
← DATA:{"pin":0,"requested_us":100,"actual_us":100,"ok":true}
```

**Gwarantowane zachowanie:**
- Pin zawsze wraca do stanu sprzed impulsu (LOW→HIGH→LOW lub HIGH→LOW→HIGH)
- Jeśli pin był UNDEFINED, traktowany jest jako LOW
- `duration_us` musi być w zakresie `[1, 1000000]`

**Przykład — pomiar opóźnienia propagacji:**
```
SET GP0 LOW          # stan wyjściowy
PULSE GP0 500        # impuls 500 µs
CAPTURE GP8 RISING 1 # czekaj na odpowiedź z DUT
```

---

#### `CAPTURE GP<n> RISING|FALLING|BOTH <samples> [timeout_ms]`

Przechwytuje do `samples` zdarzeń krawędziowych na pinie wejściowym.
Domyślny timeout: 1000 ms.

```
→ CAPTURE GP8 RISING 3 500
← DATA:{"pin":8,"count":3,"timed_out":false,"duration_us":1245}

→ CAPTURE GP8 BOTH 1 50
← DATA:{"pin":8,"count":0,"timed_out":true,"duration_us":50000}
```

**Filtry krawędzi:**

| Filtr | Co rejestruje |
|---|---|
| `RISING` | Tylko przejścia LOW → HIGH |
| `FALLING` | Tylko przejścia HIGH → LOW |
| `BOTH` | Wszystkie przejścia |

**Limity:** `samples` ∈ [1, 4096], `timeout_ms` ∈ [1, 60000]

---

### Komendy sekwencji

#### `SEQUENCE GP<n>:HIGH|LOW[:<delay_us>] ...`

Wykonuje wielokrokową sekwencję GPIO z opcjonalnymi opóźnieniami między krokami.

```
→ SEQUENCE GP0:HIGH:100 GP1:LOW:50 GP0:LOW
← DATA:{"executed":3,"total":3,"completed":true,"duration_us":150}
```

**Format kroku:** `GP<n>:HIGH|LOW[:<opóźnienie_us>]`
- `GP0:HIGH` — ustaw GP0 HIGH bez opóźnienia
- `GP0:HIGH:500` — ustaw GP0 HIGH, poczekaj 500 µs

**Gwarancja atomowości:** WSZYSTKIE kroki są walidowane przed wykonaniem pierwszego. Jeśli którykolwiek krok ma błąd (zły pin, PIN_BUSY), żaden pin nie zmieni stanu.

**Limit:** maksymalnie 64 kroki na sekwencję.

---

### Komendy PWM

#### `PWM GP<n> <freq_hz> <duty_pct>`

Uruchamia sygnał PWM na pinie.

```
→ PWM GP3 1000 50
← OK

→ PWM GP3 100000000 25
← OK
```

**Parametry:**
- `freq_hz` ∈ [1, 100 000 000] Hz
- `duty_pct` ∈ [0.0, 100.0] %

**Ważne:** Pin z aktywnym PWM jest zajęty — komendy `SET` i `PULSE` zwrócą `ERR:PIN_BUSY`. Użyj `PWM_STOP` przed zmianą.

---

#### `PWM_STOP GP<n>`

Zatrzymuje PWM i ustawia pin w stan LOW.

```
→ PWM_STOP GP3
← OK
```

---

### Komendy informacyjne

| Komenda | Odpowiedź | Opis |
|---|---|---|
| `VERSION` | `OK:McuTestPlatform/1.0.0` | Nazwa i wersja firmware |
| `STATUS` | `DATA:{...}` | Wersja, urządzenie |
| `PINS` | `DATA:{...}` | Liczba i zakres pinów |
| `RESET` | `OK` | Wszystkie piny → LOW, zatrzymuje PWM |
| `HELP` | `OK:<lista komend>` | Krótka lista komend |

---

## 3. Podłączenie sprzętu

### Schemat połączeń (prototyp na płytce stykowej)

```
Pico 2                  74LVC245 (bufor)              DUT
                        (translacja 3.3V ↔ 3.3V/5V)

GP0 ─────────► DIR=HIGH ──► A0 ─► B0 ──[100Ω]──► DUT_PIN_IN_0
GP1 ─────────────────────► A1 ─► B1 ──[100Ω]──► DUT_PIN_IN_1
...
GP7 ─────────────────────► A7 ─► B7 ──[100Ω]──► DUT_PIN_IN_7

GP8  ◄──────── DIR=LOW  ──◄ A0 ◄─ B0 ──────────── DUT_PIN_OUT_0
GP9  ◄────────────────────◄ A1 ◄─ B1 ──────────── DUT_PIN_OUT_1
...
GP15 ◄────────────────────◄ A7 ◄─ B7 ──────────── DUT_PIN_OUT_7

GND ──────────────────────────────────────────── DUT_GND
```

### Lista elementów (BOM)

| Element | Ilość | Uwagi |
|---|---|---|
| Raspberry Pi Pico 2 | 1 | RP2350, stimulus controller |
| 74LVC245 | 2 | Bufory dwukierunkowe, ochrona i translacja poziomów |
| Rezystory 100 Ω | 16 | Na każdej linii sygnałowej |
| Kondensator 100 nF | 2 | Filtrowanie zasilania buforów |
| Płytka prototypowa | 1 | 830 punktów zalecane |
| Przewody połączeniowe | — | |

### Uwagi dotyczące podłączenia

- Zawsze używaj buforów 74LVC245 między Pico 2 a DUT-em — chronią RP2350 przed zwarciem
- Rezystory 100 Ω na każdej linii ograniczają prąd przy przypadkowym konflikcie magistrali
- GP0–GP7 konfiguruj jako wyjścia stimulus (kierunek bufora: Pico → DUT)
- GP8–GP15 konfiguruj jako wejścia capture (kierunek bufora: DUT → Pico)
- Wspólna masa (GND) między Pico 2 a DUT-em jest obowiązkowa

---

## 4. Budowanie i wgrywanie firmware

### Wymagania

- CMake ≥ 3.20
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Pico SDK ≥ 2.1.0

### Budowanie firmware

```bash
# Ustaw ścieżkę do Pico SDK
export PICO_SDK_PATH=/ścieżka/do/pico-sdk

cd src/McuTestPlatform
mkdir build && cd build

# Konfiguracja dla Pico 2
cmake .. -DPICO_BOARD=pico2

# Kompilacja
make -j$(nproc) McuTestPlatform
```

Po kompilacji powstają pliki:
- `McuTestPlatform.uf2` — do wgrania przez USB (BOOTSEL)
- `McuTestPlatform.elf` — do wgrania przez debugger/SWD (OpenOCD, picotool)

---

### Metoda 1 — UF2 przez USB (tylko za pierwszym razem lub awaryjnie)

Wymaga fizycznego dostępu do przycisku BOOTSEL. Nie nadaje się do automatyzacji.

1. Przytrzymaj przycisk **BOOTSEL** na Pico 2
2. Podłącz USB do komputera
3. Puść przycisk — pojawi się dysk masowy `RPI-RP2`
4. Skopiuj `McuTestPlatform.uf2` na ten dysk
5. Pico 2 uruchomi się automatycznie i zniknie z listy dysków

---

### Metoda 2 — OpenOCD przez SWD (zalecana do codziennego developmentu)

SWD (Serial Wire Debug) to interfejs debugowania i programowania RP2350. Umożliwia wgrywanie firmware bez dotykania sprzętu — z linii poleceń lub z IDE. To właściwa metoda na platformie testowej gdzie Pico 2 jest zamontowane w obudowie.

#### 2a. Programator — Pico Debug Probe (oficjalny, ~$12)

Oficjalny programator RPi Foundation. Podłącza się przez USB do komputera, a do Pico 2 przez SWD.

**Podłączenie Pico Debug Probe → Pico 2 (stimulus):**

```
Debug Probe         Pico 2 (stimulus)
───────────         ─────────────────
SWDIO    ──────────► GP_SWDIO  (pin 24 na płytce, oznaczony SWDIO)
SWDCLK   ──────────► GP_SWDCLK (pin 25 na płytce, oznaczony SWDCLK)
GND      ──────────► GND
(opcjonalnie)
TX       ──────────► GP1  (UART RX Pico 2 — jeśli chcesz UART logi)
RX       ──────────► GP0  (UART TX Pico 2)
```

> Pico 2 ma dedykowane piny SWD na dolnej krawędzi płytki (3-pinowy konektor DEBUG), nie mylić z GP24/GP25 które są zwykłymi GPIO.

**Instalacja OpenOCD z obsługą RP2350:**

```bash
# Ubuntu/Debian
sudo apt install openocd

# macOS
brew install openocd

# Weryfikacja — musi być ≥ 0.12.0 z obsługą rp2350
openocd --version
```

**Wgrywanie firmware:**

```bash
openocd \
  -f interface/cmsis-dap.cfg \
  -f target/rp2350.cfg \
  -c "adapter speed 5000" \
  -c "program build/McuTestPlatform.elf verify reset exit"
```

- `verify` — sprawdza poprawność zapisu (nie pomijaj!)
- `reset` — restartuje Pico 2 po wgraniu
- `exit` — zamyka OpenOCD po operacji

Czas wgrywania: ~2–4 sekundy.

---

#### 2b. Programator — drugi Pico/Pico 2 jako debugger (tani, ~25 zł)

Drugi Pico 2 (lub stary Pico) wgrany z firmware **debugprobe** działa identycznie jak Pico Debug Probe — i jest rozpoznawany przez OpenOCD jako CMSIS-DAP.

**Wgranie firmware debugprobe na drugi Pico:**

```bash
# Pobierz gotowe .uf2 z GitHub Releases
wget https://github.com/raspberrypi/debugprobe/releases/latest/download/debugprobe_on_pico2.uf2

# Wgraj przez BOOTSEL (jednorazowo na programator)
# Skopiuj debugprobe_on_pico2.uf2 na dysk RPI-RP2
```

**Podłączenie Pico-jako-debugger → Pico 2 (stimulus):**

```
Pico (debugger)     Pico 2 (stimulus)
───────────────     ─────────────────
GP2   ──────────────► SWDIO
GP3   ──────────────► SWDCLK
GND   ──────────────► GND
VSYS  ──────────────► VSYS   (opcjonalnie: zasilaj stimulus z debuggera)
```

Następnie wgrywasz identycznie jak w 2a — OpenOCD rozpoznaje debugprobe jako `cmsis-dap`.

---

#### 2c. Programator — J-Link (profesjonalny)

```bash
openocd \
  -f interface/jlink.cfg \
  -f target/rp2350.cfg \
  -c "adapter speed 10000" \
  -c "program build/McuTestPlatform.elf verify reset exit"
```

---

### Metoda 3 — picotool (przez USB, bez BOOTSEL w następnych wgraniach)

`picotool` może wgrywać firmware gdy Pico 2 jest w trybie normalnym i ma aktywne USB-CDC — bez wciskania BOOTSEL. Wymaga specjalnej konfiguracji w firmware (`pico_enable_stdio_usb` + `PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE`), co jest już skonfigurowane w tym projekcie.

**Instalacja:**

```bash
# Z pakietu
sudo apt install picotool   # Ubuntu 24.04+

# Lub ze źródeł
git clone https://github.com/raspberrypi/picotool
cd picotool && mkdir build && cmake .. && make
sudo make install
```

**Wgrywanie bez BOOTSEL** (Pico 2 musi być podłączone i działać):

```bash
# Reset do trybu bootloader przez USB i wgraj .elf
picotool load build/McuTestPlatform.elf --force

# Lub .uf2
picotool load build/McuTestPlatform.uf2

# Uruchom po wgraniu
picotool reboot
```

**Skrypt automatyzujący (do CI lub Makefile):**

```bash
#!/bin/bash
# flash.sh — wgrywa firmware bez dotykania sprzętu
set -e

ELF="build/McuTestPlatform.elf"

echo "Wgrywanie firmware..."
picotool load "$ELF" --force
picotool reboot

echo "Czekam na gotowość USB-CDC..."
sleep 2

echo "Weryfikacja..."
echo "VERSION" | timeout 3 cat - /dev/ttyACM0 | grep -q "McuTestPlatform" && \
  echo "OK — firmware działa" || echo "BŁĄD — brak odpowiedzi"
```

---

### Porównanie metod

| Metoda | Wymagany dostęp | Czas | Automatyzacja | Zalecane dla |
| --- | --- | --- | --- | --- |
| UF2 (BOOTSEL) | Fizyczny (przycisk) | ~5 s | Nie | Pierwsze uruchomienie |
| OpenOCD + SWD | Przewód SWD | 2–4 s | Tak | Codzienne programowanie |
| picotool (USB) | USB-CDC aktywne | 3–5 s | Tak | CI/CD, bez dodatkowego sprzętu |
| J-Link + OpenOCD | Przewód SWD | 1–2 s | Tak | Środowisko profesjonalne |

**Rekomendacja dla platformy testowej:** OpenOCD + debugprobe (drugi Pico 2 za ~25 zł). Przewód SWD zostaje podłączony na stałe, wgrywanie jedną komendą z terminala lub Makefile.

---

### Weryfikacja po wgraniu

```bash
# Linux/macOS
screen /dev/ttyACM0 115200

# Windows
putty -serial COM3 -sercfg 115200,8,n,1

# W terminalu wyślij:
VERSION
# Oczekiwana odpowiedź:
OK:McuTestPlatform/1.0.0
```

Możesz też sprawdzić z linii poleceń:

```bash
echo "VERSION" > /dev/ttyACM0 && cat /dev/ttyACM0
# OK:McuTestPlatform/1.0.0
```

---

## 5. Uruchamianie testów

### C++ unit testy (bez sprzętu)

Testy kompilują się i uruchamiają na hoście (x86/arm64). Używają `MockHAL` — brak rzeczywistych opóźnień, deterministyczne.

```bash
cd src/McuTestPlatform
mkdir build_host && cd build_host

# Konfiguracja bez Pico SDK (automatycznie wykrywa tryb HOST)
cmake ..
make -j$(nproc)

# Uruchomienie wszystkich testów
ctest --output-on-failure

# Uruchomienie konkretnego testu
./tests/unit/test_protocol
./tests/unit/test_gpio_ctrl
./tests/unit/test_capture
./tests/unit/test_sequence
./tests/unit/test_pwm_ctrl
./tests/unit/test_cmd_handler
```

Przykładowy output:
```
test_protocol.cpp:62:test_parse_pin_gp_prefix_lowercase:PASS
test_protocol.cpp:63:test_parse_pin_gp_prefix_uppercase:PASS
...
52 Tests 0 Failures 0 Ignored
OK
```

### Python integration testy (Simulator — bez sprzętu)

Testy uruchamiają się przeciwko `simulator.py` — Python-owa replika firmware do użytku w CI.

```bash
cd src/McuTestPlatform/tests/host

pip install -r requirements.txt

# Wszystkie testy
pytest -v

# Konkretny plik
pytest test_gpio.py -v
pytest test_capture.py -v

# Z raportem pokrycia
pytest --tb=short -q
```

### Python integration testy (prawdziwy Pico 2)

```bash
cd src/McuTestPlatform/tests/host

# Podaj port USB-CDC Pico 2
export PICO_PORT=/dev/ttyACM0   # Linux
# export PICO_PORT=/dev/cu.usbmodem* # macOS
# set PICO_PORT=COM3             # Windows

pytest -v
```

Fixture `device` w `conftest.py` automatycznie wykrywa `PICO_PORT` — jeśli ustawiony, używa `SerialAdapter`; w przeciwnym razie używa `Simulator`.

### Zestawienie testów

| Plik testów | Typ | Liczba testów | Co testuje |
|---|---|---|---|
| `test_protocol.cpp` | C++ unit | 52 | Parser komend, formatowanie odpowiedzi |
| `test_gpio_ctrl.cpp` | C++ unit | 40 | SET, READ, PULSE, PWM, RESET |
| `test_capture.cpp` | C++ unit | 30 | Filtr krawędzi, limity, timeout |
| `test_sequence.cpp` | C++ unit | 22 | Walidacja-first, opóźnienia, atomowość |
| `test_pwm_ctrl.cpp` | C++ unit | 16 | Zakres częstotliwości i wypełnienia |
| `test_cmd_handler.cpp` | C++ unit | 28 | Dispatch protokołu, kody błędów |
| `test_gpio.py` | Python integ. | 21 | SET/READ przez protokół |
| `test_pulse.py` | Python integ. | 18 | PULSE: stan przed/po, zakresy |
| `test_capture.py` | Python integ. | 22 | CAPTURE: filtry, timeout, zdarzenia |
| `test_sequence.py` | Python integ. | 16 | SEQUENCE: atomowość, opóźnienia |
| **Łącznie** | | **265** | |

---

## 6. Dokładność czasowa

### PIO capture (detekcja krawędzi)

| Parametr | Wartość |
|---|---|
| Zegar PIO | 150 MHz |
| Rozdzielczość timestampu | 6.67 ns |
| Jitter detekcji krawędzi | ≤ 2 cykle PIO ≈ **13 ns** |
| Maksymalny czas rejestrowania | 60 s (timeout_ms = 60 000) |
| Maksymalna liczba zdarzeń | 4 096 |

### GPIO SET / PULSE (generowanie sygnałów)

| Parametr | Wartość |
|---|---|
| Metoda | `busy_wait_us_32()` — hardware TIMER |
| Rozdzielczość | 1 µs |
| Dokładność | ±1 µs przy 150 MHz |
| Minimalny czas impulsu | 1 µs |
| Maksymalny czas impulsu | 1 s (1 000 000 µs) |

### Dlaczego nie używać PIO do SET/PULSE?

Dla podstawowych operacji `SET` i `PULSE` `busy_wait_us_32()` z TIMER jest wystarczające (±1 µs). PIO jest zarezerwowane dla `CAPTURE` — gdzie potrzebna jest nanosekundowa rozdzielczość timestampów i niski jitter wykrywania krawędzi.

Jeśli w przyszłości potrzebna będzie precyzja <1 µs dla stimulus, można dodać dedykowany PIO state machine do generowania sekwencji — patrz sekcja [Rozszerzanie platformy](#7-rozszerzanie-platformy).

---

## 7. Rozszerzanie platformy

### Dodawanie nowej komendy

1. Zadeklaruj handler `do_newcmd()` w `command_handler.hpp`
2. Zaimplementuj w `command_handler.cpp`
3. Dodaj wpis do tabeli dispatch w `CommandHandler::handle()`
4. Dodaj testy w `test_cmd_handler.cpp`

```cpp
// command_handler.hpp
size_t do_newcmd(const protocol::ParsedCommand& cmd, char* buf, size_t sz);

// command_handler.cpp
else if (strcmp(cmd.name, "NEWCMD") == 0) return do_newcmd(cmd, buf, buf_size);

size_t CommandHandler::do_newcmd(...) {
    // walidacja → wykonanie → format_ok / format_data / format_err
}
```

### Dodawanie nowego protokołu komunikacyjnego (SPI, I²C, UART)

Implementacja w PIO — dodaj nowy plik `.pio` i odpowiadający kontroler:
1. Napisz program PIO (patrz `capture.pio` jako wzorzec)
2. Dodaj nagłówek kontrolera np. `spi_ctrl.hpp`
3. Zarejestruj komendy `SPI_WRITE`, `SPI_READ` w `CommandHandler`
4. Dodaj inicjalizację PIO SM w `pico_hal.cpp`

### Obsługa analogowa (ADC / DAC)

- **ADC:** RP2350 ma wbudowany 12-bitowy ADC na GP26–GP28. Dodaj `AdcHAL` i komendę `ADC GP26`.
- **DAC:** Użyj zewnętrznego MCP4725 przez I²C lub PWM + filtr RC. Dodaj `DacHAL` i komendę `DAC <ch> <mv>`.

### Rozproszony deployment (Pico 2 W z WiFi)

Pico 2 W ma WiFi (CYW43439). Zamiast USB-CDC można wystawić TCP server:
- Rdzeń 0: obsługa WiFi i TCP (lwIP)
- Rdzeń 1: PIO i GPIO — real-time bez zakłóceń z sieci

Protokół komend pozostaje identyczny — tylko transport zmienia się z USB-CDC na TCP socket. `SerialAdapter` w Pythonie zastąp `TcpAdapter`.

---

## 8. Struktura kodu

```
src/McuTestPlatform/
│
├── CMakeLists.txt              # Root CMake: auto-detekcja Pico SDK vs HOST
├── pico_sdk_import.cmake       # Import Pico SDK (standardowy plik RPi Foundation)
│
├── src/                        # Firmware (Pico 2)
│   ├── hal.hpp                 # Interfejsy GpioHAL / PwmHAL / CaptureHAL
│   ├── pico_hal.hpp            # Deklaracje implementacji dla RP2350
│   ├── pico_hal.cpp            # gpio_*, hardware_pwm, PIO (tylko cel Pico)
│   │
│   ├── protocol.hpp            # Typy: ParsedCommand, SeqStep, kody błędów
│   ├── protocol.cpp            # parse_pin/state/edge, tokenise, fmt_*
│   │
│   ├── gpio_ctrl.hpp           # GpioController: SET, READ, PULSE
│   ├── gpio_ctrl.cpp
│   ├── capture.hpp             # CaptureController: CAPTURE z filtrem krawędzi
│   ├── capture.cpp
│   ├── sequence.hpp            # SequenceController: SEQUENCE
│   ├── sequence.cpp
│   ├── pwm_ctrl.hpp            # PwmController: PWM, PWM_STOP
│   ├── pwm_ctrl.cpp
│   │
│   ├── command_handler.hpp     # CommandHandler: dispatch komend → kontrolery
│   ├── command_handler.cpp
│   │
│   ├── capture.pio             # Program PIO @ 150 MHz (detekcja krawędzi)
│   ├── main.cpp                # Pętla USB-CDC: czytaj linie → handle → wyślij
│   └── CMakeLists.txt          # Cel firmware + generowanie .pio.h
│
└── tests/
    ├── CMakeLists.txt          # FetchContent: Unity framework
    │
    ├── unit/                   # C++ testy uruchamiane na HOST (bez sprzętu)
    │   ├── CMakeLists.txt      # stimulus_core lib + add_unit_test macro
    │   ├── mock_hal.hpp        # MockHAL + MockCaptureHAL (delay_us = fake)
    │   ├── test_protocol.cpp   # 52 testy parsera i formaterów
    │   ├── test_gpio_ctrl.cpp  # 40 testów GpioController
    │   ├── test_capture.cpp    # 30 testów CaptureController
    │   ├── test_sequence.cpp   # 22 testy SequenceController
    │   ├── test_pwm_ctrl.cpp   # 16 testów PwmController
    │   └── test_cmd_handler.cpp # 28 testów CommandHandler
    │
    └── host/                   # Python testy (Simulator lub prawdziwy Pico 2)
        ├── simulator.py        # Python replika firmware — do CI bez sprzętu
        ├── conftest.py         # Fixture: Simulator | SerialAdapter (PICO_PORT=)
        ├── test_gpio.py        # SET / READ
        ├── test_pulse.py       # PULSE
        ├── test_capture.py     # CAPTURE
        ├── test_sequence.py    # SEQUENCE
        └── requirements.txt    # pytest, pyserial
```

### Wzorzec HAL — dlaczego?

`hal.hpp` definiuje trzy czyste interfejsy C++ (`GpioHAL`, `PwmHAL`, `CaptureHAL`). Wszystkie kontrolery operują wyłącznie na tych interfejsach.

**Konsekwencja:** unit testy w `tests/unit/` kompilują się na zwykłym x86 z `MockHAL` i nie wymagają Pico 2 ani Pico SDK. `MockHAL::delay_us()` tylko zwiększa licznik czasu — testy działają w mikrosekundach, nie w sekundach.

```
Środowisko      | GpioHAL        | PwmHAL         | CaptureHAL
─────────────────┼────────────────┼────────────────┼─────────────────
Pico 2          | PicoGpioHAL    | PicoPwmHAL     | PicoCaptureHAL
Host unit tests | MockHAL        | MockHAL        | MockCaptureHAL
```

---

*Dokumentacja dla McuTestPlatform v1.0.0*
