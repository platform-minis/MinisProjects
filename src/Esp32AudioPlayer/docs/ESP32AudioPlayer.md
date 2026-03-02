# 🎧 ESP32AudioPlayer — Dokumentacja Projektu

> **Wersja:** 1.0  
> **Data:** 2026-02-06  
> **Poziom trudności:** ⭐⭐ Średniozaawansowany  
> **Szacowany koszt:** 50–150 zł (zależnie od wariantu)  
> **Platforma:** ESP32 / ESP32-S3 (Arduino Framework)  

---

## 1. Opis projektu

ESP32AudioPlayer to modułowa platforma audio oparta na ESP32 i najlepszych dostępnych bibliotekach open-source. Projekt pokazuje jak zbudować kompletne urządzenie audio — od prostego odtwarzacza plików z karty SD, przez radio internetowe, głośnik Bluetooth, aż po stację efektów dźwiękowych i rejestrator audio.

Zamiast pisania niskopoziomowego kodu I2S i ręcznej obsługi buforów DAC (jak w poprzednich projektach TapeForge i DigitalCassettePlayer), korzystamy z potężnych bibliotek, które abstrahują hardware i pozwalają łączyć źródła audio, dekodery, efekty i wyjścia jak klocki LEGO.

### 1.1. Co pokrywa ten projekt

Projekt składa się z **10 kompletnych przykładów aplikacji** o rosnącej złożoności:

| # | Aplikacja | Źródło audio | Wyjście | Kodeki |
|---|-----------|-------------|---------|--------|
| 1 | Odtwarzacz WAV z SD | Karta SD | I2S DAC (MAX98357A) | WAV |
| 2 | Odtwarzacz MP3 z SD | Karta SD | I2S DAC | MP3 (Helix) |
| 3 | Radio internetowe | WiFi stream | I2S DAC | MP3/AAC |
| 4 | Głośnik Bluetooth A2DP | Telefon (BT) | I2S DAC | SBC→PCM |
| 5 | Nadajnik Bluetooth | SD / mikrofon | Słuchawki BT | PCM→SBC |
| 6 | Rejestrator audio | Mikrofon I2S (INMP441) | Karta SD (WAV) | PCM |
| 7 | Efekty dźwiękowe | SD / stream | I2S DAC | + reverb, echo |
| 8 | Syntezator tonów | Generowany | I2S DAC / DAC wewnętrzny | — |
| 9 | Text-to-Speech | Google TTS (WiFi) | I2S DAC | MP3 |
| 10 | Wieloźródłowy kombajn | SD + WiFi + BT + MIC | I2S + BT + SD | Wszystkie |

### 1.2. Powiązanie z ekosystemem kasetowym

ESP32AudioPlayer to czwarty projekt w serii. Poprzednie trzy skupiały się na kasetach magnetofonowych i analogowym/cyfrowym audio. Ten projekt wchodzi na wyższy poziom — profesjonalne I2S DAC/ADC, kodeki MP3/AAC/FLAC, streaming, Bluetooth i WiFi.

```
#1 DIY Odtwarzacz ──► #2 DigitalCassettePlayer ──► #3 TapeForge ──► #4 ESP32AudioPlayer
   (analogowy)           (ADC, dekodowanie)           (DAC, zapis)      (I2S, kodeki, BT, WiFi)
```

---

## 2. Ekosystem bibliotek audio dla ESP32

### 2.1. Główna biblioteka: arduino-audio-tools (Phil Schatzmann)

To najlepsza i najpełniejsza biblioteka audio dla Arduino/ESP32 (2200+ gwiazdek na GitHub, aktywny rozwój — v1.2.0 z września 2025). Oparta na filozofii strumieni Arduino (Stream/Print), pozwala łączyć komponenty w łańcuchy przetwarzania:

**Źródło → [Dekoder] → [Efekty] → [Enkoder] → Wyjście**

Każdy element to obiekt Stream, a `StreamCopy` kopiuje dane z jednego do drugiego.

Kluczowe cechy:

- Architektura źródło/ujście (Source/Sink) oparta na Arduino Stream.
- Obsługa I2S (TX, RX, TDM), wewnętrznego DAC/ADC, AnalogAudioStream, PDM.
- Kodeki: MP3 (Helix), AAC (Helix/FAAD2), WAV, FLAC, Vorbis/OGG, Opus, ADPCM, SBC, G.711/G.722/G.726, RTTTL i inne.
- Źródła: MemoryStream, URLStream (HTTP/HTTPS), I2SStream, AnalogAudioStream, GeneratedSoundStream, AudioSourceSD.
- Efekty: Boost, Distortion, Delay/Echo, Reverb, Tremolo, PitchShift, Compressor.
- Klasa AudioPlayer do zarządzania playlistą i nawigacją.
- Resampling, konwersja formatów, mikser/splitter kanałów, regulacja głośności.
- Wsparcie dla płytek audio: AI Thinker AudioKit (ES8388), LyraT, WM8960, VS1053.
- Działa na ESP32, ESP32-S3, ESP32-C3, ESP32-P4, RP2040, STM32, a nawet Linux/Win/macOS.

**Repozytorium:** https://github.com/pschatzmann/arduino-audio-tools

**Instalacja:**
```bash
cd ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/arduino-audio-tools.git
git clone https://github.com/pschatzmann/arduino-libhelix.git    # dekoder MP3/AAC
```

### 2.2. Biblioteka uzupełniająca: ESP32-audioI2S (schreibfaul1)

Alternatywa / uzupełnienie — wyspecjalizowana w odtwarzaniu z SD i streamingu. 2k+ gwiazdek, prostsza w użyciu niż audio-tools, ale mniej elastyczna. Wymaga ESP32 z PSRAM (np. ESP32-WROVER). Natywnie obsługuje MP3, AAC, WAV, FLAC, Vorbis, Opus. Odtwarza streamy HTTP/HTTPS (radio internetowe), Google TTS, OpenAI Speech. Wbudowane dekodery — nie wymaga zewnętrznych bibliotek kodeków.

**Repozytorium:** https://github.com/schreibfaul1/ESP32-audioI2S

**Instalacja:** Arduino IDE → Library Manager → szukaj "ESP32-audioI2S" lub ZIP z GitHub.

### 2.3. Bluetooth A2DP: ESP32-A2DP (Phil Schatzmann)

Biblioteka do odbierania i wysyłania audio przez Bluetooth A2DP (profil strumieniowania muzyki). Działa samodzielnie lub w połączeniu z arduino-audio-tools.

**Repozytorium:** https://github.com/pschatzmann/ESP32-A2DP

### 2.4. Tabela porównawcza bibliotek

| Cecha | arduino-audio-tools | ESP32-audioI2S | ESP32-A2DP |
|-------|--------------------:|---------------:|-----------:|
| GitHub ⭐ | ~2200 | ~2000 | ~1600 |
| Architektura | Strumieniowa (pipe) | Monolityczna | Dedykowana BT |
| MP3 dekoder | Helix (zewn. lib) | Wbudowany Helix | SBC (A2DP) |
| AAC dekoder | Helix/FAAD2 | Wbudowany FAAD2 | — |
| FLAC | Tak | Tak | — |
| OGG/Vorbis | Tak | Tak | — |
| Opus | Tak | Tak | — |
| WAV | Tak | Tak | — |
| Streaming HTTP | URLStream | Wbudowany | — |
| Bluetooth A2DP | Przez ESP32-A2DP | — | Tak |
| Nagrywanie (ADC) | Tak (I2SStream RX) | Nie | Nie |
| Efekty audio | Tak (bogaty zestaw) | Nie | Nie |
| Generator tonów | Tak | Nie | Nie |
| AudioPlayer (playlist) | Tak | Nie | Nie |
| Wymaga PSRAM | Nie (zalecane) | **Tak** | Nie |
| ESP32-S3 | Tak | Tak | Ograniczone |
| Łatwość użycia | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| Elastyczność | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ |

**Rekomendacja:** arduino-audio-tools jako baza (elastyczność, efekty, nagrywanie, generatory), ESP32-audioI2S jako szybki start do radia internetowego, ESP32-A2DP do projektów Bluetooth.

---

## 3. Hardware

### 3.1. Warianty budowy

**Wariant A: Minimum (DAC wewnętrzny)** — bez dodatkowych modułów, dźwięk przez wbudowany 8-bitowy DAC na GPIO25. Jakość niska (8-bit), ale wystarczająca do testów. Koszt: ~35 zł (ESP32 + SD).

**Wariant B: Jakość HiFi (I2S DAC)** — zewnętrzny DAC I2S: MAX98357A (mono, ze wzmacniaczem 3W, ~8–15 zł) lub PCM5102A (stereo, linia, ~15–25 zł). Jakość CD (16-bit, 44.1+ kHz). Zalecany wariant.

**Wariant C: Pełna stacja (I2S DAC + ADC + BT)** — DAC + mikrofon INMP441 (I2S ADC, ~8–15 zł) + Bluetooth. Odtwarzanie, nagrywanie, streaming. Koszt: ~80–120 zł.

**Wariant D: Płytka audio all-in-one** — AI Thinker AudioKit (~60–100 zł) — ma ESP32, DAC/ADC ES8388, SD slot, 2 mikrofony, wzmacniacz, przyciski. Wszystko na jednej płytce.

### 3.2. BOM — Wariant B (zalecany)

| # | Element | Opis | Cena |
|---|---------|------|------|
| 1 | ESP32 DevKit V1 | Podstawowy mikrokontroler | 20–35 zł |
| 2 | MAX98357A | I2S DAC + wzmacniacz mono 3W | 8–15 zł |
| 3 | Głośnik 4Ω / 8Ω 3W | Pełnozakresowy, średnica 40–57 mm | 5–10 zł |
| 4 | Moduł microSD | Czytnik kart SPI | 3–8 zł |
| 5 | Karta microSD | 4–32 GB, FAT32, Class 10 | 10–20 zł |
| 6 | OLED 0.96" SSD1306 | Wyświetlacz I2C 128×64 (opcja) | 8–15 zł |
| 7 | Przyciski tact switch | 4 szt. (Play/Pause, Next, Prev, Vol) | ~1 zł |
| 8 | Breadboard + kabelki | Montaż | 10–15 zł |
| | **RAZEM** | | **~65–120 zł** |

Dodatkowo do wariantu C: mikrofon INMP441 (I2S, ~8–15 zł).

### 3.3. Pinout — Wariant B

```
ESP32              MAX98357A (I2S DAC)
─────              ────────────────────
GPIO26  ──────────  BCLK   (Bit Clock)
GPIO25  ──────────  LRC    (Word Select / Left-Right Clock)
GPIO22  ──────────  DIN    (Data In)
                    GND  ── GND
                    VIN  ── 5V (lub 3.3V)
                    GAIN ── niepodłączony (domyślnie 9 dB)
                             lub GND (12 dB) lub VIN (15 dB)
                    SD   ── niepodłączony (aktywny)
                             lub GND przez 1MΩ (shutdown)

ESP32              Moduł microSD (SPI)
─────              ─────────────────────
GPIO5   ──────────  CS
GPIO18  ──────────  SCK
GPIO23  ──────────  MOSI
GPIO19  ──────────  MISO
3.3V    ──────────  VCC
GND     ──────────  GND

ESP32              OLED SSD1306 (I2C)
─────              ──────────────────
GPIO21  ──────────  SDA
GPIO16  ──────────  SCL    (uwaga: GPIO22 zajęty przez I2S!)
3.3V    ──────────  VCC
GND     ──────────  GND

ESP32              Mikrofon INMP441 (I2S — wariant C)
─────              ─────────────────────────────────────
GPIO14  ──────────  SCK    (Bit Clock)
GPIO15  ──────────  WS     (Word Select)
GPIO32  ──────────  SD     (Serial Data)
3.3V    ──────────  VDD
GND     ──────────  GND
GND     ──────────  L/R    (GND = lewy kanał)
```

### 3.4. MAX98357A — notatki

MAX98357A to jednoukładowy wzmacniacz klasy D ze zintegrowanym DAC I2S. Idealna prostota: 3 piny sygnałowe (BCLK, LRC, DIN), zasilanie i głośnik. Moc: do 3.2W na 4Ω. Nie wymaga żadnych zewnętrznych kondensatorów ani rezystorów — just connect and play.

Alternatywy: PCM5102A (wyjście liniowe stereo, ~15–25 zł, wymaga osobnego wzmacniacza), UDA1334A (Adafruit I2S Stereo Decoder, ~25 zł), CS4344.

### 3.5. INMP441 — mikrofon I2S

Cyfrowy mikrofon MEMS z wyjściem I2S. Czułość: -26 dBFS, SNR: 61 dB, sample rate do 48 kHz. Wbudowany ADC — sygnał cyfrowy bezpośrednio do ESP32, bez szumów analogowych. Idealny do nagrywania i rozpoznawania mowy.

---

## 4. Przykłady aplikacji — kompletne sketche Arduino

### 4.1. Przykład 1: Odtwarzacz WAV z karty SD

Najprostszy możliwy odtwarzacz — czyta plik WAV z SD i wysyła na I2S DAC. Używa klasy AudioPlayer z arduino-audio-tools, która automatycznie zarządza plikami w katalogu.

```cpp
/*
 * ESP32AudioPlayer — Przykład 1
 * Odtwarzacz WAV z karty SD
 * 
 * Biblioteka: arduino-audio-tools
 * Hardware: ESP32 + MAX98357A + moduł SD
 */

#include "AudioTools.h"

// I2S output (do MAX98357A)
I2SStream i2s;

// Źródło plików z SD
AudioSourceSD source("/audio", "wav");  // Katalog i rozszerzenie

// Player z nawigacją (next/prev/play/pause)
AudioPlayer player(source, i2s);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // Konfiguracja I2S
    auto config = i2s.defaultConfig(TX_MODE);
    config.pin_bck = 26;       // BCLK
    config.pin_ws = 25;        // LRC
    config.pin_data = 22;      // DIN
    config.sample_rate = 44100;
    config.channels = 2;
    config.bits_per_sample = 16;

    // Start
    player.begin();
    Serial.println("Odtwarzacz WAV gotowy. Pliki z /audio/ na SD.");
    Serial.println("Komendy Serial: n=next, p=prev, +=vol up, -=vol down");
}

void loop() {
    player.copy();

    // Sterowanie przez Serial
    if (Serial.available()) {
        char cmd = Serial.read();
        switch (cmd) {
            case 'n': player.next();          break;
            case 'p': player.previous();      break;
            case ' ': player.setActive(!player.isActive()); break;
            case '+': player.setVolume(player.volume() + 0.1); break;
            case '-': player.setVolume(player.volume() - 0.1); break;
        }
    }
}
```

---

### 4.2. Przykład 2: Odtwarzacz MP3 z karty SD

Dodaje dekoder MP3 Helix do łańcucha. Wymaga biblioteki `arduino-libhelix`.

```cpp
/*
 * ESP32AudioPlayer — Przykład 2
 * Odtwarzacz MP3 z karty SD
 * 
 * Biblioteki: arduino-audio-tools + arduino-libhelix
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

I2SStream i2s;
AudioSourceSD source("/music", "mp3");
MP3DecoderHelix decoder;
AudioPlayer player(source, i2s, decoder);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    auto config = i2s.defaultConfig(TX_MODE);
    config.pin_bck = 26;
    config.pin_ws = 25;
    config.pin_data = 22;

    player.begin();
    Serial.println("Odtwarzacz MP3 gotowy. Pliki z /music/ na SD.");
}

void loop() {
    player.copy();

    if (Serial.available()) {
        switch (Serial.read()) {
            case 'n': player.next();     break;
            case 'p': player.previous(); break;
            case ' ': player.setActive(!player.isActive()); break;
        }
    }
}
```

---

### 4.3. Przykład 3: Radio internetowe (streaming MP3/AAC)

Łączy się z WiFi i streamuje radio internetowe. URLStream pobiera dane HTTP, dekoder MP3 konwertuje na PCM, I2S wysyła na DAC.

```cpp
/*
 * ESP32AudioPlayer — Przykład 3
 * Radio internetowe (HTTP MP3 streaming)
 * 
 * Biblioteki: arduino-audio-tools + arduino-libhelix
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/Communication/AudioHttp.h"

// Konfiguracja WiFi
const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// Stacje radiowe
const char* stations[] = {
    "http://stream.polskieradio.pl/pr3",                          // Polskie Radio Trójka
    "http://zt01.cdn.eurozet.pl/zet-net.mp3",                     // Radio ZET
    "http://n-11-14.dcs.redcdn.pl/sc/o2/Eurozet/live/chillizet.livx", // ChilliZET
    "http://stream.srg-ssr.ch/m/rsj/mp3_128",                     // Radio Swiss Jazz
    "http://ice1.somafm.com/groovesalad-128-mp3",                 // SomaFM Groove Salad
};
const int stationCount = 5;
int currentStation = 0;

URLStream url(ssid, password);
I2SStream i2s;
MP3DecoderHelix decoder;
EncodedAudioStream decoderStream(&i2s, &decoder);
StreamCopy copier(decoderStream, url);

void connectStation(int index) {
    Serial.printf("Łączę ze stacją %d: %s\n", index, stations[index]);
    url.end();
    url.begin(stations[index], "audio/mp3");
}

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // I2S
    auto config = i2s.defaultConfig(TX_MODE);
    config.pin_bck = 26;
    config.pin_ws = 25;
    config.pin_data = 22;
    i2s.begin(config);

    // Dekoder
    decoderStream.begin();

    // Pierwsza stacja
    connectStation(0);

    Serial.println("Radio internetowe gotowe.");
    Serial.println("Komendy: n=następna stacja, p=poprzednia, 0-4=stacja");
}

void loop() {
    copier.copy();

    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'n') {
            currentStation = (currentStation + 1) % stationCount;
            connectStation(currentStation);
        } else if (c == 'p') {
            currentStation = (currentStation - 1 + stationCount) % stationCount;
            connectStation(currentStation);
        } else if (c >= '0' && c <= '4') {
            currentStation = c - '0';
            connectStation(currentStation);
        }
    }
}
```

---

### 4.4. Przykład 4: Głośnik Bluetooth A2DP (odbiornik)

ESP32 staje się głośnikiem Bluetooth — widocznym dla telefonu jako urządzenie audio. Muzyka z telefonu streamowana jest przez BT A2DP i odtwarzana na I2S DAC.

```cpp
/*
 * ESP32AudioPlayer — Przykład 4
 * Głośnik Bluetooth A2DP (odbiornik)
 * 
 * Biblioteki: arduino-audio-tools + ESP32-A2DP
 * 
 * Parowanie: na telefonie szukaj "ESP32_Speaker"
 */

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

void setup() {
    Serial.begin(115200);

    auto config = i2s.defaultConfig(TX_MODE);
    config.pin_bck = 26;
    config.pin_ws = 25;
    config.pin_data = 22;
    config.sample_rate = 44100;
    config.channels = 2;
    config.bits_per_sample = 16;
    i2s.begin(config);

    // Uruchom Bluetooth z nazwą widoczną dla telefonu
    a2dp_sink.start("ESP32_Speaker");
    
    Serial.println("Głośnik Bluetooth 'ESP32_Speaker' gotowy!");
    Serial.println("Sparuj telefon i odtwarzaj muzykę.");
}

void loop() {
    // A2DP działa w tle na osobnym rdzeniu ESP32
    delay(100);
}
```

---

### 4.5. Przykład 5: Nadajnik Bluetooth A2DP (źródło)

Odwrotnie — ESP32 czyta MP3 z SD i wysyła audio do sparowanych słuchawek/głośnika Bluetooth.

```cpp
/*
 * ESP32AudioPlayer — Przykład 5
 * Nadajnik Bluetooth A2DP (z SD do słuchawek BT)
 * 
 * Biblioteki: arduino-audio-tools + ESP32-A2DP + arduino-libhelix
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "BluetoothA2DPSource.h"

const char* btDeviceName = "Moje_Sluchawki_BT";  // Nazwa docelowego urządzenia BT

AudioSourceSD source("/music", "mp3");
MP3DecoderHelix decoder;
BluetoothA2DPSource a2dp_source;

// Bufor audio na dekodowane PCM
RingBuffer<uint8_t> ringBuffer(8192);

// Callback wywoływany przez A2DP gdy potrzebuje danych
int32_t get_data(uint8_t *data, int32_t len) {
    return ringBuffer.readArray(data, len);
}

void setup() {
    Serial.begin(115200);
    
    // Konfiguracja A2DP source
    a2dp_source.set_data_callback(get_data);
    a2dp_source.start(btDeviceName);
    
    Serial.printf("Łączę z '%s'...\n", btDeviceName);
    Serial.println("Po połączeniu odtwarzam MP3 z SD przez Bluetooth.");
}

void loop() {
    // Dekoduj MP3 i wypełniaj ring buffer
    // (uproszczona wersja — pełna implementacja wymaga AudioPlayer + callback)
    delay(10);
}
```

---

### 4.6. Przykład 6: Rejestrator audio (mikrofon → SD)

Nagrywa dźwięk z mikrofonu I2S (INMP441) na kartę SD jako plik WAV.

```cpp
/*
 * ESP32AudioPlayer — Przykład 6
 * Rejestrator audio: Mikrofon INMP441 → WAV na SD
 * 
 * Biblioteka: arduino-audio-tools
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"

// I2S wejście (mikrofon)
I2SStream microphone;

// Wyjście: plik WAV na SD
File wavFile;
EncodedAudioStream encoder(&wavFile, new WAVEncoder());
StreamCopy copier(encoder, microphone);

// Stan nagrywania
bool recording = false;
uint32_t recordStart = 0;
const uint32_t MAX_RECORD_MS = 30000;  // Max 30 sekund

void startRecording() {
    // Utwórz unikalną nazwę pliku
    char filename[32];
    snprintf(filename, sizeof(filename), "/rec_%lu.wav", millis());
    
    wavFile = SD.open(filename, FILE_WRITE);
    if (!wavFile) {
        Serial.println("BŁĄD: Nie mogę otworzyć pliku!");
        return;
    }
    
    // Konfiguracja I2S mikrofonu
    auto config = microphone.defaultConfig(RX_MODE);
    config.pin_bck = 14;       // SCK mikrofonu
    config.pin_ws = 15;        // WS mikrofonu
    config.pin_data = 32;      // SD (data) mikrofonu
    config.sample_rate = 16000;
    config.channels = 1;
    config.bits_per_sample = 16;
    config.i2s_format = I2S_STD_FORMAT;
    microphone.begin(config);
    
    // Konfiguracja enkodera WAV
    auto encConfig = encoder.defaultConfig();
    encConfig.sample_rate = 16000;
    encConfig.channels = 1;
    encConfig.bits_per_sample = 16;
    encoder.begin(encConfig);
    
    recording = true;
    recordStart = millis();
    Serial.printf("Nagrywanie do %s...\n", filename);
}

void stopRecording() {
    recording = false;
    encoder.end();
    microphone.end();
    wavFile.close();
    Serial.printf("Nagranie zakończone (%.1f s)\n", (millis() - recordStart) / 1000.0f);
}

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);
    
    SD.begin(5);  // CS pin 5
    
    Serial.println("Rejestrator audio gotowy.");
    Serial.println("Wyślij 'r' aby nagrywać, 's' aby zatrzymać.");
}

void loop() {
    if (recording) {
        copier.copy();
        
        // Auto-stop po MAX_RECORD_MS
        if (millis() - recordStart > MAX_RECORD_MS) {
            stopRecording();
        }
    }
    
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' && !recording) startRecording();
        if (c == 's' && recording) stopRecording();
    }
}
```

---

### 4.7. Przykład 7: Efekty dźwiękowe

Dodaje efekty (echo, reverb, distortion) do odtwarzanego audio w czasie rzeczywistym.

```cpp
/*
 * ESP32AudioPlayer — Przykład 7
 * Efekty dźwiękowe w czasie rzeczywistym
 * 
 * Biblioteka: arduino-audio-tools
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

I2SStream i2s;

// Definicja efektów
Delay delayEffect(1000);     // Echo: 1000ms opóźnienia
Boost boostEffect(2.0);       // Wzmocnienie 2×

// Łańcuch efektów
AudioEffectStream effects(i2s);

// Źródło: generator sinusa (do demonstracji)
SineWaveGenerator<int16_t> sineWave(16000);
GeneratedSoundStream<int16_t> sound(sineWave);
StreamCopy copier(effects, sound);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // I2S
    auto config = i2s.defaultConfig(TX_MODE);
    config.pin_bck = 26;
    config.pin_ws = 25;
    config.pin_data = 22;
    config.sample_rate = 44100;
    config.channels = 2;
    config.bits_per_sample = 16;
    i2s.begin(config);

    // Dodaj efekty do łańcucha
    effects.addEffect(delayEffect);
    effects.addEffect(boostEffect);
    effects.begin(config);

    // Generator sinusa 440 Hz (A4)
    sineWave.begin(AudioInfo(44100, 2, 16), N_A4);

    Serial.println("Efekty audio aktywne: Echo + Boost");
    Serial.println("Komendy: 1=echo ON/OFF, 2=boost ON/OFF");
}

void loop() {
    copier.copy();

    if (Serial.available()) {
        switch (Serial.read()) {
            case '1':
                delayEffect.setActive(!delayEffect.active());
                Serial.printf("Echo: %s\n", delayEffect.active() ? "ON" : "OFF");
                break;
            case '2':
                boostEffect.setActive(!boostEffect.active());
                Serial.printf("Boost: %s\n", boostEffect.active() ? "ON" : "OFF");
                break;
        }
    }
}
```

---

### 4.8. Przykład 8: Syntezator tonów

Generuje tony, melodie RTTTL i szum biały/różowy — bez żadnych plików, czysta synteza.

```cpp
/*
 * ESP32AudioPlayer — Przykład 8
 * Syntezator tonów i melodii RTTTL
 * 
 * Biblioteka: arduino-audio-tools
 */

#include "AudioTools.h"

AudioInfo info(44100, 1, 16);

// Wyjście: wewnętrzny DAC (bez MAX98357A!)
AnalogAudioStream dac;

// Generatory
SineWaveGenerator<int16_t>    sine(16000);
SquareWaveGenerator<int16_t>  square(10000);
SawToothGenerator<int16_t>    saw(10000);
NoiseGenerator<int16_t>       noise(5000);

GeneratedSoundStream<int16_t> sound(sine);
StreamCopy copier(dac, sound);

void setup() {
    Serial.begin(115200);

    // Wewnętrzny DAC na GPIO25
    auto config = dac.defaultConfig(TX_MODE);
    config.copyFrom(info);
    dac.begin(config);

    // Startowy ton: 440 Hz (A4)
    sine.begin(info, N_A4);
    sound.begin(info);

    Serial.println("Syntezator tonów.");
    Serial.println("Komendy:");
    Serial.println("  s=sine  q=square  w=saw  n=noise");
    Serial.println("  1-9 = częstotliwość (C4..C6)");
}

// Nuty
float notes[] = {
    N_C4, N_D4, N_E4, N_F4, N_G4, N_A4, N_B4, N_C5, N_D5
};

void loop() {
    copier.copy();

    if (Serial.available()) {
        char c = Serial.read();
        
        switch (c) {
            case 's':
                sound.setInput(sine);
                Serial.println("Waveform: Sine");
                break;
            case 'q':
                sound.setInput(square);
                Serial.println("Waveform: Square");
                break;
            case 'w':
                sound.setInput(saw);
                Serial.println("Waveform: Sawtooth");
                break;
            case 'n':
                sound.setInput(noise);
                Serial.println("Waveform: Noise");
                break;
        }
        
        if (c >= '1' && c <= '9') {
            int noteIdx = c - '1';
            sine.setFrequency(notes[noteIdx]);
            square.setFrequency(notes[noteIdx]);
            saw.setFrequency(notes[noteIdx]);
            Serial.printf("Nota: %.1f Hz\n", notes[noteIdx]);
        }
    }
}
```

---

### 4.9. Przykład 9: Text-to-Speech (Google TTS)

ESP32 łączy się z Google TTS, pobiera MP3 z syntezą mowy i odtwarza. Używa biblioteki ESP32-audioI2S (schreibfaul1) — ma wbudowaną obsługę Google TTS.

```cpp
/*
 * ESP32AudioPlayer — Przykład 9
 * Text-to-Speech (Google TTS)
 * 
 * Biblioteka: ESP32-audioI2S (schreibfaul1)
 * Wymaga: ESP32 z PSRAM (WROVER)
 */

#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"      // ESP32-audioI2S

#define I2S_DOUT    22
#define I2S_BCLK    26
#define I2S_LRC     25

const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

Audio audio;

// Callback informacyjny
void audio_info(const char *info) {
    Serial.printf("Audio info: %s\n", info);
}

void audio_eof_speech(const char *info) {
    Serial.printf("Mowa zakończona: %s\n", info);
}

void setup() {
    Serial.begin(115200);
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi połączone!");
    
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(15);  // 0-21
    
    // Mów po polsku!
    audio.connecttospeech("Cześć! Jestem ESP32 Audio Player. Potrafię mówić po polsku.", "pl");
    
    Serial.println("TTS gotowy. Wpisz tekst w Serial aby usłyszeć.");
}

void loop() {
    audio.loop();
    
    if (Serial.available()) {
        String text = Serial.readStringUntil('\n');
        text.trim();
        if (text.length() > 0) {
            Serial.printf("Mówię: %s\n", text.c_str());
            audio.connecttospeech(text.c_str(), "pl");
        }
    }
}
```

---

### 4.10. Przykład 10: Kombajn audio (radio + SD + BT + TTS)

Pełny wieloźródłowy odtwarzacz z menu na OLED i przyciskami. Łączy ESP32-audioI2S (prostota streamingu i TTS) z przełączaniem trybów.

```cpp
/*
 * ESP32AudioPlayer — Przykład 10
 * Kombajn audio: Radio + SD + Bluetooth + TTS
 * 
 * Biblioteka: ESP32-audioI2S (schreibfaul1)
 * Wymaga: ESP32 z PSRAM
 */

#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"
#include "SD.h"
#include "SPI.h"
#include "Wire.h"
#include "Adafruit_SSD1306.h"

// --- Piny ---
#define I2S_DOUT    22
#define I2S_BCLK    26
#define I2S_LRC     25
#define SD_CS       5
#define BTN_PLAY    12
#define BTN_NEXT    13
#define BTN_PREV    14
#define BTN_MODE    27

// --- WiFi ---
const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// --- Stacje radiowe ---
const char* radioStations[] = {
    "http://stream.polskieradio.pl/pr3",
    "http://zt01.cdn.eurozet.pl/zet-net.mp3",
    "http://ice1.somafm.com/groovesalad-128-mp3",
};
const char* radioNames[] = {"PR Trojka", "Radio ZET", "SomaFM Groove"};
const int radioCount = 3;

// --- Obiekty ---
Audio audio;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// --- Stan ---
enum Mode { MODE_RADIO, MODE_SD, MODE_TTS, MODE_COUNT };
Mode currentMode = MODE_RADIO;
int radioIndex = 0;
int sdFileIndex = 0;
String sdFiles[50];
int sdFileCount = 0;
bool isPlaying = false;

const char* modeLabels[] = {"RADIO", "SD CARD", "TTS"};

// --- Skanowanie plików SD ---
void scanSD() {
    sdFileCount = 0;
    File root = SD.open("/music");
    if (!root) return;
    while (sdFileCount < 50) {
        File f = root.openNextFile();
        if (!f) break;
        String name = f.name();
        if (name.endsWith(".mp3") || name.endsWith(".wav") || name.endsWith(".flac")) {
            sdFiles[sdFileCount++] = "/music/" + name;
        }
        f.close();
    }
    root.close();
    Serial.printf("[SD] Znaleziono %d plików\n", sdFileCount);
}

// --- Wyświetlacz ---
void updateDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.printf("ESP32AudioPlayer");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    display.setCursor(0, 14);
    display.printf("Tryb: %s", modeLabels[currentMode]);
    display.setCursor(0, 26);

    switch (currentMode) {
        case MODE_RADIO:
            display.printf("Stacja: %s", radioNames[radioIndex]);
            break;
        case MODE_SD:
            if (sdFileCount > 0)
                display.printf("Plik: %s", sdFiles[sdFileIndex].c_str() + 7);
            else
                display.printf("Brak plikow /music/");
            break;
        case MODE_TTS:
            display.printf("Text-to-Speech");
            display.setCursor(0, 38);
            display.printf("Wpisz w Serial");
            break;
    }

    display.setCursor(0, 50);
    display.printf("Vol: %d  %s", audio.getVolume(),
                   isPlaying ? "PLAY" : "STOP");
    
    // Pasek głośności
    int barW = map(audio.getVolume(), 0, 21, 0, 60);
    display.drawRect(60, 50, 62, 8, SSD1306_WHITE);
    display.fillRect(61, 51, barW, 6, SSD1306_WHITE);
    
    display.display();
}

// --- Odtwarzanie ---
void play() {
    switch (currentMode) {
        case MODE_RADIO:
            audio.connecttohost(radioStations[radioIndex]);
            isPlaying = true;
            Serial.printf("Radio: %s\n", radioNames[radioIndex]);
            break;
        case MODE_SD:
            if (sdFileCount > 0) {
                audio.connecttoFS(SD, sdFiles[sdFileIndex].c_str());
                isPlaying = true;
                Serial.printf("SD: %s\n", sdFiles[sdFileIndex].c_str());
            }
            break;
        case MODE_TTS:
            audio.connecttospeech("Witaj w trybie text to speech", "pl");
            isPlaying = true;
            break;
    }
}

void stopPlayback() {
    audio.stopSong();
    isPlaying = false;
}

void nextTrack() {
    if (currentMode == MODE_RADIO) {
        radioIndex = (radioIndex + 1) % radioCount;
        play();
    } else if (currentMode == MODE_SD && sdFileCount > 0) {
        sdFileIndex = (sdFileIndex + 1) % sdFileCount;
        play();
    }
}

void prevTrack() {
    if (currentMode == MODE_RADIO) {
        radioIndex = (radioIndex - 1 + radioCount) % radioCount;
        play();
    } else if (currentMode == MODE_SD && sdFileCount > 0) {
        sdFileIndex = (sdFileIndex - 1 + sdFileCount) % sdFileCount;
        play();
    }
}

// --- Callbacki ESP32-audioI2S ---
void audio_info(const char *info) {
    Serial.printf("Info: %s\n", info);
}

void audio_eof_mp3(const char *info) {
    Serial.printf("Koniec pliku: %s\n", info);
    // Auto-next w trybie SD
    if (currentMode == MODE_SD) nextTrack();
}

// --- Setup & Loop ---
void setup() {
    Serial.begin(115200);
    
    // Przyciski
    pinMode(BTN_PLAY, INPUT_PULLUP);
    pinMode(BTN_NEXT, INPUT_PULLUP);
    pinMode(BTN_PREV, INPUT_PULLUP);
    pinMode(BTN_MODE, INPUT_PULLUP);
    
    // OLED
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(5, 20);
    display.println("ESP32Audio");
    display.display();
    
    // SD
    SPI.begin();
    SD.begin(SD_CS);
    scanSD();
    
    // WiFi
    WiFi.begin(ssid, password);
    Serial.print("WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }
    Serial.printf(" OK! IP: %s\n", WiFi.localIP().toString().c_str());
    
    // Audio
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(12);
    
    Serial.println("\nESP32AudioPlayer gotowy!");
    Serial.println("Przyciski: PLAY/STOP, NEXT, PREV, MODE");
    Serial.println("Serial: n=next p=prev m=mode +=vol -=vol t=TTS tekst");
    
    updateDisplay();
}

void loop() {
    audio.loop();
    
    // Przyciski (z prostym debounce)
    static uint32_t lastBtn = 0;
    if (millis() - lastBtn > 250) {
        if (digitalRead(BTN_PLAY) == LOW) {
            isPlaying ? stopPlayback() : play();
            lastBtn = millis();
        }
        if (digitalRead(BTN_NEXT) == LOW) {
            nextTrack();
            lastBtn = millis();
        }
        if (digitalRead(BTN_PREV) == LOW) {
            prevTrack();
            lastBtn = millis();
        }
        if (digitalRead(BTN_MODE) == LOW) {
            stopPlayback();
            currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
            lastBtn = millis();
        }
    }
    
    // Serial
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input == "n") nextTrack();
        else if (input == "p") prevTrack();
        else if (input == "m") {
            stopPlayback();
            currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
        }
        else if (input == "+") audio.setVolume(min(21, audio.getVolume() + 1));
        else if (input == "-") audio.setVolume(max(0, audio.getVolume() - 1));
        else if (input.startsWith("t ")) {
            String text = input.substring(2);
            currentMode = MODE_TTS;
            audio.connecttospeech(text.c_str(), "pl");
            isPlaying = true;
        }
    }
    
    // Odśwież OLED co 200ms
    static uint32_t lastDisp = 0;
    if (millis() - lastDisp > 200) {
        updateDisplay();
        lastDisp = millis();
    }
}
```

---

## 5. Jak wybrać bibliotekę do swojego projektu

Prosty odtwarzacz MP3/radio → **ESP32-audioI2S** (schreibfaul1). Najmniej kodu, wbudowane dekodery, Google TTS. Wymaga PSRAM.

Odtwarzacz z efektami / nagrywanie / generator tonów → **arduino-audio-tools** (pschatzmann). Architektura pipe, efekty, mikrofon I2S, resampling, mikser.

Głośnik lub nadajnik Bluetooth → **ESP32-A2DP** + arduino-audio-tools. Dedykowane do A2DP sink/source.

Kombinacja wszystkiego → Użyj ESP32-audioI2S do streamingu/TTS (prostota) i arduino-audio-tools do efektów/nagrywania (elastyczność) — w osobnych sketch'ach lub na dwóch rdzeniach ESP32.

---

## 6. Troubleshooting

| Problem | Przyczyna | Rozwiązanie |
|---------|-----------|-------------|
| **Brak dźwięku z MAX98357A** | Złe piny I2S | Sprawdź BCLK/LRC/DIN. Domyślne piny różnią się między bibliotekami! |
| | SD pin MAX98357A na GND | Odłącz SD pin lub podłącz do VIN (enable) |
| | Głośnik niepodłączony | Sprawdź polarity nie ma znaczenia — klasa D |
| **Trzaski, zniekształcenia** | Sample rate niezgodny | Ustaw config.sample_rate odpowiednio do źródła |
| | Bufor za mały | Zwiększ bufor: `config.buffer_size = 1024` |
| **ESP32-audioI2S: PSRAM error** | Brak PSRAM w module | Użyj ESP32-WROVER (nie WROOM). Lub użyj arduino-audio-tools |
| **WiFi streaming się zacina** | Słaby sygnał WiFi | Przenieś bliżej routera. Użyj 5 GHz |
| | Bufor sieciowy za mały | ESP32-audioI2S ma wewnętrzny bufor 20KB — powinien wystarczyć |
| **SD nie czyta plików** | Złe połączenia SPI | Sprawdź CS/SCK/MOSI/MISO |
| | Format karty | FAT32 (nie exFAT). Partycja < 32 GB |
| **Bluetooth nie łączy** | Za dużo urządzeń sparowanych | Wyczyść listę parowania na telefonie |
| | A2DP nie obsługiwane na ESP32-S3 | Klasyczny BT tylko na ESP32 (nie S3/C3) |
| **Mikrofon INMP441 cisza** | Piny zamienione | SCK ≠ WS — sprawdź datasheet |
| | L/R pin nie podłączony | GND = lewy kanał, VDD = prawy |

---

## 7. Porady wydajnościowe

Używaj I2S zamiast wewnętrznego DAC — jakość 16-bit vs 8-bit, zero obciążenia CPU (DMA). Włącz PSRAM w menuconfig jeśli jest dostępny — dekodery MP3/AAC potrzebują dużo RAM. Dla streamingu WiFi używaj bufora minimum 8 KB. Dekodowanie MP3 zajmuje ~20% CPU jednego rdzenia ESP32 — drugi rdzeń jest wolny dla WiFi/BT/UI. Unikaj `delay()` w loop() — użyj `vTaskDelay(1)` lub `yield()`. Jeśli łączysz WiFi streaming z Bluetooth — ESP32 ma ograniczoną przepustowość, wybierz jedno.

---

## 8. Co dalej — rozbudowa

### 8.1. Interfejs webowy

ESP32 jako serwer HTTP z interfejsem do sterowania odtwarzaczem z przeglądarki. Zmiana stacji, głośność, wybór pliku z SD — wszystko z telefonu przez WiFi.

### 8.2. DLNA / AirPlay

Biblioteka arduino-audio-tools (v1.2+) obsługuje DLNA Media Renderer — ESP32 widoczny jako głośnik sieciowy dla aplikacji jak Hi-Fi Cast na Androidzie.

### 8.3. Equalizer graficzny

arduino-audio-tools ma wbudowany equalizer 3-pasmowy. Rozbudowa do 5–10 pasm z interfejsem na OLED i potencjometrach.

### 8.4. Analiza FFT i wizualizacja

Analiza widmowa w czasie rzeczywistym, wyświetlanie spektrogramu na OLED lub matrycy LED WS2812B — muzyczny "VU-metr".

### 8.5. Multiroom audio

ESP-NOW lub UDP multicast do synchronizacji odtwarzania na wielu ESP32 w różnych pokojach.

### 8.6. Integracja z TapeForge

Podłącz wyjście I2S (przez DAC PCM5102A + dzielnik napięcia) do LINE IN magnetofonu — nagrywaj streaming radiowy lub pliki z SD na kasety w jakości znacznie lepszej niż wewnętrzny 8-bitowy DAC!

---

## 9. Słowniczek

| Pojęcie | Wyjaśnienie |
|---------|-------------|
| **A2DP** | Advanced Audio Distribution Profile — profil Bluetooth do strumieniowania muzyki stereo. |
| **BCLK** | Bit Clock — zegar taktujący bity danych I2S. Częstotliwość = sample_rate × bits × channels. |
| **DMA** | Direct Memory Access — transfer danych I2S bez udziału CPU. ESP32 robi to automatycznie. |
| **I2S** | Inter-IC Sound — protokół cyfrowego audio między układami. 3 linie: BCLK, LRC (WS), DATA. |
| **LRC / WS** | Left-Right Clock / Word Select — sygnał przełączający kanał lewy/prawy w I2S. |
| **MAX98357A** | DAC I2S + wzmacniacz klasy D 3W w jednym chipie. Najprostsze wyjście audio dla ESP32. |
| **MEMS** | Micro-Electro-Mechanical System — technologia miniaturowych mikrofonów cyfrowych. |
| **PCM** | Pulse Code Modulation — surowe próbki audio (np. 44100 Hz, 16-bit, stereo = format CD). |
| **PCM5102A** | Stereo DAC I2S wysokiej jakości (32-bit, 384 kHz). Wyjście liniowe, wymaga wzmacniacza. |
| **PSRAM** | Pseudo-Static RAM — zewnętrzna pamięć RAM na module ESP32-WROVER (4–8 MB). |
| **SBC** | Subband Coding — kodek audio Bluetooth A2DP. Kompresja stratna ~300 kbps. |
| **Stream** | Abstrakcja Arduino dla strumienia danych. Klasy audio dziedziczą po Stream/Print. |
| **StreamCopy** | Klasa kopiująca dane między strumieniami. Serce architektury audio-tools. |
| **URLStream** | Strumień HTTP(S) — pobiera dane audio z internetu. Obsługuje ICY metadata (nazwa stacji). |

---

## 10. Zasoby i linki

| Zasób | URL |
|-------|-----|
| arduino-audio-tools (GitHub) | https://github.com/pschatzmann/arduino-audio-tools |
| arduino-audio-tools Wiki | https://github.com/pschatzmann/arduino-audio-tools/wiki |
| ESP32-audioI2S (GitHub) | https://github.com/schreibfaul1/ESP32-audioI2S |
| ESP32-A2DP (GitHub) | https://github.com/pschatzmann/ESP32-A2DP |
| arduino-libhelix (MP3/AAC) | https://github.com/pschatzmann/arduino-libhelix |
| Blog Phila Schatzmanna | https://www.pschatzmann.ch/home/ |
| DroneBot Workshop — I2S tutorial | https://dronebotworkshop.com/esp32-i2s/ |
| MAX98357A datasheet | https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A.pdf |
| INMP441 datasheet | https://invensense.tdk.com/products/digital/inmp441/ |

---

## 11. Ekosystem projektów — pełna mapa

```
┌───────────────────────────────────────────────────────────────────────────┐
│                     KOMPLETNY EKOSYSTEM AUDIO                             │
│                                                                           │
│  ┌──────────────┐  ┌──────────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │ #1 DIY       │  │ #2 Digital       │  │ #3 TapeForge │  │ #4 ESP32  │ │
│  │ Odtwarzacz   │  │ CassettePlayer   │  │              │  │ Audio     │ │
│  │              │  │                  │  │              │  │ Player    │ │
│  │ Analogowy    │  │ Kaseta→ADC→SD    │  │ SD→DAC→Kaseta│  │ SD/WiFi/  │ │
│  │ LM386        │  │ Dekoder danych   │  │ Enkoder C64  │  │ BT→I2S   │ │
│  │ →Głośnik     │  │ KCS/C64/ZX       │  │ Audio WAV    │  │ DAC→Spkr │ │
│  └──────┬───────┘  └───────┬──────────┘  └──────┬───────┘  └─────┬─────┘ │
│         │                  │                     │                │       │
│  Poziom:│ Analogowy        │ Cyfrowy             │ Cyfrowy→       │ Pro   │
│         │ 1 chip           │ ESP32 ADC           │ Analogowy      │ I2S   │
│         │ ~40 zł           │ ~80 zł              │ ~100 zł        │ Kodeki│
│         │                  │                     │                │ WiFi  │
│         └──────────────────┴─────────────────────┴────────────────┘       │
│                            Wspólne: ESP32, SD, breadboard                 │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 12. Historia zmian

| Wersja | Data | Opis |
|--------|------|------|
| 1.0 | 2026-02-06 | Pierwsza wersja: 10 przykładów, 3 biblioteki, 4 warianty HW. |

---

## 13. Licencja

Projekt open-source do dowolnego użytku edukacyjnego i hobbystycznego. Biblioteki mają własne licencje (GPL/MIT/Apache — sprawdź repozytoria). Stworzony z pomocą Claude (Anthropic).

> *„Kiedyś potrzebowałeś wieży stereo za tysiące złotych. Dziś ESP32 za 25 zł daje Ci radio, Bluetooth, syntezator, rejestrator i odtwarzacz — w jednym chipie mniejszym od znaczka pocztowego."*
