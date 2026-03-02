# 📡 ESP32StreamingAudioPlayer — Dokumentacja Projektu

> **Wersja:** 1.0  
> **Data:** 2026-02-06  
> **Poziom trudności:** ⭐⭐⭐ Zaawansowany  
> **Szacowany koszt:** 60–130 zł  
> **Platforma:** ESP32 / ESP32-S3 (Arduino Framework)  
> **Biblioteka bazowa:** arduino-audio-tools (Phil Schatzmann)  

---

## 1. Opis projektu

ESP32StreamingAudioPlayer to platforma do strumieniowania audio przez WiFi w obu kierunkach — ESP32 jako **odbiornik** (radio internetowe, Snapcast client, DLNA renderer) oraz jako **nadajnik** (web serwer audio, RTSP server, UDP multicast). Całość oparta na bibliotece `arduino-audio-tools` i jej ekosystemie.

Projekt odpowiada na pytanie: *"Co jeszcze mogę robić z audio przez WiFi na ESP32?"* — i odpowiedź brzmi: praktycznie wszystko.

### 1.1. Scenariusze streamingowe

```
 ═══════════════ ODBIORNIK (ESP32 pobiera audio z sieci) ═══════════════

  Internet ──HTTP──► URLStream ──► MP3Decoder ──► I2SStream ──► Głośnik
  (radio)           (klient HTTP)   (Helix)        (DAC I2S)     (MAX98357A)

  Icecast  ──HTTP──► ICYStream ──► MP3/AAC ──► VolumeStream ──► I2SStream
  (+ metadata)       (z tytułami)                (głośność)

  Snapcast ──TCP───► SnapClient ──► OpusDecoder ──► I2SStream
  server            (synchronizacja)

  DLNA     ──HTTP──► DLNARenderer ──► MultiDecoder ──► I2SStream
  kontroler          (UPnP)


 ═══════════════ NADAJNIK (ESP32 wysyła audio do sieci) ════════════════

  Mikrofon ──I2S──► ESP32 ──HTTP──► AudioWAVServer ──► Przeglądarka
  (INMP441)         (serwer WAV)                        (wielu klientów)

  Mikrofon ──I2S──► ESP32 ──RTSP──► VLC/FFplay
  lub SD            (serwer RTSP)    (klient RTSP)

  Mikrofon ──I2S──► ESP32 ──UDP───► Inny ESP32 ──► Głośnik
  lub generator     (multicast)      (multiroom)

  SD/gen   ──────► ESP32 ──HTTP──► AudioServerMP3 ──► Przeglądarka
                    (serwer MP3)                       (skompresowany!)
```

### 1.2. Przykłady w tym projekcie

| # | Nazwa | Kierunek | Protokół | Opis |
|---|-------|----------|----------|------|
| 1 | Radio internetowe (prosty) | ↓ odbiornik | HTTP | URLStream → MP3 → I2S |
| 2 | Radio z metadanymi ICY | ↓ odbiornik | HTTP+ICY | Nazwa stacji i utworu na OLED |
| 3 | Radio z playlistą stacji | ↓ odbiornik | HTTP | AudioPlayer + AudioSourceURL |
| 4 | Web serwer audio WAV | ↑ nadajnik | HTTP | Mikrofon → WAV → przeglądarka |
| 5 | Web serwer MP3 (skompresowany) | ↑ nadajnik | HTTP | SD/mikrofon → MP3 → klienci |
| 6 | Serwer RTSP | ↑ nadajnik | RTSP | Mikrofon → RTSP → VLC |
| 7 | UDP multicast (multiroom) | ↔ oba | UDP | Synchronizowany dźwięk w wielu pokojach |
| 8 | Snapcast client | ↓ odbiornik | TCP | Zsynchronizowane multiroom audio |
| 9 | DLNA Media Renderer | ↓ odbiornik | UPnP/HTTP | ESP32 jako głośnik sieciowy |
| 10 | Pełne radio z interfejsem WWW | ↔ oba | HTTP+WS | Sterowanie z telefonu, OLED, przyciski |

---

## 2. Klasy streamingowe w arduino-audio-tools

### 2.1. Odbiorniki (klienty HTTP)

**URLStream** — podstawowy klient HTTP(S). Otwiera URL i dostarcza strumień bajtów. Obsługuje przekierowania, chunked transfer, HTTPS (z certyfikatem).

```cpp
URLStream url("ssid", "password");
url.begin("http://stream.example.com/radio.mp3", "audio/mp3");
// Teraz url jest strumieniem z którego można czytać
```

**ICYStream** — rozszerza URLStream o obsługę metadanych Icecast/SHOUTcast (ICY protocol). Odczytuje nagłówki `icy-name`, `icy-genre`, `icy-url` oraz inline metadata (tytuł aktualnie granego utworu) — `StreamTitle='Artist - Song'`.

```cpp
ICYStream icy("ssid", "password");
icy.begin("http://stream.example.com/radio.mp3");
// icy.httpRequest().header("icy-name") → nazwa stacji
// Callback na zmianę tytułu:
icy.setMetadataCallback([](MetaDataType type, const char* value, int len) {
    if (type == Title) Serial.printf("Teraz gra: %s\n", value);
});
```

**AudioSourceURL** — wrapper na URLStream do użycia z AudioPlayer. Przyjmuje tablicę URLi i pozwala przełączać się między nimi (next/prev).

```cpp
const char* urls[] = {"http://radio1.mp3", "http://radio2.mp3"};
URLStream urlStream("ssid", "pwd");
AudioSourceURL source(urlStream, urls, "audio/mp3");
AudioPlayer player(source, i2s, decoder);
```

### 2.2. Nadajniki (serwery HTTP)

**AudioWAVServer** — serwer HTTP serwujący audio jako strumień WAV. Klient (przeglądarka, VLC) łączy się pod IP ESP32 i otrzymuje nieskompresowane audio PCM w kontenerze WAV. Jeden klient na raz.

**AudioWAVServerEx** — rozszerzona wersja obsługująca wielu klientów jednocześnie. Używa wewnętrznej biblioteki TinyHTTP.

**AudioEncoderServer** — serwer HTTP z kompresją. Wysyła MP3, Opus lub inny skompresowany format. Mniejsze zapotrzebowanie na pasmo niż WAV.

### 2.3. Komunikacja niskiego poziomu

**UDPStream** — wysyłanie/odbieranie surowego audio przez UDP. Idealny do multicast (jeden nadajnik, wielu odbiorców w sieci lokalnej). Minimalne opóźnienie, brak narzutu HTTP.

**RTSPStream** — serwer RTSP zintegrowany z audio-tools. Klienty jak VLC, FFplay lub inne ESP32 łączą się i odbierają strumień. Obsługuje PCM i kodeki.

### 2.4. Protokoły wysokiego poziomu

**SnapClient** — klient Snapcast (https://github.com/badaix/snapcast). Multiroom audio z synchronizacją czasową między wieloma ESP32 w domu. Dekoder Opus. Serwer Snapcast działa na Raspberry Pi lub PC.

**DLNARenderer** — ESP32 jako DLNA/UPnP Media Renderer. Widoczny w sieci dla aplikacji sterujących (np. Hi-Fi Cast na Androidzie, BubbleUPnP). Aplikacja wybiera muzykę, ESP32 ją odtwarza.

### 2.5. Mapa klas

```
                    ┌──────────────────────┐
                    │  Źródła sieciowe     │
                    ├──────────────────────┤
                    │ URLStream            │ ← HTTP GET
                    │ ICYStream            │ ← HTTP + ICY metadata
                    │ AudioSourceURL       │ ← Playlista URL dla AudioPlayer
                    │ UDPStream (RX)       │ ← UDP unicast/multicast
                    │ RTSPStream (klient)  │ ← RTSP
                    │ SnapClient           │ ← Snapcast TCP
                    └──────────┬───────────┘
                               │ surowe lub zakodowane bajty
                               ▼
                    ┌──────────────────────┐
                    │  Dekodery            │
                    ├──────────────────────┤
                    │ MP3DecoderHelix      │
                    │ AACDecoderHelix      │
                    │ WAVDecoder           │
                    │ FLACDecoder          │
                    │ OpusDecoder          │
                    │ VorbisDecoder        │
                    │ MultiDecoder         │ ← auto-detekcja formatu
                    └──────────┬───────────┘
                               │ PCM (16-bit, 44.1kHz, stereo)
                               ▼
                    ┌──────────────────────┐
                    │  Przetwarzanie       │
                    ├──────────────────────┤
                    │ VolumeStream         │ ← regulacja głośności
                    │ ResampleStream       │ ← zmiana sample rate
                    │ AudioEffectStream    │ ← efekty: echo, reverb
                    │ ChannelFormatConverterStream │ ← mono↔stereo
                    └──────────┬───────────┘
                               │ PCM
                               ▼
                    ┌──────────────────────┐
                    │  Wyjścia             │
                    ├──────────────────────┤
                    │ I2SStream (TX)       │ → MAX98357A / PCM5102A
                    │ AnalogAudioStream    │ → wewnętrzny DAC GPIO25
                    │ AudioWAVServerEx     │ → HTTP do przeglądarki
                    │ AudioEncoderServer   │ → HTTP MP3 do klientów
                    │ UDPStream (TX)       │ → UDP multicast
                    │ RTSPStream (serwer)  │ → RTSP do VLC
                    │ A2DPStream           │ → Bluetooth A2DP
                    └──────────────────────┘
```

---

## 3. Hardware

### 3.1. BOM (zalecany wariant)

| # | Element | Opis | Cena |
|---|---------|------|------|
| 1 | ESP32 DevKit V1 | WiFi + BT, 2 rdzenie, 520KB RAM | 20–35 zł |
| 2 | MAX98357A | I2S DAC + wzmacniacz 3W | 8–15 zł |
| 3 | Głośnik 4Ω/8Ω 3W | Pełnozakresowy | 5–10 zł |
| 4 | INMP441 | Mikrofon I2S MEMS (do nadawania) | 8–15 zł |
| 5 | OLED 0.96" SSD1306 | Wyświetlacz I2C (opcja) | 8–15 zł |
| 6 | Przyciski × 4 | Tact switch | ~1 zł |
| 7 | Breadboard + kabelki | Montaż | 10–15 zł |
| | **RAZEM** | | **~60–110 zł** |

Karta SD nie jest potrzebna — audio pochodzi z sieci! (Chyba że chcesz buforować lub nagrywać.)

### 3.2. Pinout

```
ESP32            MAX98357A         INMP441           OLED SSD1306
──────           ──────────        ────────           ────────────
GPIO26 ─── BCLK                   GPIO14 ─── SCK    GPIO21 ─── SDA
GPIO25 ─── LRC                    GPIO15 ─── WS     GPIO16 ─── SCL
GPIO22 ─── DIN                    GPIO32 ─── SD     3.3V   ─── VCC
5V     ─── VIN                    3.3V   ─── VDD    GND    ─── GND
GND    ─── GND                    GND    ─── GND
                                  GND    ─── L/R
```

---

## 4. Przykłady — kompletne sketche

### 4.1. Przykład 1: Radio internetowe — minimum kodu

Absolutne minimum do działającego radia internetowego. 20 linii kodu.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 1
 * Radio internetowe (minimum)
 * 
 * Łańcuch: URLStream → MP3DecoderHelix → I2SStream → MAX98357A
 * 
 * Biblioteki:
 *   git clone https://github.com/pschatzmann/arduino-audio-tools.git
 *   git clone https://github.com/pschatzmann/arduino-libhelix.git
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/Communication/AudioHttp.h"

URLStream url("TwojaSiecWiFi", "TwojeHaslo");
I2SStream i2s;
MP3DecoderHelix mp3;
EncodedAudioStream decoder(&i2s, &mp3);
StreamCopy copier(decoder, url);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = 26;
    cfg.pin_ws = 25;
    cfg.pin_data = 22;
    i2s.begin(cfg);

    decoder.begin();
    url.begin("http://stream.srg-ssr.ch/m/rsj/mp3_128", "audio/mp3");

    Serial.println("Radio gra! (Radio Swiss Jazz)");
}

void loop() {
    copier.copy();
}
```

---

### 4.2. Przykład 2: Radio z metadanymi ICY na OLED

ICYStream pobiera metadane ze streamu Icecast — nazwa stacji i aktualnie grany utwór. Wyświetlane na OLED.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 2
 * Radio z metadanymi ICY + OLED
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/Communication/AudioHttp.h"
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// WiFi
const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// Audio chain
ICYStream icy(ssid, password);
I2SStream i2s;
VolumeStream volume(i2s);          // Regulacja głośności w łańcuchu
MP3DecoderHelix mp3;
EncodedAudioStream decoder(&volume, &mp3);
StreamCopy copier(decoder, icy);

// OLED
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// Metadane
char stationName[64] = "Łączenie...";
char currentTitle[128] = "";
float currentVolume = 0.7;

// Callback metadanych ICY
void onMetadata(MetaDataType type, const char* value, int len) {
    switch (type) {
        case Title:
            strncpy(currentTitle, value, 127);
            Serial.printf("🎵 Teraz gra: %s\n", value);
            break;
        case Name:
            strncpy(stationName, value, 63);
            Serial.printf("📻 Stacja: %s\n", value);
            break;
        case Genre:
            Serial.printf("🎶 Gatunek: %s\n", value);
            break;
        default:
            break;
    }
}

void updateDisplay() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // Nazwa stacji
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(stationName);
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // Tytuł (z zawijaniem)
    display.setCursor(0, 14);
    display.setTextSize(1);
    // Podziel długi tytuł na linie
    String title(currentTitle);
    int y = 14;
    while (title.length() > 0 && y < 48) {
        display.setCursor(0, y);
        if (title.length() > 21) {
            display.println(title.substring(0, 21));
            title = title.substring(21);
        } else {
            display.println(title);
            title = "";
        }
        y += 10;
    }

    // Pasek głośności
    display.setCursor(0, 56);
    display.printf("Vol: %d%%", (int)(currentVolume * 100));
    int barW = (int)(currentVolume * 70);
    display.drawRect(50, 56, 72, 8, SSD1306_WHITE);
    display.fillRect(51, 57, barW, 6, SSD1306_WHITE);

    display.display();
}

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

    // OLED
    Wire.begin(21, 16);  // SDA=21, SCL=16 (22 zajęty przez I2S DIN)
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.println("WiFi...");
    display.display();

    // I2S
    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = 26;
    cfg.pin_ws = 25;
    cfg.pin_data = 22;
    i2s.begin(cfg);

    // Głośność
    auto vcfg = volume.defaultConfig();
    vcfg.copyFrom(cfg);
    volume.begin(vcfg);
    volume.setVolume(currentVolume);

    // Dekoder
    decoder.begin();

    // Metadane
    icy.setMetadataCallback(onMetadata);

    // Start streamu
    icy.begin("http://ice1.somafm.com/groovesalad-128-mp3", "audio/mp3");

    Serial.println("Radio ICY z metadanymi gotowe!");
    Serial.println("Komendy: +=głośniej -=ciszej n=następna stacja");
}

void loop() {
    copier.copy();

    // Serial sterowanie
    if (Serial.available()) {
        char c = Serial.read();
        if (c == '+') {
            currentVolume = min(1.0f, currentVolume + 0.05f);
            volume.setVolume(currentVolume);
        } else if (c == '-') {
            currentVolume = max(0.0f, currentVolume - 0.05f);
            volume.setVolume(currentVolume);
        }
    }

    // OLED co 250ms
    static uint32_t lastDisp = 0;
    if (millis() - lastDisp > 250) {
        updateDisplay();
        lastDisp = millis();
    }
}
```

---

### 4.3. Przykład 3: Radio z playlistą stacji (AudioPlayer + AudioSourceURL)

Klasa AudioPlayer zarządza przełączaniem między stacjami (next/prev), automatycznym reconnectem i głośnością.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 3
 * Radio z playlistą stacji (AudioPlayer)
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/Communication/AudioHttp.h"

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// Lista stacji — dodaj swoje ulubione!
const char* stations[] = {
    "http://stream.polskieradio.pl/pr1",                            // Jedynka
    "http://stream.polskieradio.pl/pr3",                            // Trójka
    "http://zt01.cdn.eurozet.pl/zet-net.mp3",                      // Radio ZET
    "http://n-11-14.dcs.redcdn.pl/sc/o2/Eurozet/live/chillizet.livx", // ChilliZET
    "http://stream.srg-ssr.ch/m/rsj/mp3_128",                      // Swiss Jazz
    "http://ice1.somafm.com/groovesalad-128-mp3",                   // SomaFM Groove
    "http://ice1.somafm.com/defcon-128-mp3",                        // SomaFM DEF CON
    "http://stream.srg-ssr.ch/m/drs3/mp3_128",                     // SRF 3
};
const char* stationNames[] = {
    "PR Jedynka", "PR Trojka", "Radio ZET", "ChilliZET",
    "Swiss Jazz", "Groove Salad", "DEF CON Radio", "SRF 3"
};
const int stationCount = 8;

URLStream urlStream(ssid, password);
AudioSourceURL source(urlStream, stations, "audio/mp3");
I2SStream i2s;
MP3DecoderHelix decoder;
AudioPlayer player(source, i2s, decoder);

int currentStation = 0;

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = 26;
    cfg.pin_ws = 25;
    cfg.pin_data = 22;

    player.setVolume(0.7);
    player.begin();

    Serial.println("Radio z playlistą gotowe!");
    Serial.println("Komendy: n=next  p=prev  0-7=stacja  +=vol  -=vol");
    Serial.printf("Stacja: [0] %s\n", stationNames[0]);
}

void loop() {
    player.copy();

    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'n') {
            player.next();
            currentStation = (currentStation + 1) % stationCount;
            Serial.printf("→ [%d] %s\n", currentStation, stationNames[currentStation]);
        } else if (c == 'p') {
            player.previous();
            currentStation = (currentStation - 1 + stationCount) % stationCount;
            Serial.printf("← [%d] %s\n", currentStation, stationNames[currentStation]);
        } else if (c >= '0' && c <= '7') {
            int idx = c - '0';
            player.setIndex(idx);
            currentStation = idx;
            Serial.printf("⏭ [%d] %s\n", idx, stationNames[idx]);
        } else if (c == '+') {
            float v = min(1.0f, player.volume() + 0.05f);
            player.setVolume(v);
            Serial.printf("Vol: %d%%\n", (int)(v * 100));
        } else if (c == '-') {
            float v = max(0.0f, player.volume() - 0.05f);
            player.setVolume(v);
            Serial.printf("Vol: %d%%\n", (int)(v * 100));
        }
    }
}
```

---

### 4.4. Przykład 4: Web serwer audio WAV (mikrofon → przeglądarka)

ESP32 jako serwer HTTP — zbiera audio z mikrofonu I2S i streamuje jako WAV do każdego klienta w sieci. Otwórz `http://<IP_ESP32>` w przeglądarce i słuchasz na żywo.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 4
 * Serwer audio WAV (mikrofon → WiFi → przeglądarka)
 * 
 * Klasa: AudioWAVServer (jednoklientowy) 
 *   lub AudioWAVServerEx (wieloklientowy)
 */

#include "AudioTools.h"
#include "AudioTools/Communication/AudioServer.h"

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

const int sampleRate = 16000;
const int channels = 1;
const int bitsPerSample = 16;

I2SStream microphone;                          // I2S wejście (mikrofon)
AudioWAVServer server(ssid, password, 80);     // Serwer HTTP na porcie 80
StreamCopy copier(server, microphone);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // Konfiguracja mikrofonu I2S (INMP441)
    auto micCfg = microphone.defaultConfig(RX_MODE);
    micCfg.pin_bck = 14;
    micCfg.pin_ws = 15;
    micCfg.pin_data = 32;
    micCfg.sample_rate = sampleRate;
    micCfg.channels = channels;
    micCfg.bits_per_sample = bitsPerSample;
    micCfg.i2s_format = I2S_STD_FORMAT;
    microphone.begin(micCfg);

    // Konfiguracja serwera
    auto serverCfg = server.defaultConfig();
    serverCfg.sample_rate = sampleRate;
    serverCfg.channels = channels;
    serverCfg.bits_per_sample = bitsPerSample;
    server.begin(serverCfg);

    Serial.println("Serwer audio WAV gotowy!");
    Serial.printf("Otwórz w przeglądarce: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("Lub w VLC: File → Open Network Stream → URL powyżej");
}

void loop() {
    copier.copy();
}
```

---

### 4.5. Przykład 5: Web serwer MP3 (skompresowany streaming)

Zamiast surowego WAV (~176 KB/s dla 44.1kHz stereo), kompresujemy do MP3 (~16 KB/s) — 10× mniejsze zapotrzebowanie na pasmo. Więcej klientów, mniejsze opóźnienia.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 5
 * Serwer audio MP3 (skompresowany streaming z mikrofonu)
 * 
 * Wymaga: arduino-liblame (MP3 encoder)
 *   git clone https://github.com/pschatzmann/arduino-liblame.git
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3LAME.h"
#include "AudioTools/Communication/AudioServer.h"

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

I2SStream microphone;
MP3EncoderLAME mp3enc;
AudioEncoderServer server(&mp3enc, ssid, password, 80);
StreamCopy copier(server, microphone);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // Mikrofon
    auto micCfg = microphone.defaultConfig(RX_MODE);
    micCfg.pin_bck = 14;
    micCfg.pin_ws = 15;
    micCfg.pin_data = 32;
    micCfg.sample_rate = 16000;
    micCfg.channels = 1;
    micCfg.bits_per_sample = 16;
    microphone.begin(micCfg);

    // Serwer MP3
    auto cfg = server.defaultConfig();
    cfg.sample_rate = 16000;
    cfg.channels = 1;
    server.begin(cfg);

    Serial.printf("Serwer MP3 na http://%s/\n", WiFi.localIP().toString().c_str());
}

void loop() {
    copier.copy();
}
```

---

### 4.6. Przykład 6: Serwer RTSP (streaming do VLC)

ESP32 jako serwer RTSP — profesjonalny protokół streamingowy. VLC łączy się podając `rtsp://<IP>:554/audio`.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 6
 * Serwer RTSP (mikrofon → VLC/FFplay)
 * 
 * Wymaga: Micro-RTSP-Audio (zintegrowane w audio-tools)
 */

#include "AudioTools.h"
#include "AudioTools/AudioLibs/RTSPStream.h"

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

I2SStream microphone;
RTSPSourceFromAudioStream source(microphone);
RTSPStream rtsp(source);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());

    // Mikrofon
    auto micCfg = microphone.defaultConfig(RX_MODE);
    micCfg.pin_bck = 14;
    micCfg.pin_ws = 15;
    micCfg.pin_data = 32;
    micCfg.sample_rate = 16000;
    micCfg.channels = 1;
    micCfg.bits_per_sample = 16;
    microphone.begin(micCfg);

    // RTSP serwer
    rtsp.begin();

    Serial.printf("Serwer RTSP gotowy!\n");
    Serial.printf("VLC: Media → Open Network → rtsp://%s:554/audio\n",
                  WiFi.localIP().toString().c_str());
}

void loop() {
    rtsp.copy();
}
```

---

### 4.7. Przykład 7: UDP Multicast (multiroom audio)

Jeden ESP32 jako nadajnik, wiele ESP32 jako odbiorniki — wszystkie grają ten sam dźwięk. UDP multicast nie wymaga połączenia punkt-punkt. Idealne do multiroom audio.

**Nadajnik (TX):**

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 7a
 * UDP Multicast NADAJNIK
 * 
 * Wysyła audio z generatora (lub mikrofonu/SD) do grupy multicast
 */

#include "AudioTools.h"

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// Adres multicast — wszystkie odbiorniki nasłuchują na tym adresie
IPAddress multicastIP(239, 1, 1, 1);
const int multicastPort = 8888;

SineWaveGenerator<int16_t> sine(16000);
GeneratedSoundStream<int16_t> source(sine);
UDPStream udp;
StreamCopy copier(udp, source);

void setup() {
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.printf("TX WiFi: %s\n", WiFi.localIP().toString().c_str());

    // Konfiguracja UDP multicast TX
    auto cfg = udp.defaultConfig();
    cfg.rx_port = multicastPort;
    cfg.remote_ip = multicastIP;
    cfg.remote_port = multicastPort;
    udp.begin(cfg);

    // Generator: sinus 440 Hz
    sine.begin(AudioInfo(16000, 1, 16), N_A4);
    source.begin();

    Serial.printf("Nadaję na %s:%d\n", multicastIP.toString().c_str(), multicastPort);
}

void loop() {
    copier.copy();
}
```

**Odbiornik (RX):**

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 7b
 * UDP Multicast ODBIORNIK
 * 
 * Odbiera audio z grupy multicast i odtwarza na I2S
 */

#include "AudioTools.h"

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

IPAddress multicastIP(239, 1, 1, 1);
const int multicastPort = 8888;

UDPStream udp;
I2SStream i2s;
StreamCopy copier(i2s, udp);

void setup() {
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.printf("RX WiFi: %s\n", WiFi.localIP().toString().c_str());

    // I2S wyjście
    auto i2sCfg = i2s.defaultConfig(TX_MODE);
    i2sCfg.pin_bck = 26;
    i2sCfg.pin_ws = 25;
    i2sCfg.pin_data = 22;
    i2sCfg.sample_rate = 16000;
    i2sCfg.channels = 1;
    i2sCfg.bits_per_sample = 16;
    i2s.begin(i2sCfg);

    // UDP multicast RX
    auto cfg = udp.defaultConfig();
    cfg.rx_port = multicastPort;
    cfg.remote_ip = multicastIP;
    cfg.remote_port = multicastPort;
    udp.begin(cfg);

    Serial.printf("Odbieram z %s:%d\n", multicastIP.toString().c_str(), multicastPort);
}

void loop() {
    copier.copy();
}
```

---

### 4.8. Przykład 8: Snapcast Client (zsynchronizowane multiroom)

Snapcast to profesjonalne rozwiązanie multiroom audio. Serwer Snapcast (na Raspberry Pi/PC) synchronizuje czasowo wszystkie klienty, eliminując echo między pokojami. ESP32 jako klient jest najtańszym sposobem na dodanie pokoju.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 8
 * Snapcast Client
 * 
 * Wymaga serwera Snapcast na Raspberry Pi/PC:
 *   sudo apt install snapserver
 *   snapserver -s "pipe:///tmp/snapfifo?name=default"
 * 
 * Biblioteka:
 *   git clone https://github.com/pschatzmann/arduino-snapclient.git
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecOpus.h"
#include "SnapClient.h"

#define ARDUINO_LOOP_STACK_SIZE (10 * 1024)

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

OpusAudioDecoder opus;
I2SStream out;
SnapClient client(out, opus);

void setup() {
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    Serial.print("WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(500);
    }
    Serial.printf(" OK: %s\n", WiFi.localIP().toString().c_str());

    // I2S
    auto cfg = out.defaultConfig(TX_MODE);
    cfg.pin_bck = 26;
    cfg.pin_ws = 25;
    cfg.pin_data = 22;
    out.begin(cfg);

    // Snapcast client — łączy się automatycznie z serwerem w sieci
    client.begin();

    Serial.println("Snapcast client gotowy. Czekam na serwer...");
}

void loop() {
    client.doLoop();
}
```

---

### 4.9. Przykład 9: DLNA Media Renderer

ESP32 widoczny jako głośnik sieciowy DLNA/UPnP. Sterowany z aplikacji na telefonie (Hi-Fi Cast, BubbleUPnP). Aplikacja wybiera muzykę z serwera DLNA (NAS, Plex, MinimServer), ESP32 ją odtwarza.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 9
 * DLNA Media Renderer
 * 
 * Biblioteka:
 *   git clone https://github.com/pschatzmann/arduino-dlna.git
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "AudioTools/Communication/AudioHttp.h"
#include "dlna/DLNADeviceAVTransport.h"

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

I2SStream i2s;
MP3DecoderHelix mp3;
WAVDecoder wav;

// Multi-dekoder rozpoznaje format automatycznie
MultiDecoder multiDecoder;

EncodedAudioStream decoderStream(&i2s, &multiDecoder);
URLStream urlStream;
StreamCopy copier(decoderStream, urlStream);

DLNADeviceAVTransport dlna("ESP32_Speaker");  // Nazwa widoczna w sieci

void setup() {
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.printf("WiFi: %s\n", WiFi.localIP().toString().c_str());

    // I2S
    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = 26;
    cfg.pin_ws = 25;
    cfg.pin_data = 22;
    i2s.begin(cfg);

    // Dekodery
    multiDecoder.addDecoder(mp3, "audio/mpeg");
    multiDecoder.addDecoder(wav, "audio/wav");
    decoderStream.begin();

    // DLNA renderer
    dlna.setURLStream(urlStream);
    dlna.setOutput(decoderStream);
    dlna.begin();

    Serial.println("DLNA Renderer 'ESP32_Speaker' gotowy!");
    Serial.println("Otwórz Hi-Fi Cast lub BubbleUPnP na telefonie.");
}

void loop() {
    dlna.loop();
    copier.copy();
}
```

---

### 4.10. Przykład 10: Kompletne radio z interfejsem WWW

Pełne radio internetowe z webowym interfejsem sterowania. ESP32 serwuje stronę HTML na swoim IP — z telefonu lub komputera zmieniasz stacje i głośność.

```cpp
/*
 * ESP32StreamingAudioPlayer — Przykład 10
 * Radio internetowe z interfejsem WWW
 * 
 * ESP32 odtwarza radio I JEDNOCZEŚNIE serwuje stronę kontrolną.
 * Otwórz http://<IP_ESP32>/ w przeglądarce → steruj radiem!
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/Communication/AudioHttp.h"
#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// Stacje
struct Station { const char* name; const char* url; };
Station stations[] = {
    {"PR Trojka",       "http://stream.polskieradio.pl/pr3"},
    {"Radio ZET",       "http://zt01.cdn.eurozet.pl/zet-net.mp3"},
    {"ChilliZET",       "http://n-11-14.dcs.redcdn.pl/sc/o2/Eurozet/live/chillizet.livx"},
    {"Swiss Jazz",      "http://stream.srg-ssr.ch/m/rsj/mp3_128"},
    {"Groove Salad",    "http://ice1.somafm.com/groovesalad-128-mp3"},
    {"DEF CON Radio",   "http://ice1.somafm.com/defcon-128-mp3"},
    {"Dub Step Beyond",  "http://ice1.somafm.com/dubstep-128-mp3"},
    {"Drone Zone",      "http://ice1.somafm.com/dronezone-128-mp3"},
};
const int stationCount = 8;
int currentStation = 0;
float currentVolume = 0.7;
bool isPlaying = true;

// Audio
URLStream url(ssid, password);
I2SStream i2s;
VolumeStream volume(i2s);
MP3DecoderHelix mp3;
EncodedAudioStream decoder(&volume, &mp3);
StreamCopy copier(decoder, url);

// Web serwer (port 80)
WebServer webServer(80);

// Zmiana stacji
void switchStation(int idx) {
    if (idx < 0 || idx >= stationCount) return;
    currentStation = idx;
    url.end();
    decoder.begin();
    url.begin(stations[idx].url, "audio/mp3");
    isPlaying = true;
    Serial.printf("Radio: [%d] %s\n", idx, stations[idx].name);
}

// HTML interfejsu
String buildHTML() {
    String html = R"rawhtml(
<!DOCTYPE html>
<html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Radio</title>
<style>
  body{font-family:system-ui;background:#1a1a2e;color:#eee;margin:0;padding:16px;max-width:480px;margin:auto}
  h1{color:#e94560;text-align:center;font-size:1.5em}
  .station{background:#16213e;padding:12px;margin:6px 0;border-radius:8px;cursor:pointer;border:2px solid transparent;transition:.2s}
  .station:hover{border-color:#e94560}
  .station.active{border-color:#e94560;background:#0f3460}
  .controls{display:flex;gap:10px;justify-content:center;margin:16px 0}
  .btn{background:#e94560;border:none;color:#fff;padding:12px 24px;border-radius:8px;font-size:1.1em;cursor:pointer}
  .btn:hover{background:#c73e54}
  .vol{width:100%;margin:10px 0}
  .info{text-align:center;color:#a0a0a0;font-size:0.85em;margin-top:20px}
</style></head><body>
<h1>📻 ESP32 Radio</h1>
<div class="controls">
  <button class="btn" onclick="fetch('/prev')">⏮</button>
  <button class="btn" onclick="fetch('/toggle')">⏯</button>
  <button class="btn" onclick="fetch('/next')">⏭</button>
</div>
<div style="text-align:center">
  <label>Głośność: <span id="vv">)rawhtml";

    html += String((int)(currentVolume * 100));
    html += R"rawhtml(%</span></label><br>
  <input type="range" class="vol" min="0" max="100" value=")rawhtml";
    html += String((int)(currentVolume * 100));
    html += R"rawhtml(" oninput="document.getElementById('vv').innerText=this.value+'%';fetch('/vol?v='+this.value)">
</div>
<h3>Stacje:</h3>)rawhtml";

    for (int i = 0; i < stationCount; i++) {
        html += "<div class='station";
        if (i == currentStation) html += " active";
        html += "' onclick=\"fetch('/station?i=" + String(i) + "').then(()=>location.reload())\">";
        html += String(i + 1) + ". " + String(stations[i].name);
        html += "</div>";
    }

    html += "<div class='info'>ESP32StreamingAudioPlayer v1.0<br>";
    html += "IP: " + WiFi.localIP().toString() + "</div>";
    html += "</body></html>";
    return html;
}

// Handlery HTTP
void handleRoot()    { webServer.send(200, "text/html", buildHTML()); }
void handleStation() { switchStation(webServer.arg("i").toInt()); webServer.send(200); }
void handleNext()    { switchStation((currentStation + 1) % stationCount); webServer.send(200); }
void handlePrev()    { switchStation((currentStation - 1 + stationCount) % stationCount); webServer.send(200); }
void handleToggle()  {
    if (isPlaying) { url.end(); isPlaying = false; }
    else switchStation(currentStation);
    webServer.send(200);
}
void handleVolume()  {
    currentVolume = webServer.arg("v").toFloat() / 100.0;
    volume.setVolume(currentVolume);
    webServer.send(200);
}

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

    // I2S
    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = 26;
    cfg.pin_ws = 25;
    cfg.pin_data = 22;
    i2s.begin(cfg);

    // Volume
    auto vcfg = volume.defaultConfig();
    vcfg.copyFrom(cfg);
    volume.begin(vcfg);
    volume.setVolume(currentVolume);

    // Dekoder
    decoder.begin();

    // Pierwsza stacja
    url.begin(stations[0].url, "audio/mp3");

    // Web serwer
    webServer.on("/", handleRoot);
    webServer.on("/station", handleStation);
    webServer.on("/next", handleNext);
    webServer.on("/prev", handlePrev);
    webServer.on("/toggle", handleToggle);
    webServer.on("/vol", handleVolume);
    webServer.begin();

    Serial.printf("\n📻 Radio gotowe! Interfejs: http://%s/\n",
                  WiFi.localIP().toString().c_str());
}

void loop() {
    if (isPlaying) copier.copy();
    webServer.handleClient();
}
```

---

## 5. Porównanie protokołów streamingowych

| Protokół | Kierunek | Opóźnienie | Wielu klientów | Kompresja | Zastosowanie |
|----------|----------|-----------|----------------|-----------|-------------|
| HTTP (URLStream) | Odbiór | ~2–5 s (bufor) | — | MP3/AAC/OGG | Radio internetowe |
| HTTP (AudioWAVServer) | Nadawanie | ~0.5–2 s | 1 (lub Ex: wielu) | Brak (WAV) | Monitoring, interkom |
| HTTP (AudioEncoderServer) | Nadawanie | ~1–3 s | Wielu | MP3 | Własna stacja |
| RTSP | Nadawanie | ~0.5–1 s | Wielu | PCM/Opus | Profesjonalny monitoring |
| UDP unicast | Oba | ~10–50 ms | 1 | Brak | Interkom punkt-punkt |
| UDP multicast | Nadawanie | ~10–50 ms | Wielu | Brak | Multiroom (proste) |
| Snapcast | Odbiór | ~20–100 ms | Wielu (sync!) | Opus/FLAC | Multiroom (profesjonalne) |
| DLNA/UPnP | Odbiór | ~2–5 s | — | MP3/WAV/FLAC | Głośnik sieciowy |

---

## 6. Troubleshooting

| Problem | Rozwiązanie |
|---------|-------------|
| **Stream się zacina/przerwy** | Sprawdź siłę WiFi (RSSI > -70 dBm). Użyj HTTP nie HTTPS. Zwiększ bufor: `config.buffer_size = 1024; config.buffer_count = 20;` |
| **Brak połączenia ze stacją** | URL może być nieaktualny — stacje zmieniają adresy. Sprawdź w przeglądarce. Użyj HTTP nie HTTPS (mniej zasobów) |
| **Dźwięk zniekształcony po zmianie stacji** | `decoder.begin()` przed `url.begin()` — reset dekodera czyści stary stan |
| **WebServer nie odpowiada** | `webServer.handleClient()` musi być w loop(). Nie blokuj loop() długimi operacjami |
| **UDP multicast — odbiornik nie słyszy** | Router musi przepuszczać multicast (239.x.x.x). Niektóre tanie routery blokują. Spróbuj unicast na test |
| **Snapcast — brak połączenia z serwerem** | Serwer musi działać na porcie 1704. ESP32 i serwer w tej samej podsieci. Sprawdź `snapserver` logi |
| **DLNA — nie widoczny w aplikacji** | UPnP wymaga multicast SSDP (239.255.255.250:1900). Sprawdź router. Hi-Fi Cast: odśwież listę urządzeń |
| **RTSP — VLC nie łączy** | URL: `rtsp://<IP>:554/audio` (domyślny port 554). Firewall może blokować |
| **Za duże opóźnienie** | UDP < RTSP < HTTP. Dla minimalne opóźnienie użyj UDP unicast z surowym PCM |
| **ESP32 resetuje się** | Za mało RAM — dekodery MP3/AAC potrzebują ~30KB. Zmniejsz bufory lub użyj ESP32-WROVER z PSRAM |

---

## 7. Wydajność i pasmo

| Format | Bitrate | Pasmo WiFi | CPU (1 rdzeń) | Jakość |
|--------|---------|-----------|---------------|--------|
| PCM 16-bit 44.1kHz stereo | 1411 kbps | ~180 KB/s | ~5% | CD |
| PCM 16-bit 16kHz mono | 256 kbps | ~32 KB/s | ~2% | Mowa |
| MP3 128 kbps | 128 kbps | ~16 KB/s | ~20% (dekoder) | Dobre radio |
| MP3 320 kbps | 320 kbps | ~40 KB/s | ~25% | HiFi |
| AAC 64 kbps | 64 kbps | ~8 KB/s | ~15% | Dobre (mała przepustowość) |
| Opus 64 kbps | 64 kbps | ~8 KB/s | ~15% | Bardzo dobre |
| FLAC (lossless) | ~800 kbps | ~100 KB/s | ~30% | Perfekcyjna |

WiFi ESP32 ma przepustowość ~2–5 Mbps w praktyce — wystarczy na kilka równoczesnych strumieni MP3 lub jeden FLAC.

---

## 8. Co dalej

**HLS (HTTP Live Streaming)** — pschatzmann pracuje nad obsługą HLS w audio-tools. To standard używany przez Apple Music, YouTube i większość serwisów. Pozwoli na streaming z adaptacyjnym bitrate.

**ESP-NOW Audio** — protokół Espressif do komunikacji peer-to-peer bez routera WiFi. Zasięg ~200m, opóźnienie ~1ms. Idealny do bezprzewodowego interkomu.

**MQTT + Audio** — sterowanie odtwarzaczem przez MQTT (Home Assistant, Node-RED). Integracja z inteligentnym domem.

**OTA Update** — aktualizacja firmware przez WiFi. Zmiana listy stacji bez konieczności podłączania USB.

---

## 9. Zasoby

| Zasób | URL |
|-------|-----|
| arduino-audio-tools | https://github.com/pschatzmann/arduino-audio-tools |
| arduino-audio-tools Wiki | https://github.com/pschatzmann/arduino-audio-tools/wiki |
| arduino-libhelix (MP3/AAC) | https://github.com/pschatzmann/arduino-libhelix |
| arduino-liblame (MP3 encoder) | https://github.com/pschatzmann/arduino-liblame |
| arduino-snapclient | https://github.com/pschatzmann/arduino-snapclient |
| arduino-dlna | https://github.com/pschatzmann/arduino-dlna |
| Micro-RTSP-Audio | https://github.com/pschatzmann/Micro-RTSP-Audio |
| ESP32-A2DP | https://github.com/pschatzmann/ESP32-A2DP |
| Blog Phila Schatzmanna | https://www.pschatzmann.ch/home/ |
| SomaFM (lista stacji) | https://somafm.com/listen/ |
| Polskie Radio streamy | https://www.polskieradio.pl/ |

---

## 10. Historia zmian

| Wersja | Data | Opis |
|--------|------|------|
| 1.0 | 2026-02-06 | 10 przykładów streamingowych WiFi, pełna dokumentacja. |

---

## 11. Licencja

Projekt open-source do dowolnego użytku. Biblioteki mają własne licencje (sprawdź repozytoria).

> *„ESP32 za 25 zł, mikrofon za 10 zł, DAC za 10 zł — i masz urządzenie, które potrafi być radiem internetowym, głośnikiem sieciowym, serwerem audio, klientem multiroom i interkomem. Wszystko jednocześnie. Na dwóch rdzeniach i 520 KB RAM."*
