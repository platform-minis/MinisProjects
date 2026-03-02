# 🔥 Esp32TapeForge — Dokumentacja Projektu

> **Wersja:** 1.0  
> **Data:** 2026-02-06  
> **Poziom trudności:** ⭐⭐⭐ Zaawansowany  
> **Szacowany czas montażu:** 6–12 godzin  
> **Szacowany koszt:** 100–200 zł (bez magnetofonu)  
> **Platforma:** ESP32 (Arduino Framework)  

---

## 1. Opis projektu

TapeForge to urządzenie oparte na ESP32, które nagrywa na kasety magnetofonowe — zarówno dane cyfrowe kompatybilne z komputerami retro (Commodore 64, ZX Spectrum, MSX), jak i zwykłe audio z plików WAV przechowywanych na karcie microSD.

ESP32 generuje sygnał audio przez wbudowany przetwornik DAC (Digital-to-Analog Converter) i podaje go na wejście LINE IN / AUX magnetofonu nagrywającego (np. Retekess TR621 lub używany radiomagnetofon z OLX). Magnetofon traktuje ten sygnał dokładnie tak, jak dźwięk z mikrofonu czy radia — nagrywa go na taśmę.

Efekt końcowy: kaseta, którą można włożyć do prawdziwego Commodore 64 i załadować z niej program, albo kaseta z muzyką do odtworzenia w dowolnym walkmanie.

### 1.1. Co potrafi TapeForge

- **Zapis danych C64** — generuje sygnał kompatybilny z Datasette. Wczytuje plik .TAP z karty SD, koduje go jako impulsy o odpowiedniej szerokości i wysyła przez DAC. Prawdziwy C64 załaduje te dane komendą `LOAD`.
- **Zapis danych ZX Spectrum** — generuje sygnał z pliku .TZX / .TAP. Spectrum załaduje program komendą `LOAD ""`.
- **Zapis danych KCS** — generuje sygnał Kansas City Standard z dowolnego pliku binarnego. Kompatybilny z systemami CP/M, Altair i innymi maszynami lat 70.
- **Zapis audio z SD** — odtwarza pliki WAV (mono/stereo, 8/16-bit, 22050/44100 Hz) z karty microSD przez DAC, pozwalając nagrać muzykę lub dowolny dźwięk na kasetę.
- **Tryb mieszany** — możliwość nagrania na jednej kasecie sekwencji: pilot → dane programu → przerwa → muzyka. Jak w starych czasach, gdy na jednej stronie kasety był program, a na drugiej muzyka z gry.
- **Podgląd na OLED** — wyświetla tryb, postęp, nazwę pliku, VU-metr poziomu sygnału.
- **Kalibracja poziomu** — generuje ton testowy 1 kHz do ustawienia poziomu nagrywania na magnetofonie.

### 1.2. Cele edukacyjne

- Zrozumienie konwersji cyfrowo-analogowej (DAC) i generowania precyzyjnych przebiegów.
- Nauka kodowania danych jako sygnałów audio (FSK, pulse width modulation).
- Praca z formatami plików retro-komputerowych (.TAP, .TZX).
- Odtwarzanie plików WAV — parsowanie nagłówka, konwersja sample rate, obsługa stereo→mono.
- Praktyka programowania czasu rzeczywistego z timerami sprzętowymi i DMA.

### 1.3. Powiązanie z poprzednimi projektami

TapeForge jest trzecim projektem w serii:

| # | Projekt | Kierunek | Opis |
|---|---------|----------|------|
| 1 | DIY Odtwarzacz Kasetowy | Kaseta → Głośnik | Analogowy odtwarzacz z LM386 |
| 2 | DigitalCassettePlayer | Kaseta → ESP32 → SD | Digitalizacja i dekodowanie danych |
| 3 | **TapeForge** | **SD → ESP32 → Kaseta** | **Zapis danych i audio na kasetę** |

Razem tworzą kompletny ekosystem: odtwarzanie, digitalizacja i nagrywanie.

---

## 2. Teoria — generowanie sygnału audio przez DAC

### 2.1. DAC w ESP32

ESP32 posiada dwa 8-bitowe przetworniki cyfrowo-analogowe na pinach GPIO25 (DAC1) i GPIO26 (DAC2). Każdy z nich konwertuje wartość cyfrową 0–255 na napięcie 0–3.3V.

```
Wartość DAC    Napięcie wyjściowe
─────────────  ──────────────────
    0          0.00 V
   64          0.83 V
  128          1.65 V  ← punkt środkowy (cisza)
  192          2.48 V
  255          3.30 V
```

Aby wygenerować falę sinusoidalną 1 kHz, DAC musi być aktualizowany z częstotliwością znacznie wyższą niż 1 kHz — typowo 44100 razy na sekundę (jak na płycie CD). Timer sprzętowy ESP32 wywołuje przerwanie co ~22.7 µs, a procedura ISR ustawia nową wartość DAC obliczoną z tablicy sinusowej lub wzoru kodowania.

### 2.2. Generowanie sygnału FSK (Kansas City Standard)

Dla KCS potrzebujemy dwóch częstotliwości: 1200 Hz (bit "0") i 2400 Hz (bit "1"). Generujemy je jako ciągi sinusów:

```
Bit "0" — 4 cykle sinusa 1200 Hz:

 1.0 ┤  ╭─╮     ╭─╮     ╭─╮     ╭─╮
     │ ╭╯ ╰╮   ╭╯ ╰╮   ╭╯ ╰╮   ╭╯ ╰╮
 0.0 ┤─╯   ╰───╯   ╰───╯   ╰───╯   ╰──
     │
-1.0 ┤
     └───────────────────────────────────
       |<──── 1 bit = 3.33 ms ────────>|

Bit "1" — 8 cykli sinusa 2400 Hz:

 1.0 ┤ ╭╮ ╭╮ ╭╮ ╭╮ ╭╮ ╭╮ ╭╮ ╭╮
     │╭╯╰╮╯╰╮╯╰╮╯╰╮╯╰╮╯╰╮╯╰╮╯╰╮
 0.0 ┤╯  ╰╯ ╰╯ ╰╯ ╰╯ ╰╯ ╰╯ ╰╯ ╰─
     │
-1.0 ┤
     └───────────────────────────────────
       |<──── 1 bit = 3.33 ms ────────>|
```

Oba bity trwają tyle samo (~3.33 ms), ale zawierają różną liczbę cykli — to jest serce FSK.

### 2.3. Generowanie impulsów Commodore 64

C64 używa fali prostokątnej o zmiennej szerokości impulsu. DAC generuje przejścia między dwoma poziomami (niski ≈ 64, wysoki ≈ 192) z precyzyjnie odmierzonym czasem:

```
Impuls "short" (352 cykle zegara C64 ≈ 363 µs):
  ┌────┐
  │    │
──┘    └─────────

Impuls "medium" (512 cykli ≈ 528 µs):
  ┌──────┐
  │      │
──┘      └───────

Impuls "long" (672 cykli ≈ 692 µs):
  ┌────────┐
  │        │
──┘        └─────
```

Pary impulsów kodują bity: short+medium = "0", medium+short = "1".

### 2.4. Generowanie sygnału ZX Spectrum

Spectrum używa podobnego schematu jak C64, ale z innymi czasami i kolejnością bitów (MSB first):

Pilot: impulsy ~619 µs (powtarzane 8063 razy dla nagłówka). Sync: dwa impulsy 190 µs + 210 µs. Dane: bit "0" = 2 × 244 µs, bit "1" = 2 × 489 µs.

### 2.5. Odtwarzanie pliku WAV

Plik WAV to surowe próbki audio z nagłówkiem 44 bajtów opisującym format. TapeForge parsuje nagłówek, odczytuje próbki z karty SD i wysyła je na DAC z odpowiednią częstotliwością.

Konwersje wykonywane w locie: stereo → mono (uśrednianie kanałów), 16-bit → 8-bit (przesunięcie o 8 bitów + offset 128), resampling jeśli sample rate pliku ≠ sample rate DAC.

```
Struktura pliku WAV:

Bajty 0–3:    "RIFF"
Bajty 4–7:    Rozmiar pliku - 8
Bajty 8–11:   "WAVE"
Bajty 12–15:  "fmt "
Bajty 16–19:  Rozmiar bloku fmt (16 dla PCM)
Bajty 20–21:  Format audio (1 = PCM)
Bajty 22–23:  Liczba kanałów (1 = mono, 2 = stereo)
Bajty 24–27:  Sample rate (np. 44100)
Bajty 28–31:  Byte rate
Bajty 32–33:  Block align
Bajty 34–35:  Bits per sample (8 lub 16)
Bajty 36–39:  "data"
Bajty 40–43:  Rozmiar danych
Bajty 44+:    Próbki audio (surowe dane PCM)
```

### 2.6. Dlaczego sinus a nie prostokąt?

Dla danych cyfrowych (C64, Spectrum) fala prostokątna jest idealna — oryginalne komputery generowały właśnie takie sygnały. Ale dla KCS i audio lepszy jest sinus, ponieważ magnetofon i taśma mają ograniczone pasmo przenoszenia (~50 Hz – 15 kHz). Fala prostokątna zawiera harmoniczne (3f, 5f, 7f...), które mogą zostać obcięte przez tor nagrywający magnetofonu i spowodować zniekształcenia. Sinus jest "czysty" — zawiera tylko jedną częstotliwość.

W praktyce TapeForge oferuje oba tryby: sinus dla KCS/audio i prostokąt dla C64/Spectrum (tak jak oryginalne komputery).

---

## 3. Architektura systemu

### 3.1. Schemat blokowy

```
┌────────────┐    SPI     ┌──────────────┐
│  microSD   │◄──────────►│              │
│  karta     │            │              │    GPIO25 (DAC1)     ┌───────────┐
│            │            │    ESP32     │───────────────────►│ Filtr RC  │
│ • plik.TAP │            │              │                     │ wyjściowy │
│ • plik.TZX │            │  • Enkoder   │    GPIO26 (DAC2)    │ R=1kΩ    │
│ • plik.WAV │            │    KCS/C64   │───────────────────►│ C=100nF   │
│ • plik.BIN │            │    /ZX       │                     └─────┬─────┘
└────────────┘            │              │                           │
                          │  • Parser    │                    Kabel audio
┌────────────┐    I2C     │    WAV       │                    jack 3.5mm
│  OLED      │◄──────────►│              │                           │
│  SSD1306   │            │  • Generator │                           ▼
│            │            │    DAC       │                  ┌─────────────────┐
│ • tryb     │            │              │                  │  MAGNETOFON     │
│ • postęp   │            │  • UI/menu   │                  │  (Retekess      │
│ • VU-metr  │            │              │                  │   TR621 lub     │
└────────────┘            └──────┬───────┘                  │   inny z        │
                                 │                          │   LINE IN/AUX)  │
                          ┌──────┴───────┐                  │                 │
                          │  3 przyciski │                  │  ► REC ◄        │
                          │  PLAY  MODE  │                  │                 │
                          │  SELECT      │                  └─────────────────┘
                          └──────────────┘                           │
                                                                     ▼
                                                              ┌─────────────┐
                                                              │   KASETA    │
                                                              │  z danymi   │
                                                              │  lub muzyką │
                                                              └─────────────┘
```

### 3.2. Tor wyjściowy DAC → Magnetofon

Sygnał z DAC ESP32 (0–3.3V, 8-bit) wymaga przygotowania przed podaniem na wejście LINE IN magnetofonu:

```
  ESP32                Filtr RC            Dzielnik          Kabel
  GPIO25 ───┬─── R8 ──┬─── C7 ──────── R9 ──┬──────── Jack 3.5mm
  (DAC1)    │  (1kΩ)  │  (100nF)     (10kΩ) │         TIP (sygnał)
            │         │                      │
            │        GND              R10    │
            │                        (10kΩ)  │
            │                          │     │
           GND                        GND   GND ──── Jack 3.5mm
                                                     SLEEVE (masa)

  R8 + C7: Filtr dolnoprzepustowy fc ≈ 1.6 kHz (wygładza schodki DAC)
            Dla audio: zamień C7 na 10nF → fc ≈ 16 kHz
  R9 + R10: Dzielnik napięcia 1:1 → zmniejsza amplitudę z ~3.3Vpp do ~1.65Vpp
            Magnetofon LINE IN oczekuje sygnału ~0.5-1V RMS
```

Uwaga o dwóch wariantach filtru: dla danych cyfrowych (KCS, C64, Spectrum) wystarczy filtr z fc ≈ 1.6 kHz — dane mają pasmo do ~2.4 kHz. Dla audio potrzebne jest szersze pasmo, więc C7 powinno być 10 nF (fc ≈ 16 kHz). TapeForge w firmware informuje użytkownika, który kondensator podłączyć, ale w praktyce 10 nF działa dobrze dla obu zastosowań — po prostu sygnał danych będzie miał nieco więcej szumu powyżej 3 kHz, co nie przeszkadza dekoderom.

### 3.3. Połączenie z magnetofonem

Potrzebny jest kabel audio z wtykiem jack 3.5mm (męski) na obu końcach. Jeden koniec do wyjścia TapeForge, drugi do wejścia LINE IN / AUX IN magnetofonu.

Jeśli magnetofon ma tylko wejście mikrofonowe (MIC) zamiast LINE IN, sygnał będzie za mocny. Wtedy zmień dzielnik R9/R10 na 47kΩ/4.7kΩ (tłumienie ~10:1) lub użyj potencjometru 10kΩ jako regulatora poziomu.

---

## 4. Lista materiałów (BOM)

### 4.1. Elementy elektroniczne

| # | Ref | Element | Wartość | Opis | Cena |
|---|-----|---------|---------|------|------|
| 1 | — | ESP32 DevKit V1 | — | Mikrokontroler z DAC, WiFi | 20–35 zł |
| 2 | — | Moduł microSD | SPI | Czytnik kart (3.3V) | 3–8 zł |
| 3 | — | Karta microSD | 4–32 GB | FAT32, Class 10 | 10–20 zł |
| 4 | — | OLED 0.96" | SSD1306 I2C | Wyświetlacz 128×64 | 8–15 zł |
| 5 | R8 | Rezystor | 1 kΩ | Filtr RC wyjściowy | ~0.10 zł |
| 6 | C7 | Kondensator ceramiczny | 10 nF | Filtr RC (fc ≈ 16 kHz) | ~0.20 zł |
| 7 | R9 | Rezystor | 10 kΩ | Dzielnik napięcia (górny) | ~0.10 zł |
| 8 | R10 | Rezystor | 10 kΩ | Dzielnik napięcia (dolny) | ~0.10 zł |
| 9 | — | Gniazdo jack 3.5mm | Stereo, montaż panel | Wyjście audio | ~2 zł |
| 10 | — | Kabel audio jack-jack | 3.5mm M-M, ~1m | Do magnetofonu | 3–8 zł |
| 11 | — | Przyciski tact switch | 3 szt. | PLAY/MODE/SELECT | ~1 zł |
| 12 | — | Płytka stykowa | 400/830 pkt | Breadboard | 5–10 zł |
| 13 | — | Kabelki jumper | M-M, zestaw | Połączenia | ~5 zł |

### 4.2. Magnetofon nagrywający

| Opcja | Model | Wymagania | Cena |
|-------|-------|-----------|------|
| Nowy | Retekess TR621 | CD, kaseta z nagrywaniem, AUX IN | ~330 zł |
| Używany | Panasonic RX-CT810 | Dwukasetowy, LINE IN, nagrywanie | 80–200 zł |
| Używany | Sony CFS-W338 | Dwukasetowy, MIC/AUX, nagrywanie | 100–250 zł |
| Używany | Sharp WQ-267Z | Kompaktowy, nagrywanie | 60–150 zł |

Kluczowe: magnetofon musi mieć funkcję nagrywania (nie tylko odtwarzanie) i najlepiej wejście LINE IN / AUX IN (jack 3.5mm lub RCA). Wejście MIC też zadziała, ale wymaga tłumienia sygnału.

### 4.3. Kasety

| Typ | Opis | Zalecenie | Cena |
|-----|------|-----------|------|
| C-60 | 30 min na stronę | Optymalne — krótka taśma = mniej problemów z napięciem | 3–10 zł |
| C-90 | 45 min na stronę | OK, cieńsza taśma, bardziej podatna na zmiętosienie | 3–10 zł |
| Type I (Normal) | Fe₂O₃ | Wystarczające dla danych. Tańsze | 3–5 zł |
| Type II (Chrome) | CrO₂ | Lepsze dla muzyki. Droższe | 5–15 zł |

Nowe czyste kasety: Maxell UR-60 lub UR-90 — wciąż produkowane i dostępne w sklepach muzycznych, na Allegro, Amazon. Do zapisu danych wystarczy najtańsza kaseta Type I.

### 4.4. Podsumowanie kosztów

| Kategoria | Koszt |
|-----------|-------|
| ESP32 + moduły (SD, OLED) | 40–70 zł |
| Elementy pasywne toru DAC | ~5 zł |
| Kabel audio + gniazdo jack | 5–10 zł |
| Kasety (5 szt.) | 15–30 zł |
| Breadboard + kabelki | 10–15 zł |
| **Razem (bez magnetofonu)** | **~75–130 zł** |
| Magnetofon (używany) | 60–250 zł |

---

## 5. Schemat połączeń

### 5.1. Pinout ESP32

| GPIO ESP32 | Funkcja | Podłączone do |
|------------|---------|---------------|
| GPIO25 | DAC1 (wyjście audio) | R8 → filtr RC → dzielnik → jack 3.5mm |
| GPIO5 | SPI CS | Moduł microSD (CS) |
| GPIO18 | SPI CLK | Moduł microSD (SCK) |
| GPIO23 | SPI MOSI | Moduł microSD (MOSI) |
| GPIO19 | SPI MISO | Moduł microSD (MISO) |
| GPIO21 | I2C SDA | OLED SSD1306 (SDA) |
| GPIO22 | I2C SCL | OLED SSD1306 (SCL) |
| GPIO12 | Input (pull-up) | Przycisk PLAY/STOP |
| GPIO13 | Input (pull-up) | Przycisk MODE |
| GPIO14 | Input (pull-up) | Przycisk SELECT (przeglądanie plików) |
| 3.3V | Zasilanie | OLED VCC, SD VCC |
| GND | Masa wspólna | Wszystko |
| VIN / USB | Zasilanie ESP32 | 5V USB lub 7–12V |

### 5.2. Tor wyjściowy — schemat na breadboardzie

```
ESP32 GPIO25 ────────── R8 (1kΩ) ────┬──── R9 (10kΩ) ────┬──── Jack 3.5mm TIP
                                       │                    │     (sygnał)
                                    C7 (10nF)           R10 (10kΩ)
                                       │                    │
ESP32 GND ─────────────────────────── GND ──────────────── GND ── Jack 3.5mm
                                                                  SLEEVE (masa)
```

### 5.3. Moduł microSD (SPI)

```
ESP32          Moduł SD
─────          ─────────
GPIO5   ────── CS
GPIO18  ────── SCK
GPIO23  ────── MOSI
GPIO19  ────── MISO
3.3V    ────── VCC
GND     ────── GND
```

### 5.4. OLED SSD1306 (I2C)

```
ESP32          OLED
─────          ────
GPIO21  ────── SDA
GPIO22  ────── SCL
3.3V    ────── VCC
GND     ────── GND
```

---

## 6. Przygotowanie plików na karcie SD

### 6.1. Struktura katalogów

```
microSD (FAT32)
├── /c64/
│   ├── game1.tap
│   ├── game2.tap
│   └── demo.prg         ← TapeForge automatycznie opakuje w format TAP
├── /spectrum/
│   ├── manic_miner.tzx
│   └── jetset_willy.tap
├── /kcs/
│   ├── program.bin
│   └── data.bin
├── /audio/
│   ├── song1.wav
│   ├── mixtape.wav
│   └── podcast.wav
└── /config/
    └── tapeforge.cfg    ← opcjonalny plik konfiguracyjny
```

### 6.2. Wymagania dla plików

**Pliki .TAP (C64):** Format opisany w dokumentacji emulatora VICE. Nagłówek 20 bajtów + surowe dane impulsów. Dostępne w archiwach: archive.org, csdb.dk, c64.com.

**Pliki .TZX (Spectrum):** Uniwersalny format kasetowy. Dostępne: worldofspectrum.org, spectrumcomputing.co.uk.

**Pliki .WAV (audio):** Obsługiwane formaty: PCM 8-bit unsigned lub 16-bit signed, mono lub stereo, sample rate 8000–44100 Hz. TapeForge konwertuje w locie do 8-bit mono odpowiedniego dla DAC.

**Pliki .BIN (KCS):** Dowolne dane binarne — TapeForge opakuje je w ramki KCS (bit startu, 8 bitów danych, 2 bity stopu) z pilotem i synchronizacją.

### 6.3. Konwersja plików audio do WAV

Jeśli masz pliki MP3, FLAC lub inne formaty, skonwertuj je do WAV przed umieszczeniem na karcie SD. Najłatwiej przez Audacity (darmowy) lub ffmpeg:

```bash
# Konwersja MP3 → WAV mono 44100 Hz 16-bit
ffmpeg -i song.mp3 -ac 1 -ar 44100 -sample_fmt s16 song.wav

# Konwersja FLAC → WAV mono 22050 Hz (mniejszy plik, wystarczy na kasetę)
ffmpeg -i album.flac -ac 1 -ar 22050 -sample_fmt s16 album.wav

# Konwersja z redukcją do 8-bit (bezpośrednio kompatybilne z DAC)
ffmpeg -i song.mp3 -ac 1 -ar 22050 -acodec pcm_u8 song_8bit.wav
```

---

## 7. Firmware — kompletny kod

### 7.1. Wymagane biblioteki

```
- Arduino ESP32 Core (Espressif, Board Manager)
- Adafruit SSD1306
- Adafruit GFX
- SD (wbudowana)
- SPI, Wire (wbudowane)
```

### 7.2. Kompletny firmware TapeForge

```cpp
/*
 * TapeForge — ESP32 Cassette Tape Writer
 * ========================================
 * Wersja: 1.0
 * Data: 2026-02-06
 * 
 * Nagrywa na kasety magnetofonowe:
 *  - Dane C64 z plików .TAP
 *  - Dane ZX Spectrum z plików .TZX / .TAP
 *  - Dane KCS z plików .BIN
 *  - Audio z plików .WAV
 * 
 * Wyjście: DAC GPIO25 → filtr RC → LINE IN magnetofonu
 * 
 * Licencja: Open Source / Edukacyjny
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/dac.h>
#include <driver/timer.h>

// ============================================================
//  KONFIGURACJA PINÓW
// ============================================================

#define DAC_PIN             25          // GPIO25 = DAC1
#define DAC_CHANNEL         DAC_CHANNEL_1
#define SD_CS_PIN           5
#define BTN_PLAY            12
#define BTN_MODE            13
#define BTN_SELECT          14
#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_RESET          -1

// ============================================================
//  STAŁE
// ============================================================

// DAC
#define DAC_SAMPLE_RATE     44100       // Hz — częstotliwość aktualizacji DAC
#define DAC_MID             128         // Punkt środkowy (cisza)
#define DAC_HIGH            210         // Poziom wysoki (prostokąt)
#define DAC_LOW             46          // Poziom niski (prostokąt)
#define DAC_AMPLITUDE       80          // Amplituda sinusa (±80 wokół 128)

// Bufory
#define FILE_BUF_SIZE       4096        // Bufor odczytu SD
#define SINE_TABLE_SIZE     256         // Rozmiar tablicy sinusowej

// KCS
#define KCS_FREQ_ZERO       1200
#define KCS_FREQ_ONE        2400
#define KCS_CYCLES_ZERO     4
#define KCS_CYCLES_ONE      8
#define KCS_PILOT_SECONDS   5           // Czas pilota [s]
#define KCS_SYNC_BYTE       0x16

// C64 — czasy impulsów w µs (standard PAL)
#define C64_SHORT_US        363         // Short pulse
#define C64_MEDIUM_US       528         // Medium pulse
#define C64_LONG_US         692         // Long pulse
#define C64_PILOT_PULSES    20000       // Liczba impulsów pilota (header)
#define C64_PILOT_SHORT     10000       // Pilot przed blokiem danych (krótszy)
#define C64_HEADER_LONG     9           // Countdown markers

// ZX Spectrum — czasy impulsów w µs
#define ZX_PILOT_US         619         // Impuls pilota
#define ZX_PILOT_HEADER     8063        // Impulsy pilota (nagłówek)
#define ZX_PILOT_DATA       3223        // Impulsy pilota (dane)
#define ZX_SYNC1_US         190         // Sync pulse 1
#define ZX_SYNC2_US         210         // Sync pulse 2
#define ZX_ZERO_US          244         // Pół-impuls bitu 0
#define ZX_ONE_US           489         // Pół-impuls bitu 1

// ============================================================
//  TYPY
// ============================================================

enum ForgeMode {
    FORGE_C64,
    FORGE_SPECTRUM,
    FORGE_KCS,
    FORGE_AUDIO,
    FORGE_CALIBRATE,
    FORGE_MODE_COUNT
};

enum ForgeState {
    FSTATE_IDLE,
    FSTATE_BROWSING,
    FSTATE_PLAYING,
    FSTATE_DONE,
    FSTATE_ERROR
};

struct WavInfo {
    uint16_t numChannels;
    uint32_t sampleRate;
    uint16_t bitsPerSample;
    uint32_t dataSize;
    uint32_t dataOffset;
    bool     valid;
};

struct PlaybackStats {
    uint32_t bytesProcessed;
    uint32_t totalBytes;
    uint32_t elapsedMs;
    float    percentDone;
    char     filename[64];
};

// ============================================================
//  ZMIENNE GLOBALNE
// ============================================================

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
File currentFile;

ForgeMode   currentMode   = FORGE_C64;
ForgeState  currentState  = FSTATE_IDLE;
PlaybackStats playStats   = {0};

// Tablica sinusowa (obliczona w setup)
uint8_t sineTable[SINE_TABLE_SIZE];

// Bufor pliku
uint8_t fileBuf[FILE_BUF_SIZE];

// Lista plików w bieżącym katalogu
#define MAX_FILES           50
#define MAX_FILENAME_LEN    48
char fileList[MAX_FILES][MAX_FILENAME_LEN];
int  fileCount = 0;
int  fileIndex = 0;

// Timer do generowania DAC
hw_timer_t *dacTimer = NULL;

// Bufor wyjściowy DAC (ring buffer wypełniany w main loop, konsumowany przez ISR)
#define DAC_BUF_SIZE        8192
volatile uint8_t dacBuf[DAC_BUF_SIZE];
volatile uint32_t dacBufWriteIdx = 0;
volatile uint32_t dacBufReadIdx  = 0;
volatile bool     dacActive      = false;
volatile bool     dacUnderrun    = false;

// Przyciski
volatile bool btnPlayPressed   = false;
volatile bool btnModePressed   = false;
volatile bool btnSelectPressed = false;
volatile uint32_t lastBtnPlay   = 0;
volatile uint32_t lastBtnMode   = 0;
volatile uint32_t lastBtnSelect = 0;
const uint32_t DEBOUNCE_MS      = 200;

bool sdReady   = false;
bool oledReady = false;

const char* modeNames[] = {
    "C64 (.TAP)",
    "Spectrum (.TZX)",
    "KCS (.BIN)",
    "Audio (.WAV)",
    "Kalibracja"
};

const char* modeDirs[] = {
    "/c64",
    "/spectrum",
    "/kcs",
    "/audio",
    ""
};

// ============================================================
//  PRZERWANIE DAC — ODTWARZANIE Z BUFORA
// ============================================================

void IRAM_ATTR onDacTimer() {
    if (!dacActive) {
        dac_output_voltage(DAC_CHANNEL, DAC_MID);
        return;
    }
    
    if (dacBufReadIdx != dacBufWriteIdx) {
        dac_output_voltage(DAC_CHANNEL, dacBuf[dacBufReadIdx]);
        dacBufReadIdx = (dacBufReadIdx + 1) % DAC_BUF_SIZE;
    } else {
        // Underrun — bufor pusty
        dac_output_voltage(DAC_CHANNEL, DAC_MID);
        dacUnderrun = true;
    }
}

// ============================================================
//  PRZERWANIA PRZYCISKÓW
// ============================================================

void IRAM_ATTR onBtnPlay() {
    if (millis() - lastBtnPlay > DEBOUNCE_MS) {
        btnPlayPressed = true;
        lastBtnPlay = millis();
    }
}

void IRAM_ATTR onBtnMode() {
    if (millis() - lastBtnMode > DEBOUNCE_MS) {
        btnModePressed = true;
        lastBtnMode = millis();
    }
}

void IRAM_ATTR onBtnSelect() {
    if (millis() - lastBtnSelect > DEBOUNCE_MS) {
        btnSelectPressed = true;
        lastBtnSelect = millis();
    }
}

// ============================================================
//  INICJALIZACJA
// ============================================================

void generateSineTable() {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        float angle = 2.0f * PI * (float)i / (float)SINE_TABLE_SIZE;
        sineTable[i] = (uint8_t)(DAC_MID + DAC_AMPLITUDE * sin(angle));
    }
    Serial.println("[SINE] Tablica sinusowa wygenerowana (256 próbek)");
}

void setupDAC() {
    dac_output_enable(DAC_CHANNEL);
    dac_output_voltage(DAC_CHANNEL, DAC_MID);
    Serial.println("[DAC] Włączony na GPIO25, wartość początkowa: 128 (1.65V)");
}

void setupDacTimer() {
    dacTimer = timerBegin(1, 80, true);  // Timer 1, prescaler 80 → 1 MHz
    timerAttachInterrupt(dacTimer, &onDacTimer, true);
    timerAlarmWrite(dacTimer, 1000000 / DAC_SAMPLE_RATE, true);
    Serial.printf("[TIMER] DAC: %d Hz (okres: %d µs)\n",
                  DAC_SAMPLE_RATE, 1000000 / DAC_SAMPLE_RATE);
}

void startDac() {
    dacBufReadIdx = 0;
    dacBufWriteIdx = 0;
    dacActive = true;
    dacUnderrun = false;
    timerAlarmEnable(dacTimer);
}

void stopDac() {
    dacActive = false;
    timerAlarmDisable(dacTimer);
    dac_output_voltage(DAC_CHANNEL, DAC_MID);
}

bool setupSD() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[SD] BŁĄD: Karta nie wykryta!");
        return false;
    }
    Serial.printf("[SD] OK: %llu MB\n", SD.cardSize() / (1024 * 1024));
    
    // Utwórz katalogi jeśli nie istnieją
    if (!SD.exists("/c64"))      SD.mkdir("/c64");
    if (!SD.exists("/spectrum")) SD.mkdir("/spectrum");
    if (!SD.exists("/kcs"))      SD.mkdir("/kcs");
    if (!SD.exists("/audio"))    SD.mkdir("/audio");
    
    return true;
}

bool setupOLED() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[OLED] BŁĄD: Nie wykryty!");
        return false;
    }
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);
    display.println("TapeForge");
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.println("ESP32 Tape Writer");
    display.println("v1.0");
    display.println("");
    display.println("Inicjalizacja...");
    display.display();
    return true;
}

void setupButtons() {
    pinMode(BTN_PLAY, INPUT_PULLUP);
    pinMode(BTN_MODE, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BTN_PLAY), onBtnPlay, FALLING);
    attachInterrupt(digitalPinToInterrupt(BTN_MODE), onBtnMode, FALLING);
    attachInterrupt(digitalPinToInterrupt(BTN_SELECT), onBtnSelect, FALLING);
}

// ============================================================
//  BUFOR DAC — FUNKCJE POMOCNICZE
// ============================================================

// Ile miejsca wolnego w buforze DAC
uint32_t dacBufFree() {
    uint32_t w = dacBufWriteIdx;
    uint32_t r = dacBufReadIdx;
    if (w >= r) return DAC_BUF_SIZE - 1 - (w - r);
    return r - w - 1;
}

// Wstaw jedną próbkę do bufora DAC (blokujące jeśli pełny)
void dacWrite(uint8_t sample) {
    while (dacBufFree() == 0) {
        yield();  // Czekaj aż ISR zużyje próbki
    }
    dacBuf[dacBufWriteIdx] = sample;
    dacBufWriteIdx = (dacBufWriteIdx + 1) % DAC_BUF_SIZE;
}

// Wstaw blok próbek
void dacWriteBlock(const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        dacWrite(data[i]);
    }
}

// Czekaj aż bufor się opróżni (wszystkie próbki odtworzone)
void dacFlush() {
    while (dacBufReadIdx != dacBufWriteIdx) {
        yield();
    }
    delay(10);  // Dodatkowy margines
}

// ============================================================
//  GENERATORY SYGNAŁÓW
// ============================================================

// --- Generowanie sinusa o zadanej częstotliwości i czasie trwania ---
void generateSine(float freqHz, uint32_t durationMs) {
    uint32_t totalSamples = (uint32_t)((float)DAC_SAMPLE_RATE * durationMs / 1000.0f);
    float phaseIncrement = freqHz * SINE_TABLE_SIZE / (float)DAC_SAMPLE_RATE;
    float phase = 0;
    
    for (uint32_t i = 0; i < totalSamples; i++) {
        uint32_t idx = (uint32_t)phase % SINE_TABLE_SIZE;
        dacWrite(sineTable[idx]);
        phase += phaseIncrement;
        if (phase >= SINE_TABLE_SIZE) phase -= SINE_TABLE_SIZE;
    }
}

// --- Generowanie ciszy ---
void generateSilence(uint32_t durationMs) {
    uint32_t totalSamples = (uint32_t)((float)DAC_SAMPLE_RATE * durationMs / 1000.0f);
    for (uint32_t i = 0; i < totalSamples; i++) {
        dacWrite(DAC_MID);
    }
}

// --- Generowanie impulsu prostokątnego (dla C64/Spectrum) ---
void generatePulse(uint32_t highUs, uint32_t lowUs) {
    // Faza wysoka
    uint32_t highSamples = (uint32_t)((float)DAC_SAMPLE_RATE * highUs / 1000000.0f);
    for (uint32_t i = 0; i < highSamples; i++) {
        dacWrite(DAC_HIGH);
    }
    // Faza niska
    uint32_t lowSamples = (uint32_t)((float)DAC_SAMPLE_RATE * lowUs / 1000000.0f);
    for (uint32_t i = 0; i < lowSamples; i++) {
        dacWrite(DAC_LOW);
    }
}

// Symetryczny impuls (wysoki+niski o tym samym czasie)
void generateSymmetricPulse(uint32_t halfPeriodUs) {
    generatePulse(halfPeriodUs, halfPeriodUs);
}

// ============================================================
//  ENKODER: Kansas City Standard
// ============================================================

void kcsEncodeBit(bool bitValue) {
    if (bitValue) {
        // Bit "1": 8 cykli 2400 Hz (sinus)
        float periodMs = 1000.0f / KCS_FREQ_ONE;
        for (int c = 0; c < KCS_CYCLES_ONE; c++) {
            generateSine(KCS_FREQ_ONE, periodMs);
        }
    } else {
        // Bit "0": 4 cykle 1200 Hz (sinus)
        float periodMs = 1000.0f / KCS_FREQ_ZERO;
        for (int c = 0; c < KCS_CYCLES_ZERO; c++) {
            generateSine(KCS_FREQ_ZERO, periodMs);
        }
    }
}

void kcsEncodeByte(uint8_t byte) {
    // Bit startu (0)
    kcsEncodeBit(false);
    
    // 8 bitów danych (LSB first)
    for (int i = 0; i < 8; i++) {
        kcsEncodeBit((byte >> i) & 1);
    }
    
    // 2 bity stopu (1, 1)
    kcsEncodeBit(true);
    kcsEncodeBit(true);
}

void kcsEncodePilot() {
    Serial.printf("[KCS] Generuję pilot (%d s)...\n", KCS_PILOT_SECONDS);
    // Pilot = ciągły ton 2400 Hz
    generateSine(KCS_FREQ_ONE, KCS_PILOT_SECONDS * 1000);
}

void kcsEncodeData(uint8_t* data, uint32_t length) {
    Serial.printf("[KCS] Koduję %lu bajtów...\n", length);
    
    // Pilot
    kcsEncodePilot();
    
    // Bajt synchronizacji
    kcsEncodeByte(KCS_SYNC_BYTE);
    
    // Dane
    uint8_t checksum = 0;
    for (uint32_t i = 0; i < length; i++) {
        kcsEncodeByte(data[i]);
        checksum ^= data[i];
        
        playStats.bytesProcessed = i + 1;
        playStats.percentDone = 100.0f * (i + 1) / length;
        
        if (i % 100 == 0) {
            Serial.printf("[KCS] Postęp: %lu/%lu bajtów (%.1f%%)\n",
                          i + 1, length, playStats.percentDone);
        }
    }
    
    // Checksum
    kcsEncodeByte(checksum);
    
    // Końcowa cisza
    generateSilence(500);
    
    Serial.printf("[KCS] Zakończono. Checksum: 0x%02X\n", checksum);
}

// ============================================================
//  ENKODER: Commodore 64 (.TAP)
// ============================================================

void c64GeneratePilot(uint32_t numPulses) {
    Serial.printf("[C64] Pilot: %lu impulsów short...\n", numPulses);
    for (uint32_t i = 0; i < numPulses; i++) {
        generateSymmetricPulse(C64_SHORT_US / 2);
        
        if (i % 5000 == 0 && i > 0) {
            Serial.printf("[C64] Pilot: %lu/%lu\n", i, numPulses);
        }
    }
}

void c64GenerateCountdown() {
    // Countdown: sekwencja długich impulsów (marker synchronizacji)
    Serial.println("[C64] Countdown...");
    for (int i = 0; i < C64_HEADER_LONG; i++) {
        generateSymmetricPulse(C64_LONG_US / 2);
    }
}

void c64EncodeBit(bool bitValue) {
    if (bitValue) {
        // Bit 1: medium + short
        generateSymmetricPulse(C64_MEDIUM_US / 2);
        generateSymmetricPulse(C64_SHORT_US / 2);
    } else {
        // Bit 0: short + medium
        generateSymmetricPulse(C64_SHORT_US / 2);
        generateSymmetricPulse(C64_MEDIUM_US / 2);
    }
}

void c64EncodeByte(uint8_t byte) {
    // Marker nowego bajtu (1 impuls long)
    generateSymmetricPulse(C64_LONG_US / 2);
    
    // 8 bitów (LSB first)
    uint8_t parity = 1;  // Odd parity
    for (int i = 0; i < 8; i++) {
        bool bit = (byte >> i) & 1;
        c64EncodeBit(bit);
        parity ^= bit;
    }
    
    // Bit parzystości
    c64EncodeBit(parity);
}

void c64EncodeFromTAP(File &tapFile) {
    // Format TAP: 20-bajtowy nagłówek + surowe czasy impulsów
    
    // Czytaj nagłówek TAP
    uint8_t header[20];
    if (tapFile.read(header, 20) != 20) {
        Serial.println("[C64] BŁĄD: Za krótki plik TAP");
        return;
    }
    
    // Sprawdź sygnaturę "C64-TAPE-RAW"
    if (memcmp(header, "C64-TAPE-RAW", 12) != 0) {
        Serial.println("[C64] UWAGA: Niestandardowy nagłówek TAP, próbuję kontynuować");
    }
    
    uint8_t tapVersion = header[12];
    uint32_t dataLen = header[16] | (header[17] << 8) | (header[18] << 16) | (header[19] << 24);
    
    Serial.printf("[C64] TAP v%d, dane: %lu bajtów\n", tapVersion, dataLen);
    
    playStats.totalBytes = dataLen;
    
    // Czytaj i generuj impulsy
    uint32_t processed = 0;
    while (tapFile.available() && processed < dataLen) {
        uint8_t pulse = tapFile.read();
        processed++;
        
        uint32_t pulseDuration;
        
        if (pulse > 0) {
            // Standardowy impuls: czas = pulse × 8 / 0.985 µs (PAL)
            pulseDuration = (uint32_t)(pulse * 8.12f);  // ~8.12 µs per unit (PAL)
        } else {
            // Pulse = 0: rozszerzony format (TAP v1)
            if (tapVersion >= 1 && tapFile.available() >= 3) {
                uint8_t b0 = tapFile.read();
                uint8_t b1 = tapFile.read();
                uint8_t b2 = tapFile.read();
                processed += 3;
                pulseDuration = (b0 | (b1 << 8) | (b2 << 16));
                pulseDuration = (uint32_t)(pulseDuration / 0.985f);
            } else {
                pulseDuration = 0;  // Pomiń
            }
        }
        
        if (pulseDuration > 0) {
            // Generuj impuls: połowa wysoko, połowa nisko
            generatePulse(pulseDuration / 2, pulseDuration / 2);
        }
        
        playStats.bytesProcessed = processed;
        playStats.percentDone = 100.0f * processed / dataLen;
        
        if (processed % 10000 == 0) {
            Serial.printf("[C64] TAP: %lu/%lu (%.1f%%)\n",
                          processed, dataLen, playStats.percentDone);
        }
    }
    
    // Końcowa cisza
    generateSilence(1000);
    Serial.printf("[C64] TAP zakończony: %lu impulsów\n", processed);
}

void c64EncodeFromBinary(uint8_t* data, uint32_t length, const char* filename) {
    // Jeśli plik nie jest .TAP, koduj surowe bajty w formacie C64
    Serial.printf("[C64] Koduję %lu bajtów binarnych jako C64...\n", length);
    
    // === Blok nagłówka ===
    c64GeneratePilot(C64_PILOT_PULSES);
    c64GenerateCountdown();
    
    // Nagłówek: typ (1=PRG), adres startowy, adres końcowy, nazwa
    uint8_t headerBlock[192] = {0};
    headerBlock[0] = 0x03;  // Typ: sequential file
    // Adres startowy: 0x0801 (standardowy BASIC)
    headerBlock[1] = 0x01;
    headerBlock[2] = 0x08;
    // Adres końcowy
    uint16_t endAddr = 0x0801 + length;
    headerBlock[3] = endAddr & 0xFF;
    headerBlock[4] = (endAddr >> 8) & 0xFF;
    // Nazwa pliku (max 16 znaków, padded spacjami)
    strncpy((char*)&headerBlock[5], filename, 16);
    for (int i = strlen(filename) + 5; i < 21; i++) headerBlock[i] = 0x20;
    
    uint8_t checksum = 0;
    for (int i = 0; i < 192; i++) {
        c64EncodeByte(headerBlock[i]);
        checksum ^= headerBlock[i];
    }
    c64EncodeByte(checksum);
    
    // === Przerwa między blokami ===
    generateSilence(2000);
    
    // === Blok danych ===
    c64GeneratePilot(C64_PILOT_SHORT);
    c64GenerateCountdown();
    
    checksum = 0;
    uint32_t offset = 0;
    while (offset < length) {
        uint32_t blockSize = min((uint32_t)192, length - offset);
        
        for (uint32_t i = 0; i < blockSize; i++) {
            c64EncodeByte(data[offset + i]);
            checksum ^= data[offset + i];
        }
        // Pad reszty bloku zerami
        for (uint32_t i = blockSize; i < 192; i++) {
            c64EncodeByte(0x00);
            checksum ^= 0x00;
        }
        
        c64EncodeByte(checksum);
        offset += blockSize;
        
        playStats.bytesProcessed = offset;
        playStats.percentDone = 100.0f * offset / length;
    }
    
    generateSilence(1000);
    Serial.printf("[C64] Zakończono kodowanie %lu bajtów\n", length);
}

// ============================================================
//  ENKODER: ZX Spectrum
// ============================================================

void zxGeneratePilot(uint32_t numPulses) {
    Serial.printf("[ZX] Pilot: %lu impulsów...\n", numPulses);
    for (uint32_t i = 0; i < numPulses; i++) {
        generateSymmetricPulse(ZX_PILOT_US / 2);
    }
}

void zxGenerateSync() {
    generatePulse(ZX_SYNC1_US, ZX_SYNC1_US);
    generatePulse(ZX_SYNC2_US, ZX_SYNC2_US);
}

void zxEncodeBit(bool bitValue) {
    uint32_t pulseUs = bitValue ? ZX_ONE_US : ZX_ZERO_US;
    // Każdy bit = 2 symetryczne impulsy
    generateSymmetricPulse(pulseUs);
    generateSymmetricPulse(pulseUs);
}

void zxEncodeByte(uint8_t byte) {
    // MSB first (Spectrum)
    for (int i = 7; i >= 0; i--) {
        zxEncodeBit((byte >> i) & 1);
    }
}

void zxEncodeBlock(uint8_t flagByte, uint8_t* data, uint32_t length) {
    // Pilot (liczba impulsów zależy od typu bloku)
    uint32_t pilotPulses = (flagByte < 128) ? ZX_PILOT_HEADER : ZX_PILOT_DATA;
    zxGeneratePilot(pilotPulses);
    
    // Sync
    zxGenerateSync();
    
    // Flag byte
    zxEncodeByte(flagByte);
    
    // Dane
    uint8_t checksum = flagByte;
    for (uint32_t i = 0; i < length; i++) {
        zxEncodeByte(data[i]);
        checksum ^= data[i];
        
        playStats.bytesProcessed += 1;
        playStats.percentDone = 100.0f * playStats.bytesProcessed / playStats.totalBytes;
    }
    
    // Checksum
    zxEncodeByte(checksum);
    
    Serial.printf("[ZX] Blok: flag=0x%02X, %lu bajtów, chk=0x%02X\n",
                  flagByte, length, checksum);
}

void zxEncodeFromTZX(File &tzxFile) {
    // Podstawowy parser TZX — obsługuje najczęstsze bloki
    
    uint8_t tzxHeader[10];
    if (tzxFile.read(tzxHeader, 10) != 10) {
        Serial.println("[ZX] BŁĄD: Za krótki plik TZX");
        return;
    }
    
    if (memcmp(tzxHeader, "ZXTape!", 7) != 0) {
        Serial.println("[ZX] BŁĄD: Nieprawidłowa sygnatura TZX");
        return;
    }
    
    uint8_t verMajor = tzxHeader[8];
    uint8_t verMinor = tzxHeader[9];
    Serial.printf("[ZX] TZX v%d.%d\n", verMajor, verMinor);
    
    playStats.totalBytes = tzxFile.size();
    
    while (tzxFile.available()) {
        uint8_t blockId = tzxFile.read();
        
        switch (blockId) {
            case 0x10: {
                // Standard Speed Data Block
                uint16_t pauseMs = tzxFile.read() | (tzxFile.read() << 8);
                uint16_t dataLen = tzxFile.read() | (tzxFile.read() << 8);
                
                Serial.printf("[TZX] Block 0x10: %u bajtów, pauza %u ms\n", dataLen, pauseMs);
                
                // Czytaj dane bloku
                uint8_t* blockData = (uint8_t*)malloc(dataLen);
                if (blockData && tzxFile.read(blockData, dataLen) == dataLen) {
                    uint8_t flagByte = blockData[0];
                    zxEncodeBlock(flagByte, blockData + 1, dataLen - 1);
                    free(blockData);
                }
                
                if (pauseMs > 0) {
                    generateSilence(pauseMs);
                }
                break;
            }
            
            case 0x11: {
                // Turbo Speed Data Block — pomijamy szczegóły, czytamy długość
                uint8_t turboHeader[15];
                tzxFile.read(turboHeader, 15);
                uint32_t dataLen = turboHeader[12] | (turboHeader[13] << 8) | (turboHeader[14] << 16);
                
                Serial.printf("[TZX] Block 0x11 (turbo): %lu bajtów — pomijam\n", dataLen);
                tzxFile.seek(tzxFile.position() + dataLen);
                break;
            }
            
            case 0x12: {
                // Pure Tone
                uint16_t pulseLen = tzxFile.read() | (tzxFile.read() << 8);
                uint16_t numPulses = tzxFile.read() | (tzxFile.read() << 8);
                
                float pulseUs = pulseLen / 3.5f;  // T-states → µs
                for (uint16_t i = 0; i < numPulses; i++) {
                    generateSymmetricPulse((uint32_t)pulseUs);
                }
                break;
            }
            
            case 0x20: {
                // Pause / Stop the tape
                uint16_t pauseMs = tzxFile.read() | (tzxFile.read() << 8);
                if (pauseMs == 0) {
                    Serial.println("[TZX] Stop the tape");
                    goto tzx_done;
                }
                generateSilence(pauseMs);
                break;
            }
            
            case 0x30: {
                // Text description
                uint8_t len = tzxFile.read();
                char text[256];
                tzxFile.read((uint8_t*)text, min((int)len, 255));
                text[min((int)len, 255)] = 0;
                Serial.printf("[TZX] Opis: %s\n", text);
                break;
            }
            
            default:
                Serial.printf("[TZX] Nieobsługiwany blok: 0x%02X — przerywam\n", blockId);
                goto tzx_done;
        }
        
        playStats.bytesProcessed = tzxFile.position();
        playStats.percentDone = 100.0f * tzxFile.position() / tzxFile.size();
    }
    
    tzx_done:
    generateSilence(1000);
    Serial.println("[ZX] TZX zakończony");
}

void zxEncodeFromBinary(uint8_t* data, uint32_t length, const char* filename) {
    // Koduj surowe dane jako blok Spectrum (nagłówek + dane)
    Serial.printf("[ZX] Koduję %lu bajtów jako Spectrum...\n", length);
    
    playStats.totalBytes = length;
    playStats.bytesProcessed = 0;
    
    // === Blok nagłówka (flag = 0x00) ===
    uint8_t header[17];
    header[0] = 3;  // Typ: Code (bytes)
    
    // Nazwa (10 znaków, padded spacjami)
    memset(&header[1], 0x20, 10);
    strncpy((char*)&header[1], filename, 10);
    
    // Długość danych
    header[11] = length & 0xFF;
    header[12] = (length >> 8) & 0xFF;
    
    // Parametr 1 (adres startowy)
    header[13] = 0x00;  // 0x8000
    header[14] = 0x80;
    
    // Parametr 2
    header[15] = 0x00;
    header[16] = 0x00;
    
    zxEncodeBlock(0x00, header, 17);  // Flag 0x00 = nagłówek
    
    generateSilence(1000);
    
    // === Blok danych (flag = 0xFF) ===
    zxEncodeBlock(0xFF, data, length);
    
    generateSilence(1000);
    Serial.printf("[ZX] Zakończono\n");
}

// ============================================================
//  ODTWARZACZ WAV
// ============================================================

WavInfo parseWavHeader(File &wavFile) {
    WavInfo info = {0, 0, 0, 0, 0, false};
    uint8_t header[44];
    
    if (wavFile.read(header, 44) != 44) {
        Serial.println("[WAV] BŁĄD: Plik za krótki");
        return info;
    }
    
    // Sprawdź "RIFF" i "WAVE"
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        Serial.println("[WAV] BŁĄD: Nieprawidłowy nagłówek WAV");
        return info;
    }
    
    // Sprawdź PCM
    uint16_t audioFormat = header[20] | (header[21] << 8);
    if (audioFormat != 1) {
        Serial.printf("[WAV] BŁĄD: Nieobsługiwany format: %d (wymagany PCM=1)\n", audioFormat);
        return info;
    }
    
    info.numChannels   = header[22] | (header[23] << 8);
    info.sampleRate    = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    info.bitsPerSample = header[34] | (header[35] << 8);
    info.dataSize      = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);
    info.dataOffset    = 44;
    info.valid         = true;
    
    Serial.printf("[WAV] Format: %d kanałów, %lu Hz, %d-bit, dane: %lu bajtów\n",
                  info.numChannels, info.sampleRate, info.bitsPerSample, info.dataSize);
    Serial.printf("[WAV] Czas trwania: %.1f s\n",
                  (float)info.dataSize / (info.sampleRate * info.numChannels * (info.bitsPerSample / 8)));
    
    return info;
}

void playWavFile(File &wavFile) {
    WavInfo info = parseWavHeader(wavFile);
    if (!info.valid) return;
    
    playStats.totalBytes = info.dataSize;
    playStats.bytesProcessed = 0;
    
    // Oblicz stosunek sample rate pliku do DAC sample rate
    float sampleRatio = (float)info.sampleRate / (float)DAC_SAMPLE_RATE;
    float samplePos = 0;
    
    uint32_t bytesPerSample = info.numChannels * (info.bitsPerSample / 8);
    uint32_t samplesTotal = info.dataSize / bytesPerSample;
    
    Serial.printf("[WAV] Odtwarzam: %lu próbek, ratio: %.3f\n", samplesTotal, sampleRatio);
    
    // Bufor odczytu
    uint32_t bufSamples = FILE_BUF_SIZE / bytesPerSample;
    uint8_t readBuf[FILE_BUF_SIZE];
    uint32_t samplesRead = 0;
    uint32_t bufPos = 0;
    uint32_t bufAvail = 0;
    
    wavFile.seek(info.dataOffset);
    
    uint32_t dacSamplesGenerated = 0;
    uint32_t totalDacSamples = (uint32_t)((float)samplesTotal / sampleRatio);
    
    while (dacSamplesGenerated < totalDacSamples) {
        // Potrzebujemy próbkę na pozycji samplePos w pliku źródłowym
        uint32_t srcSampleIdx = (uint32_t)samplePos;
        
        // Załaduj dane z SD jeśli trzeba
        while (srcSampleIdx >= samplesRead + bufAvail / bytesPerSample) {
            uint32_t toRead = min((uint32_t)FILE_BUF_SIZE, info.dataSize - (samplesRead * bytesPerSample + bufAvail));
            if (toRead == 0) goto wav_done;
            
            uint32_t actualRead = wavFile.read(readBuf, toRead);
            if (actualRead == 0) goto wav_done;
            
            samplesRead += bufAvail / bytesPerSample;
            bufAvail = actualRead;
            bufPos = 0;
        }
        
        // Oblicz offset w buforze
        uint32_t localIdx = srcSampleIdx - samplesRead;
        uint32_t byteOffset = localIdx * bytesPerSample;
        
        if (byteOffset < bufAvail) {
            uint8_t dacValue;
            
            if (info.bitsPerSample == 8) {
                // 8-bit unsigned
                if (info.numChannels == 1) {
                    dacValue = readBuf[byteOffset];
                } else {
                    // Stereo → mono (średnia)
                    dacValue = ((uint16_t)readBuf[byteOffset] + readBuf[byteOffset + 1]) / 2;
                }
            } else {
                // 16-bit signed → 8-bit unsigned
                int16_t sample16;
                if (info.numChannels == 1) {
                    sample16 = (int16_t)(readBuf[byteOffset] | (readBuf[byteOffset + 1] << 8));
                } else {
                    // Stereo → mono
                    int16_t left  = (int16_t)(readBuf[byteOffset]     | (readBuf[byteOffset + 1] << 8));
                    int16_t right = (int16_t)(readBuf[byteOffset + 2] | (readBuf[byteOffset + 3] << 8));
                    sample16 = (left / 2) + (right / 2);
                }
                // Konwersja signed 16-bit → unsigned 8-bit
                dacValue = (uint8_t)((sample16 >> 8) + 128);
            }
            
            dacWrite(dacValue);
            dacSamplesGenerated++;
        }
        
        samplePos += sampleRatio;
        
        // Aktualizuj statystyki
        playStats.bytesProcessed = (uint32_t)(samplePos * bytesPerSample);
        playStats.percentDone = 100.0f * samplePos / samplesTotal;
        
        if (dacSamplesGenerated % (DAC_SAMPLE_RATE * 5) == 0) {
            Serial.printf("[WAV] Postęp: %.1f%% (%.1f s)\n",
                          playStats.percentDone,
                          (float)dacSamplesGenerated / DAC_SAMPLE_RATE);
        }
        
        // Sprawdź czy STOP
        if (btnPlayPressed) {
            Serial.println("[WAV] Przerwano przez użytkownika");
            btnPlayPressed = false;
            goto wav_done;
        }
    }
    
    wav_done:
    generateSilence(500);
    Serial.printf("[WAV] Zakończono: %lu próbek DAC\n", dacSamplesGenerated);
}

// ============================================================
//  KALIBRACJA — TON TESTOWY
// ============================================================

void runCalibration() {
    Serial.println("\n=== KALIBRACJA ===");
    Serial.println("Generuję ton 1 kHz — ustaw poziom nagrywania na magnetofonie.");
    Serial.println("Naciśnij PLAY aby zatrzymać.\n");
    
    startDac();
    
    while (!btnPlayPressed) {
        generateSine(1000, 1000);  // 1 kHz przez 1 sekundę, w pętli
    }
    btnPlayPressed = false;
    
    dacFlush();
    stopDac();
    
    Serial.println("Kalibracja zakończona.\n");
}

// ============================================================
//  PRZEGLĄDANIE PLIKÓW
// ============================================================

void scanDirectory(const char* dirPath) {
    fileCount = 0;
    fileIndex = 0;
    
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        Serial.printf("[DIR] Nie mogę otworzyć: %s\n", dirPath);
        return;
    }
    
    while (fileCount < MAX_FILES) {
        File entry = dir.openNextFile();
        if (!entry) break;
        
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            
            // Filtruj po rozszerzeniu w zależności od trybu
            bool accept = false;
            switch (currentMode) {
                case FORGE_C64:
                    accept = strstr(name, ".tap") || strstr(name, ".TAP") ||
                             strstr(name, ".prg") || strstr(name, ".PRG");
                    break;
                case FORGE_SPECTRUM:
                    accept = strstr(name, ".tzx") || strstr(name, ".TZX") ||
                             strstr(name, ".tap") || strstr(name, ".TAP");
                    break;
                case FORGE_KCS:
                    accept = strstr(name, ".bin") || strstr(name, ".BIN") ||
                             strstr(name, ".dat") || strstr(name, ".DAT");
                    break;
                case FORGE_AUDIO:
                    accept = strstr(name, ".wav") || strstr(name, ".WAV");
                    break;
                default:
                    break;
            }
            
            if (accept) {
                strncpy(fileList[fileCount], entry.name(), MAX_FILENAME_LEN - 1);
                fileList[fileCount][MAX_FILENAME_LEN - 1] = 0;
                fileCount++;
            }
        }
        entry.close();
    }
    dir.close();
    
    Serial.printf("[DIR] %s: znaleziono %d plików\n", dirPath, fileCount);
    for (int i = 0; i < fileCount; i++) {
        Serial.printf("  [%d] %s\n", i, fileList[i]);
    }
}

// ============================================================
//  WYŚWIETLACZ OLED
// ============================================================

void updateDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Nagłówek
    display.setCursor(0, 0);
    display.print("TapeForge | ");
    display.println(modeNames[currentMode]);
    display.drawLine(0, 10, OLED_WIDTH, 10, SSD1306_WHITE);
    
    switch (currentState) {
        case FSTATE_IDLE:
            display.setCursor(0, 14);
            display.println("Stan: GOTOWY");
            display.println("");
            display.println("[PLAY] Start/kalibruj");
            display.println("[MODE] Zmien tryb");
            display.println("[SEL]  Przegladaj SD");
            break;
            
        case FSTATE_BROWSING:
            display.setCursor(0, 14);
            display.printf("Pliki (%d):\n", fileCount);
            if (fileCount == 0) {
                display.println("  (brak plikow)");
            } else {
                for (int i = max(0, fileIndex - 2); i < min(fileCount, fileIndex + 3); i++) {
                    display.setCursor(0, 24 + (i - max(0, fileIndex - 2)) * 9);
                    display.printf("%s %s\n", (i == fileIndex) ? ">" : " ", fileList[i]);
                }
            }
            display.setCursor(0, 56);
            display.println("[PLAY] Nagraj  [SEL] >>>");
            break;
            
        case FSTATE_PLAYING:
            display.setCursor(0, 14);
            display.println("NAGRYWANIE...");
            display.setCursor(0, 26);
            {
                // Skróć nazwę pliku jeśli za długa
                char shortName[22];
                strncpy(shortName, playStats.filename, 21);
                shortName[21] = 0;
                display.println(shortName);
            }
            display.setCursor(0, 38);
            display.printf("%.1f%%  %lu B\n", playStats.percentDone, playStats.bytesProcessed);
            
            // Pasek postępu
            {
                int barWidth = (int)(playStats.percentDone * (OLED_WIDTH - 4) / 100.0f);
                display.drawRect(2, 50, OLED_WIDTH - 4, 10, SSD1306_WHITE);
                if (barWidth > 0) {
                    display.fillRect(3, 51, barWidth, 8, SSD1306_WHITE);
                }
            }
            break;
            
        case FSTATE_DONE:
            display.setCursor(0, 14);
            display.println("ZAKONCZONE!");
            display.printf("\n%lu bajtow\n", playStats.bytesProcessed);
            display.printf("Czas: %.1f s\n", playStats.elapsedMs / 1000.0f);
            display.println("\n[PLAY] Powrot");
            break;
            
        case FSTATE_ERROR:
            display.setCursor(0, 14);
            display.println("!!! BLAD !!!");
            display.println("Sprawdz:");
            display.println("- Karte SD");
            display.println("- Pliki");
            display.println("[PLAY] Powrot");
            break;
    }
    
    display.display();
}

// ============================================================
//  NAGRYWANIE — GŁÓWNA FUNKCJA
// ============================================================

void startRecording() {
    if (fileCount == 0 || fileIndex >= fileCount) {
        Serial.println("[REC] Brak pliku do nagrania!");
        currentState = FSTATE_ERROR;
        return;
    }
    
    // Otwórz plik
    char filepath[80];
    snprintf(filepath, sizeof(filepath), "%s/%s", modeDirs[currentMode], fileList[fileIndex]);
    
    File file = SD.open(filepath);
    if (!file) {
        Serial.printf("[REC] Nie mogę otworzyć: %s\n", filepath);
        currentState = FSTATE_ERROR;
        return;
    }
    
    Serial.printf("\n========== NAGRYWANIE ==========\n");
    Serial.printf("Tryb:  %s\n", modeNames[currentMode]);
    Serial.printf("Plik:  %s\n", filepath);
    Serial.printf("Rozmiar: %lu bajtów\n\n", file.size());
    
    // Reset statystyk
    memset(&playStats, 0, sizeof(playStats));
    strncpy(playStats.filename, fileList[fileIndex], 63);
    playStats.elapsedMs = millis();
    
    currentState = FSTATE_PLAYING;
    updateDisplay();
    
    // Uruchom DAC
    startDac();
    
    // Wstępna cisza (czas na naciśnięcie REC na magnetofonie)
    Serial.println("[REC] Cisza 3s — naciśnij REC+PLAY na magnetofonie!");
    generateSilence(3000);
    
    switch (currentMode) {
        case FORGE_C64: {
            if (strstr(filepath, ".tap") || strstr(filepath, ".TAP")) {
                c64EncodeFromTAP(file);
            } else {
                // Wczytaj cały plik do RAM i koduj jako surowe dane
                uint32_t fileSize = file.size();
                uint8_t* data = (uint8_t*)malloc(fileSize);
                if (data) {
                    file.read(data, fileSize);
                    c64EncodeFromBinary(data, fileSize, fileList[fileIndex]);
                    free(data);
                } else {
                    Serial.println("[C64] BŁĄD: Za mało RAM!");
                }
            }
            break;
        }
        
        case FORGE_SPECTRUM: {
            if (strstr(filepath, ".tzx") || strstr(filepath, ".TZX")) {
                zxEncodeFromTZX(file);
            } else {
                uint32_t fileSize = file.size();
                uint8_t* data = (uint8_t*)malloc(fileSize);
                if (data) {
                    file.read(data, fileSize);
                    zxEncodeFromBinary(data, fileSize, fileList[fileIndex]);
                    free(data);
                }
            }
            break;
        }
        
        case FORGE_KCS: {
            uint32_t fileSize = file.size();
            uint8_t* data = (uint8_t*)malloc(fileSize);
            if (data) {
                file.read(data, fileSize);
                kcsEncodeData(data, fileSize);
                free(data);
            } else {
                Serial.println("[KCS] BŁĄD: Za mało RAM! Plik za duży.");
                // Alternatywa: czytaj po kawałku
            }
            break;
        }
        
        case FORGE_AUDIO: {
            playWavFile(file);
            break;
        }
        
        default:
            break;
    }
    
    // Końcowa cisza
    generateSilence(2000);
    
    // Flush i stop DAC
    dacFlush();
    stopDac();
    
    file.close();
    
    playStats.elapsedMs = millis() - playStats.elapsedMs;
    
    Serial.printf("\n========== ZAKOŃCZONO ==========\n");
    Serial.printf("Bajty: %lu\n", playStats.bytesProcessed);
    Serial.printf("Czas:  %.1f s\n", playStats.elapsedMs / 1000.0f);
    Serial.printf("================================\n\n");
    
    currentState = FSTATE_DONE;
}

// ============================================================
//  OBSŁUGA PRZYCISKÓW
// ============================================================

void handleButtons() {
    if (btnPlayPressed) {
        btnPlayPressed = false;
        
        switch (currentState) {
            case FSTATE_IDLE:
                if (currentMode == FORGE_CALIBRATE) {
                    currentState = FSTATE_PLAYING;
                    updateDisplay();
                    runCalibration();
                    currentState = FSTATE_IDLE;
                } else {
                    scanDirectory(modeDirs[currentMode]);
                    currentState = FSTATE_BROWSING;
                }
                break;
                
            case FSTATE_BROWSING:
                startRecording();
                break;
                
            case FSTATE_DONE:
            case FSTATE_ERROR:
                currentState = FSTATE_IDLE;
                break;
                
            case FSTATE_PLAYING:
                // STOP zostanie obsłużony w pętli nagrywania
                break;
        }
    }
    
    if (btnModePressed) {
        btnModePressed = false;
        
        if (currentState == FSTATE_IDLE) {
            currentMode = (ForgeMode)((currentMode + 1) % FORGE_MODE_COUNT);
            Serial.printf("[MODE] → %s\n", modeNames[currentMode]);
        }
    }
    
    if (btnSelectPressed) {
        btnSelectPressed = false;
        
        if (currentState == FSTATE_BROWSING && fileCount > 0) {
            fileIndex = (fileIndex + 1) % fileCount;
            Serial.printf("[SEL] → %s\n", fileList[fileIndex]);
        }
    }
}

// ============================================================
//  SETUP & LOOP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║  🔥 TapeForge v1.0                    ║");
    Serial.println("║  ESP32 Cassette Tape Writer            ║");
    Serial.println("║                                        ║");
    Serial.println("║  Nagrywa dane i audio na kasety!       ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();
    
    // Generuj tablicę sinusową
    generateSineTable();
    
    // Inicjalizacja
    oledReady = setupOLED();
    sdReady   = setupSD();
    setupDAC();
    setupDacTimer();
    setupButtons();
    
    Serial.println();
    Serial.println("Status:");
    Serial.printf("  OLED:  %s\n", oledReady ? "OK" : "BRAK");
    Serial.printf("  SD:    %s\n", sdReady   ? "OK" : "BRAK");
    Serial.printf("  DAC:   OK (GPIO25, 8-bit, %d Hz)\n", DAC_SAMPLE_RATE);
    Serial.println();
    Serial.println("Tryby:");
    for (int i = 0; i < FORGE_MODE_COUNT; i++) {
        Serial.printf("  [%d] %s\n", i, modeNames[i]);
    }
    Serial.println();
    Serial.println("Sterowanie:");
    Serial.println("  [PLAY]   Start/Stop nagrywania");
    Serial.println("  [MODE]   Zmiana trybu");
    Serial.println("  [SELECT] Następny plik");
    Serial.println();
    Serial.println("Instrukcja szybkiego startu:");
    Serial.println("  1. Umieść pliki na karcie SD w odpowiednich folderach");
    Serial.println("  2. Podłącz jack 3.5mm do LINE IN magnetofonu");
    Serial.println("  3. MODE → wybierz tryb → PLAY → wybierz plik → PLAY");
    Serial.println("  4. Naciśnij REC+PLAY na magnetofonie w ciągu 3 sekund");
    Serial.println("  5. Gotowe! Kaseta nagrana.");
    Serial.println();
    
    currentState = FSTATE_IDLE;
    if (oledReady) updateDisplay();
}

void loop() {
    handleButtons();
    
    static uint32_t lastDisplayUpdate = 0;
    if (oledReady && millis() - lastDisplayUpdate > 150) {
        updateDisplay();
        lastDisplayUpdate = millis();
    }
    
    if (dacUnderrun && currentState == FSTATE_PLAYING) {
        dacUnderrun = false;
        // Underrun w trybie nagrywania — SD za wolna
        Serial.println("[WARN] DAC underrun — karta SD za wolna?");
    }
    
    yield();
}
```

---

## 8. Instrukcja obsługi krok po kroku

### 8.1. Nagrywanie programu C64 na kasetę

**Krok 1.** Pobierz plik .TAP z gry C64 (np. z archive.org lub csdb.dk). Umieść go na karcie SD w folderze `/c64/`.

**Krok 2.** Podłącz kabel audio jack 3.5mm z wyjścia TapeForge do wejścia LINE IN magnetofonu.

**Krok 3.** Włóż czystą kasetę do magnetofonu. Przewiń na początek.

**Krok 4.** Na TapeForge: przyciskiem MODE wybierz "C64 (.TAP)". Naciśnij PLAY. Przyciskiem SELECT wybierz plik. Naciśnij PLAY ponownie.

**Krok 5.** Masz 3 sekundy ciszy — naciśnij REC + PLAY na magnetofonie!

**Krok 6.** Czekaj. OLED pokazuje postęp. Serial wypisuje szczegóły. Typowy program C64 (~30 KB) nagrywa się ~15–20 minut w standardowej prędkości.

**Krok 7.** Po zakończeniu TapeForge pokaże "ZAKOŃCZONE". Zatrzymaj nagrywanie na magnetofonie (STOP).

**Krok 8.** Wyjmij kasetę, włóż do Datasette C64, wpisz `LOAD` i naciśnij PLAY na Datasette. Program się załaduje!

### 8.2. Nagrywanie muzyki z SD na kasetę

**Krok 1.** Skonwertuj muzykę do WAV (mono, 44100 Hz, 16-bit): `ffmpeg -i song.mp3 -ac 1 -ar 44100 song.wav`. Umieść na SD w `/audio/`.

**Krok 2.** Podłącz kabel do LINE IN, włóż kasetę.

**Krok 3.** MODE → "Audio (.WAV)" → PLAY → SELECT (wybierz plik) → PLAY.

**Krok 4.** REC + PLAY na magnetofonie w ciągu 3 sekund.

**Krok 5.** TapeForge odtwarza WAV w czasie rzeczywistym przez DAC. Kaseta nagrywa dźwięk.

**Krok 6.** Wynik: kaseta z muzyką do odtworzenia w dowolnym walkmanie lub boomboxie.

### 8.3. Kalibracja poziomu nagrywania

Przed pierwszym nagraniem warto skalibrować poziom. MODE → "Kalibracja" → PLAY. TapeForge generuje ciągły ton 1 kHz. Na magnetofonie ustaw pokrętło poziomu nagrywania (REC LEVEL) tak, aby wskaźnik VU nie wchodził w czerwone pole (przesterowanie). Naciśnij PLAY na TapeForge aby zatrzymać.

Jeśli magnetofon nie ma regulacji poziomu (automatyczny ALC), wyreguluj potencjometrem toru wyjściowego (dodaj potencjometr 10 kΩ między filtrem RC a jackiem).

---

## 9. Troubleshooting

| Problem | Przyczyna | Rozwiązanie |
|---------|-----------|-------------|
| **C64 nie ładuje danych** | Zbyt wysoki/niski poziom sygnału | Użyj kalibracji, wyreguluj poziom |
| | Zła prędkość taśmy w magnetofonie | Sprawdź wow & flutter, wymień pasek |
| | Uszkodzony plik .TAP | Zweryfikuj plik w emulatorze (VICE) |
| | Magnetofon nagrywa z equalizacją | Spróbuj wejścia LINE IN zamiast MIC |
| **Przesterowany dźwięk** | Za mocny sygnał z DAC | Zwiększ R10 w dzielniku (np. na 22 kΩ) |
| **Cichy dźwięk** | Dzielnik tłumi za mocno | Zmniejsz R10 (np. na 4.7 kΩ) lub pomiń dzielnik |
| **Szum / buczenie** | Pętla masy (ground loop) | Zasilaj ESP32 z baterii zamiast USB |
| | Brak filtrowania DAC | Sprawdź filtr RC (R8 + C7) |
| **DAC underrun** | Karta SD za wolna | Użyj karty Class 10. Zmniejsz sample rate |
| **Plik WAV nie gra** | Format nie-PCM (MP3 w WAV) | Skonwertuj: `ffmpeg -i plik.wav -acodec pcm_s16le out.wav` |
| | Stereo 24-bit | Skonwertuj do mono 16-bit lub 8-bit |
| **OLED nic nie pokazuje** | Zły adres I2C | Zmień `0x3C` na `0x3D` w kodzie |
| **SD nie wykryta** | Złe połączenie SPI | Sprawdź CS=5, SCK=18, MOSI=23, MISO=19 |

---

## 10. Porady i dobre praktyki

### 10.1. Jakość nagrywania

Używaj kaset Type I (Normal) do danych — chrome i metal mają inną charakterystykę i mogą pogorszyć kompatybilność z Datasette C64. Kasety Type II (Chrome) są lepsze dla muzyki. Nowe kasety dają lepsze rezultaty niż wielokrotnie nagrywane. Przed nagraniem danych warto wymazać kasetę na całej długości (nagraj ciszę lub użyj kasownika).

### 10.2. Prędkość vs niezawodność

Standard C64 (300 baud) jest wolny, ale niezawodny. Turbo loadery (2400+ baud) są szybsze, ale bardziej wrażliwe na jakość taśmy i magnetofonu. Dla pierwszych eksperymentów trzymaj się standardowej prędkości.

### 10.3. Weryfikacja nagrania

Najlepszy sposób weryfikacji: odtwórz kasetę z powrotem przez DigitalCassettePlayer (projekt #2) i porównaj zdekodowane dane z oryginałem. Lub po prostu załaduj na prawdziwym C64/Spectrum.

### 10.4. Nagrywanie mixtape'ów

W trybie AUDIO możesz nagrać wiele plików WAV po kolei — na OLED wybierz kolejny plik i naciśnij PLAY bez zatrzymywania magnetofonu. Między utworami TapeForge generuje 2 sekundy ciszy.

---

## 11. Co dalej — rozbudowa

### 11.1. Interfejs webowy WiFi

ESP32 ma WiFi — stwórz serwer WWW z interfejsem do uploadu plików na SD, wyboru trybu i monitorowania postępu nagrywania z telefonu. Upload pliku .TAP z komputera → ESP32 nagrywa na kasetę — bez dotykania karty SD.

### 11.2. Tryb mieszany: dane + audio na jednej kasecie

Nagraj na stronie A program C64, a na stronie B muzykę z tego programu. Lub na początku strony A dane programu, a po przerwie muzykę z gry — dokładnie jak robiono to w latach 80.

### 11.3. Generator turbo loaderów

Zaimplementuj popularne formaty turbo (Novaload, Cyberload, Pavloda) — nagrywanie 5–10× szybsze niż standard.

### 11.4. Bidirectional: nagrywanie i odtwarzanie

Połącz TapeForge z DigitalCassettePlayer w jedno urządzenie — dodaj tor ADC (z projektu #2) na drugim pinie, i masz kompletny "magnetofon cyfrowy" na ESP32, który zarówno czyta jak i pisze kasety.

### 11.5. Konwerter formatów

Dodaj konwersję między formatami: .TAP ↔ .TZX, .PRG → .TAP, .WAV → dane KCS. ESP32 jako uniwersalny "tape hub".

### 11.6. Własny PCB

Zaprojektuj PCB łączącą wszystkie trzy projekty: odtwarzacz analogowy (LM386), digitalizacja (ADC), nagrywanie (DAC), ESP32, OLED, SD, jack wejściowy i wyjściowy. Kompletny retro-tape-station w jednej obudowie.

---

## 12. Słowniczek

| Pojęcie | Wyjaśnienie |
|---------|-------------|
| **ALC** | Automatic Level Control — automatyczna regulacja poziomu nagrywania w magnetofonie. |
| **Baud** | Liczba symboli (bitów) transmitowanych na sekundę. 300 baud = ~37.5 bajtów/s. |
| **DAC** | Digital-to-Analog Converter — przetwornik cyfrowo-analogowy. ESP32 ma 8-bitowy na GPIO25/26. |
| **Datasette** | Commodore 1530 — magnetofon kasetowy dedykowany dla C64/VIC-20. |
| **DMA** | Direct Memory Access — transfer danych bez udziału CPU (ESP32 obsługuje I2S DMA). |
| **FSK** | Frequency Shift Keying — kodowanie bitów częstotliwościami. |
| **Ground Loop** | Pętla masowa — różnica potencjałów między masami urządzeń, powoduje buczenie 50 Hz. |
| **ISR** | Interrupt Service Routine — funkcja wywoływana przez przerwanie sprzętowe. |
| **LINE IN** | Wejście liniowe magnetofonu (~0.5–1V RMS). Lepsze niż MIC dla sygnału z DAC. |
| **PCM** | Pulse Code Modulation — najprostsze kodowanie audio: surowe próbki amplitudy. |
| **Resampling** | Zmiana częstotliwości próbkowania (np. 44100→22050 Hz). |
| **Ring Buffer** | Bufor cykliczny — ISR konsumuje, main loop napełnia, wskaźniki zawijają się. |
| **RMS** | Root Mean Square — efektywna wartość napięcia sygnału zmiennego. |
| **Turbo Loader** | Niestandardowa procedura ładowania danych z kasety, szybsza niż systemowa. |
| **VU-metr** | Volume Unit meter — wskaźnik poziomu sygnału audio. |

---

## 13. Powiązanie z ekosystemem projektów

```
┌─────────────────────────────────────────────────────────────┐
│                    EKOSYSTEM KASETOWY                        │
│                                                              │
│  ┌────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │ #1 DIY         │  │ #2 Digital      │  │ #3 TapeForge │ │
│  │ Odtwarzacz     │  │ CassettePlayer  │  │              │ │
│  │                │  │                 │  │              │ │
│  │ Kaseta→LM386   │  │ Kaseta→ESP32    │  │ ESP32→Kaseta │ │
│  │ →Głośnik       │  │ →SD (dekoduj)   │  │ SD→DAC→REC  │ │
│  │                │  │                 │  │              │ │
│  │ Analogowy      │  │ Digitalizacja   │  │ Nagrywanie   │ │
│  │ odtwarzacz     │  │ i dekodowanie   │  │ danych+audio │ │
│  └────────┬───────┘  └───────┬─────────┘  └──────┬───────┘ │
│           │                  │                    │         │
│           └──────── WSPÓLNE: ─────────────────────┘         │
│             • Mechanizm kasetowy                             │
│             • Głowica magnetyczna                            │
│             • Teoria zapisu magnetycznego                    │
│             • Breadboard + elementy pasywne                  │
└─────────────────────────────────────────────────────────────┘
```

---

## 14. Historia zmian

| Wersja | Data | Opis |
|--------|------|------|
| 1.0 | 2026-02-06 | Pierwsza wersja. Enkodery C64/Spectrum/KCS, odtwarzacz WAV, kalibracja. |

---

## 15. Licencja

Projekt open-source do dowolnego użytku edukacyjnego i hobbystycznego. Stworzony z pomocą Claude (Anthropic). Inspirowany projektami Tapuino, TZXDuino oraz duchem lat 80., kiedy kaseta magnetofonowa była najważniejszym nośnikiem danych dla milionów użytkowników domowych komputerów.

> *„PRESS PLAY ON TAPE — ale tym razem to Ty decydujesz, co jest na tej taśmie."*
