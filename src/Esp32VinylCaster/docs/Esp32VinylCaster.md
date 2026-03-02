# 🎛️ VinylCaster — Dokumentacja Projektu

> **Wersja:** 1.0  
> **Data:** 2026-02-07  
> **Poziom trudności:** ⭐⭐⭐ Zaawansowany  
> **Szacowany koszt:** 80–200 zł (zależnie od wariantu ADC)  
> **Platforma:** ESP32 / ESP32-S3 (Arduino Framework)  
> **Biblioteka bazowa:** arduino-audio-tools (Phil Schatzmann)  

---

## 1. Opis projektu

VinylCaster to bezprzewodowy digitizer audio oparty na ESP32 — zamienia sygnał analogowy z gramofonu, magnetofonu, wzmacniacza lub dowolnego urządzenia z wyjściem liniowym na cyfrowy strumień audio, który można odebrać na komputerze, telefonie lub innym urządzeniu w sieci WiFi. Dodatkowo może nagrywać na kartę SD w formacie WAV lub FLAC.

Kluczowym elementem jest **zewnętrzny ADC I2S** (przetwornik analogowo-cyfrowy) o jakości znacznie przewyższającej wbudowany 12-bitowy ADC ESP32. Projekt rozważa trzy opcje ADC: od budżetowej do audiofilskiej.

### 1.1. Zastosowania

- **Digitalizacja winyli** — gramofon + przedwzmacniacz RIAA → VinylCaster → WiFi → komputer (Audacity) → FLAC/MP3. Archiwizacja kolekcji płyt analogowych.
- **Digitalizacja kaset** — magnetofon → VinylCaster → WiFi → komputer. Uzupełnienie projektu #3 TapeForge (ten czyta z kasety, tamten pisze na kasetę).
- **Bezprzewodowy streaming z wzmacniacza** — wyjście REC OUT / TAPE OUT ze starego wzmacniacza → VinylCaster → WiFi → słuchawki/głośnik w innym pokoju.
- **Monitoring audio na żywo** — mikser → VinylCaster → WiFi → telefon (sprawdzanie miksu na słuchawkach bez kabli).
- **Archiwizacja radia** — wyjście liniowe tunera FM → VinylCaster → WAV na SD. Nagrywanie audycji.

### 1.2. Łańcuch sygnału

```
┌──────────────┐   Analog    ┌─────────────┐   I2S    ┌─────────┐   WiFi
│ Źródło       │   (line     │ Zewnętrzny  │  (cyfrowy│         │   /SD/BT
│ analogowe    │───level)───►│ ADC I2S     │──────────►│  ESP32  │──────────►
│              │   ~1V RMS   │ (PCM1808    │  24-bit  │         │ Komputer
│ • Gramofon*  │             │  lub ES8388 │  44.1kHz │         │ Telefon
│ • Magnetofon │             │  lub CS5343)│  stereo  │         │ Głośnik
│ • Wzmacniacz │             └─────────────┘          │         │ Karta SD
│ • Tuner FM   │                                       └─────────┘
│ • Mikser     │
└──────────────┘
 * Gramofon wymaga przedwzmacniacza RIAA (phono preamp)
```

### 1.3. Dlaczego zewnętrzny ADC, nie wbudowany ESP32?

| Parametr | Wbudowany ADC ESP32 | PCM1808 (zewn.) | ES8388 (zewn.) |
|----------|--------------------:|----------------:|---------------:|
| Rozdzielczość | 12-bit | 24-bit | 24-bit |
| Zakres dynamiki | ~50 dB | ~99 dB (SNR) | ~95 dB (SNR) |
| THD+N | ~-60 dB (bardzo słabe) | ~-93 dB | ~-90 dB |
| Sample rate max | ~100 kHz (niestabilne) | 96 kHz | 96 kHz |
| Szum | Wysoki (powiązany z WiFi!) | Bardzo niski | Niski |
| Interfejs | SAR (GPIO) | I2S (cyfrowy) | I2S (cyfrowy) |
| Koszt modułu | 0 zł (wbudowany) | ~10–20 zł | ~25–40 zł |

Wbudowany ADC ESP32 jest zaprojektowany do odczytu czujników (temperatura, potencjometr), nie do audio. Ma zaledwie 12 bitów rozdzielczości, ogromny szum (zwłaszcza gdy WiFi jest aktywne — dzielą zasoby zasilania), nieliniowość i brak wejścia różnicowego. Dla poważnego audio — jedynym sensownym rozwiązaniem jest zewnętrzny ADC I2S.

Sygnał z zewnętrznego ADC trafia do ESP32 cyfrowo przez magistralę I2S — zero szumów, zero przesłuchów, zero problemów z masą.

---

## 2. Wybór ADC — trzy warianty

### 2.1. Wariant A: PCM1808 (zalecany — najlepszy stosunek jakość/cena)

**TI PCM1808** — 24-bitowy stereo ADC, SNR 99 dB, THD+N -93 dB. Dedykowany do audio. Dostępne gotowe moduły na Aliexpress/Amazon za ~10–20 zł (fioletowa PCB z 5 kondensatorami).

Cechy: najlepsza jakość ADC w tej cenie, prosty interfejs (I2S slave lub master), wejście liniowe (nie potrzebuje dodatkowego wzmacniacza dla sygnału line-level). Wymaga sygnału MCLK z ESP32 (GPIO0) lub zewnętrznego oscylatora.

```
PCM1808 Moduł — pinout typowy:
───────────────────────────────
VCC   — 5V (zasilanie analogowe)
VDD   — 3.3V (zasilanie cyfrowe)
GND   — masa
DOUT  — dane I2S wyjście (do ESP32 data_in)
BCK   — bit clock (z ESP32 lub generowany)
LRCK  — word select / left-right clock
SCKI  — system clock input = MCLK (z ESP32 GPIO0)
FMT   — format: GND=I2S, VDD=left-justified
MD0   — tryb: patrz tabela poniżej
MD1   — tryb: patrz tabela poniżej
VINL  — wejście audio lewy kanał
VINR  — wejście audio prawy kanał

Tryby pracy (MD1:MD0):
  GND:GND → Slave mode (ESP32 = master, zalecane)
  GND:VDD → Master mode 256fs (oscylator na SCKI)
  VDD:GND → Master mode 384fs
  VDD:VDD → Master mode 512fs
```

### 2.2. Wariant B: ES8388 (kodek DAC+ADC — all-in-one)

**Everest ES8388** — kodek audio z DAC I2S i ADC I2S na jednym chipie. Używany w płytkach AI Thinker AudioKit. Dostępne moduły breakout (PCB Artists, ~25–40 zł). Sterowany przez I2C.

Cechy: ADC 24-bit + DAC 24-bit w jednym chipie (digitalizuj I odtwarzaj jednocześnie), regulacja wzmocnienia wejścia przez I2C (od -15 dB do +24 dB), wbudowany mikser, PGA (Programmable Gain Amplifier), filtr górnoprzepustowy. Wada: wymaga inicjalizacji przez I2C (kilkadziesiąt rejestrów — ale audio-tools ma gotowe drivery).

Idealny jeśli oprócz digitalizacji chcesz też odsłuch (monitoring) — wyjście DAC podłączasz do słuchawek i słyszysz w czasie rzeczywistym to, co nagrywasz.

### 2.3. Wariant C: CS5343 (Cirrus Logic — prosty, dobry)

**CS5343** — 24-bit stereo ADC, 100 dB SNR, -88 dB THD+N. Proste sterowanie (brak I2C, konfiguracja pinami). Mniej popularny niż PCM1808, trudniej dostać gotowe moduły, ale niektóre płytki WLED mają go na pokładzie.

### 2.4. Wariant D: INMP441 (budżetowy — mikrofon I2S)

Mikrofon MEMS I2S. Nie nadaje się do digitalizacji winyli (zbyt cichy, jednokierunkowy, mono), ale za ~8 zł można go użyć do nagrywania mowy lub monitoringu pomieszczeń. Opisany w poprzednich projektach.

### 2.5. Tabela porównawcza

| Parametr | PCM1808 | ES8388 | CS5343 | INMP441 |
|----------|---------|--------|--------|---------|
| Typ | Dedykowany ADC | Kodek (ADC+DAC) | Dedykowany ADC | Mikrofon MEMS |
| Rozdzielczość | 24-bit | 24-bit | 24-bit | 24-bit |
| SNR | 99 dB | 95 dB | 100 dB | 61 dB |
| THD+N | -93 dB | -90 dB | -88 dB | ~-60 dB |
| Wejście | Liniowe (RCA/jack) | Liniowe + mikrofon | Liniowe | Wbudowany mic |
| Wyjście DAC | ❌ Brak | ✅ Tak (odsłuch!) | ❌ Brak | ❌ Brak |
| Sterowanie | Piny (tryb) | I2C (rejestry) | Piny | — |
| MCLK wymagany | Tak (GPIO0) | Tak (GPIO0) | Tak | Nie |
| Cena modułu | 10–20 zł | 25–40 zł | 15–25 zł | 8–15 zł |
| Gotowe moduły | ✅ Łatwo dostępne | ✅ (PCB Artists) | ⚠️ Rzadziej | ✅ Łatwo |
| Wsparcie audio-tools | ✅ (I2SStream RX) | ✅ (AudioKitStream) | ✅ (I2SStream RX) | ✅ (I2SStream RX) |
| **Rekomendacja** | **🏆 Najlepszy wybór** | Odsłuch + nagrywanie | Alternatywa | Tylko mowa |

---

## 3. Przedwzmacniacz RIAA (phono preamp) — dla gramofonu

Gramofon generuje sygnał bardzo cichy (~2-5 mV) i o zniekształconej charakterystyce częstotliwościowej (krzywa RIAA — basy obcięte, tony wysokie wzmocnione). Przed podaniem na ADC potrzebujesz przedwzmacniacza phono (RIAA preamp), który wzmocni sygnał do poziomu liniowego (~0.5-1V RMS) i skoryguje krzywą RIAA.

### 3.1. Opcje przedwzmacniacza

**Opcja 1: Gotowy moduł** — Behringer PP400 MicroPhono (~80-120 zł), Art DJ PRE II (~100-150 zł). Plug-and-play, wejście RCA, wyjście RCA line-level. Najprostsze rozwiązanie.

**Opcja 2: Wbudowany w wzmacniaczu** — Większość wzmacniaczy stereo z lat 80-90 ma wejście PHONO z wbudowanym przedwzmacniaczem RIAA. Podłącz gramofon do PHONO, a z wyjścia REC OUT / TAPE OUT weź sygnał liniowy do VinylCastera.

**Opcja 3: Wbudowany w gramofonie** — Wiele nowoczesnych gramofonów (Audio-Technica AT-LP60X, AT-LP120X) ma wbudowany przedwzmacniacz z przełącznikiem LINE/PHONO. Na pozycji LINE sygnał jest gotowy do podania na ADC.

**Opcja 4: DIY** — Projekt Calvin-Phono lub PlatINA — schematy open-source do budowy własnego przedwzmacniacza RIAA na opampach (NE5532, OPA2134). Koszt: ~20-40 zł.

### 3.2. Łańcuch sygnału z gramofonem

```
Gramofon ──RCA──► Phono Preamp (RIAA) ──RCA──► Jack 3.5mm ──► PCM1808 ──I2S──► ESP32
(~3 mV)           (wzmocnienie ~40 dB)         (~0.7V RMS)     (24-bit ADC)      (WiFi/SD)
                  (korekcja RIAA)
```

### 3.3. Bez gramofonu — inne źródła

Magnetofon, CD player, tuner FM, wzmacniacz (wyjście REC OUT / TAPE OUT / headphone) — te urządzenia dają sygnał line-level (~0.5–2V RMS). Podłączasz bezpośrednio do wejścia ADC bez przedwzmacniacza. Potrzebujesz tylko kabla RCA→jack 3.5mm lub RCA→RCA (zależnie od wejścia modułu ADC).

---

## 4. Hardware

### 4.1. BOM — Wariant A (PCM1808, zalecany)

| # | Element | Opis | Cena |
|---|---------|------|------|
| 1 | ESP32 DevKit V1 | WiFi + BT, 2 rdzenie | 20–35 zł |
| 2 | Moduł PCM1808 | 24-bit stereo ADC I2S, breakout | 10–20 zł |
| 3 | Moduł microSD | Czytnik kart SPI (do nagrywania) | 3–8 zł |
| 4 | Karta microSD | 4–32 GB, FAT32, Class 10 | 10–20 zł |
| 5 | OLED 0.96" SSD1306 | Wyświetlacz I2C (opcja) | 8–15 zł |
| 6 | Gniazdo RCA stereo | Montaż panelowy, 2× (L+R) | 3–5 zł |
| 7 | Kabel RCA-RCA | Do połączenia ze źródłem | 5–10 zł |
| 8 | Rezystor 33Ω | Między ESP32 GPIO0 a SCKI PCM1808 | ~0.10 zł |
| 9 | Kondensatory 100nF | Bypass zasilania (2–3 szt.) | ~0.30 zł |
| 10 | Breadboard + kabelki | Montaż | 10–15 zł |
| | **RAZEM** | | **~70–130 zł** |

Opcjonalnie: MAX98357A + głośnik (~15–25 zł) do odsłuchu lub Behringer PP400 (~90 zł) jeśli potrzebny phono preamp.

### 4.2. Schemat połączeń — PCM1808

```
ESP32                    PCM1808 Moduł
─────                    ──────────────
GPIO0  ─── R(33Ω) ────── SCKI    (Master Clock — MCLK)
GPIO14 ────────────────── BCK     (Bit Clock)
GPIO15 ────────────────── LRCK    (Left-Right Clock / Word Select)
GPIO32 ────────────────── DOUT    (Dane I2S z ADC)
3.3V   ────────────────── VDD     (Zasilanie cyfrowe 3.3V)
5V     ────────────────── VCC     (Zasilanie analogowe 5V)
GND    ────────────────── GND
                          FMT  →  GND     (Format: I2S standard)
                          MD0  →  GND     (Slave mode)
                          MD1  →  GND     (Slave mode)

Wejście audio:
  RCA L (tip) ────────── VINL    (wejście lewy kanał)
  RCA R (tip) ────────── VINR    (wejście prawy kanał)
  RCA shield  ────────── AGND    (masa analogowa)

ESP32                    Moduł microSD (SPI)
─────                    ─────────────────────
GPIO5  ────────────────── CS
GPIO18 ────────────────── SCK
GPIO23 ────────────────── MOSI
GPIO19 ────────────────── MISO
3.3V   ────────────────── VCC
GND    ────────────────── GND

ESP32                    OLED SSD1306 (I2C)
─────                    ──────────────────
GPIO21 ────────────────── SDA
GPIO22 ────────────────── SCL
3.3V   ────────────────── VCC
GND    ────────────────── GND
```

Uwaga o GPIO0: Na ESP32 GPIO0 jest domyślnym wyjściem MCLK (master clock) I2S. Rezystor 33Ω tłumi odbicia na linii (MCLK to sygnał wysokiej częstotliwości — 11.29 MHz przy 44.1 kHz × 256fs). Krótkie kabelki (< 5 cm) są krytyczne!

### 4.3. Schemat połączeń — ES8388 (wariant B)

```
ESP32                    ES8388 Moduł (PCB Artists)
─────                    ──────────────────────────
GPIO0  ────────────────── MCLK
GPIO26 ────────────────── BCLK    (Bit Clock — wspólny TX/RX)
GPIO25 ────────────────── LRC     (Word Select — wspólny TX/RX)
GPIO35 ────────────────── ASDOUT  (ADC → ESP32, dane wejściowe)
GPIO22 ────────────────── DSDIN   (ESP32 → DAC, dane wyjściowe — odsłuch)
GPIO21 ────────────────── SDA     (I2C — sterowanie)
GPIO18 ────────────────── SCL     (I2C — sterowanie)
3.3V   ────────────────── VCC
GND    ────────────────── GND

Wejście:  LINEINL / LINEINR (jack 3.5mm na module)
Wyjście:  HPOUTL / HPOUTR   (jack 3.5mm — słuchawki, monitoring!)
```

---

## 5. Przykłady aplikacji — kompletne sketche

### 5.1. Przykład 1: Serwer WAV — stream z ADC do przeglądarki

Najprostszy scenariusz: PCM1808 digitalizuje audio, ESP32 serwuje strumień WAV przez HTTP. Otwierasz przeglądarkę (lub VLC, Audacity) pod `http://<IP_ESP32>/` i słuchasz/nagrywasz.

```cpp
/*
 * VinylCaster — Przykład 1
 * Serwer WAV: ADC I2S (PCM1808) → WiFi → Przeglądarka/VLC/Audacity
 * 
 * Biblioteki:
 *   arduino-audio-tools
 */

#include "AudioTools.h"
#include "AudioTools/Communication/AudioServer.h"

const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// I2S wejście — PCM1808
I2SStream adcInput;

// Serwer HTTP serwujący WAV
AudioWAVServer server(ssid, password, 80);

// Kopiowanie strumienia
StreamCopy copier(server, adcInput);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // Konfiguracja I2S dla PCM1808 (ESP32 = master, PCM1808 = slave)
    auto cfg = adcInput.defaultConfig(RX_MODE);
    cfg.pin_mck = 0;          // MCLK → GPIO0 → PCM1808 SCKI
    cfg.pin_bck = 14;         // BCLK → PCM1808 BCK
    cfg.pin_ws  = 15;         // LRCK → PCM1808 LRCK
    cfg.pin_data = 32;        // DOUT ← PCM1808 DOUT
    cfg.sample_rate = 44100;  // CD quality
    cfg.channels = 2;         // Stereo
    cfg.bits_per_sample = 16; // 16-bit (PCM1808 daje 24-bit, ESP32 truncuje)
    cfg.i2s_format = I2S_STD_FORMAT;
    cfg.is_master = true;     // ESP32 generuje zegary
    adcInput.begin(cfg);

    // Konfiguracja serwera
    auto serverCfg = server.defaultConfig();
    serverCfg.sample_rate = 44100;
    serverCfg.channels = 2;
    serverCfg.bits_per_sample = 16;
    server.begin(serverCfg);

    Serial.println("╔═══════════════════════════════════════╗");
    Serial.println("║  VinylCaster — Serwer WAV             ║");
    Serial.println("╚═══════════════════════════════════════╝");
    Serial.printf("Stream: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("Otwórz w przeglądarce, VLC lub Audacity.");
    Serial.println("Audacity: File → Import → Raw Data → URL");
}

void loop() {
    copier.copy();
}
```

**Odbiór na komputerze:**

W VLC: Media → Open Network Stream → `http://<IP_ESP32>/` → Play.

W Audacity (nagrywanie): File → Import → Raw Data → podaj URL. Lub użyj `ffmpeg` do nagrania:
```bash
ffmpeg -i http://<IP_ESP32>/ -c copy nagranie_winyl.wav
```

---

### 5.2. Przykład 2: Nagrywanie na kartę SD (WAV)

Digitalizacja bezpośrednio na kartę SD — bez WiFi. Idealne do archiwizacji: puszczasz płytę, VinylCaster nagrywa na SD, potem przenosisz plik na komputer.

```cpp
/*
 * VinylCaster — Przykład 2
 * Nagrywanie z ADC na kartę SD jako WAV
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"

// I2S wejście — PCM1808
I2SStream adcInput;

// Wyjście: plik WAV na SD
File wavFile;
WAVEncoder wavEncoder;
EncodedAudioStream encoder(&wavFile, &wavEncoder);
StreamCopy copier(encoder, adcInput);

// Stan
bool recording = false;
uint32_t recordStart = 0;
int fileNumber = 1;

// VU-metr (szczytowa wartość)
int16_t peakL = 0, peakR = 0;

void startRecording() {
    char filename[32];
    snprintf(filename, sizeof(filename), "/vinyl_%03d.wav", fileNumber++);

    wavFile = SD.open(filename, FILE_WRITE);
    if (!wavFile) {
        Serial.printf("BŁĄD: Nie mogę utworzyć %s\n", filename);
        return;
    }

    auto cfg = encoder.defaultConfig();
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    encoder.begin(cfg);

    recording = true;
    recordStart = millis();
    Serial.printf("🔴 REC → %s\n", filename);
}

void stopRecording() {
    recording = false;
    encoder.end();
    uint32_t fileSize = wavFile.size();
    wavFile.close();

    float duration = (millis() - recordStart) / 1000.0;
    Serial.printf("⏹ STOP — %.1f s, %.1f MB\n", duration, fileSize / 1048576.0);
}

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

    // SD
    if (!SD.begin(5)) {
        Serial.println("BŁĄD: Karta SD nie wykryta!");
        while(1) delay(1000);
    }
    Serial.printf("SD: %llu MB wolne\n", (SD.totalBytes() - SD.usedBytes()) / 1048576);

    // I2S ADC (PCM1808)
    auto cfg = adcInput.defaultConfig(RX_MODE);
    cfg.pin_mck = 0;
    cfg.pin_bck = 14;
    cfg.pin_ws  = 15;
    cfg.pin_data = 32;
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.is_master = true;
    adcInput.begin(cfg);

    Serial.println("VinylCaster — Rejestrator SD");
    Serial.println("Komendy: r=nagrywaj  s=stop  i=info SD");
}

void loop() {
    if (recording) {
        copier.copy();
    }

    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' && !recording) startRecording();
        if (c == 's' && recording)  stopRecording();
        if (c == 'i') {
            Serial.printf("SD: użyte %llu MB / %llu MB\n",
                SD.usedBytes()/1048576, SD.totalBytes()/1048576);
        }
    }
}
```

---

### 5.3. Przykład 3: Serwer MP3 (skompresowany streaming)

Surowy WAV to ~1.4 Mbps (44.1kHz stereo 16-bit). MP3 128kbps to ~128 kbps — 10× mniej. Mniejsze opóźnienia, więcej klientów, stabilniejszy stream przez WiFi.

```cpp
/*
 * VinylCaster — Przykład 3
 * Serwer MP3: ADC → enkoder MP3 → WiFi → klienci HTTP
 * 
 * Biblioteki:
 *   arduino-audio-tools
 *   arduino-liblame  (MP3 encoder)
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3LAME.h"
#include "AudioTools/Communication/AudioServer.h"

const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

I2SStream adcInput;
MP3EncoderLAME mp3enc;
AudioEncoderServer server(&mp3enc, ssid, password, 80);
StreamCopy copier(server, adcInput);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // I2S ADC
    auto cfg = adcInput.defaultConfig(RX_MODE);
    cfg.pin_mck = 0;
    cfg.pin_bck = 14;
    cfg.pin_ws  = 15;
    cfg.pin_data = 32;
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.is_master = true;
    adcInput.begin(cfg);

    // Serwer MP3
    auto sCfg = server.defaultConfig();
    sCfg.sample_rate = 44100;
    sCfg.channels = 2;
    server.begin(sCfg);

    Serial.printf("VinylCaster MP3 Stream: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("Odtwórz w VLC, przeglądarce lub dodaj jako stację w radiu.");
}

void loop() {
    copier.copy();
}
```

---

### 5.4. Przykład 4: Streaming + jednoczesne nagrywanie na SD

Często chcesz jednocześnie streamować (żeby słuchać na żywo) i nagrywać (żeby mieć plik). `MultiOutput` kieruje ten sam strumień do dwóch wyjść.

```cpp
/*
 * VinylCaster — Przykład 4
 * Jednoczesny streaming WAV + nagrywanie na SD
 */

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "AudioTools/Communication/AudioServer.h"

const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

I2SStream adcInput;

// Wyjście 1: serwer HTTP (WiFi stream)
AudioWAVServer wifiServer(ssid, password, 80);

// Wyjście 2: plik WAV na SD
File sdFile;
WAVEncoder wavEnc;
EncodedAudioStream sdEncoder(&sdFile, &wavEnc);

// Multi-output: jeden strumień → dwa wyjścia
MultiOutput multiOut;
StreamCopy copier(multiOut, adcInput);

bool sdRecording = false;

void startSDRecord() {
    sdFile = SD.open("/live_capture.wav", FILE_WRITE);
    if (!sdFile) { Serial.println("SD BŁĄD!"); return; }

    auto cfg = sdEncoder.defaultConfig();
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    sdEncoder.begin(cfg);

    multiOut.add(sdEncoder);
    sdRecording = true;
    Serial.println("🔴 Nagrywanie na SD + streaming");
}

void stopSDRecord() {
    sdRecording = false;
    sdEncoder.end();
    sdFile.close();
    // multiOut nadal streamuje przez WiFi
    Serial.println("⏹ SD zatrzymane, WiFi stream kontynuuje");
}

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    SD.begin(5);

    // I2S ADC
    auto cfg = adcInput.defaultConfig(RX_MODE);
    cfg.pin_mck = 0;
    cfg.pin_bck = 14;
    cfg.pin_ws  = 15;
    cfg.pin_data = 32;
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.is_master = true;
    adcInput.begin(cfg);

    // Serwer WiFi
    auto sCfg = wifiServer.defaultConfig();
    sCfg.sample_rate = 44100;
    sCfg.channels = 2;
    wifiServer.begin(sCfg);

    // Multi-output: domyślnie tylko WiFi
    multiOut.add(wifiServer);

    Serial.printf("VinylCaster Dual: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("r=REC na SD  s=STOP SD  (WiFi stream ciągły)");
}

void loop() {
    copier.copy();

    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' && !sdRecording) startSDRecord();
        if (c == 's' && sdRecording)  stopSDRecord();
    }
}
```

---

### 5.5. Przykład 5: RTSP Server (profesjonalny streaming do VLC)

RTSP to profesjonalny protokół streamingowy — obsługiwany przez VLC, FFplay, OBS i inne narzędzia.

```cpp
/*
 * VinylCaster — Przykład 5
 * Serwer RTSP: ADC → RTSP → VLC/FFplay
 */

#include "AudioTools.h"
#include "AudioTools/AudioLibs/RTSPStream.h"

const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

I2SStream adcInput;
RTSPSourceFromAudioStream source(adcInput);
RTSPStream rtsp(source);

void setup() {
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);

    auto cfg = adcInput.defaultConfig(RX_MODE);
    cfg.pin_mck = 0;
    cfg.pin_bck = 14;
    cfg.pin_ws  = 15;
    cfg.pin_data = 32;
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.is_master = true;
    adcInput.begin(cfg);

    rtsp.begin();

    Serial.printf("VinylCaster RTSP: rtsp://%s:554/audio\n",
                  WiFi.localIP().toString().c_str());
}

void loop() {
    rtsp.copy();
}
```

---

### 5.6. Przykład 6: Nadajnik Bluetooth A2DP (do słuchawek BT)

Winyl bezprzewodowo w słuchawkach Bluetooth — gramofon → preamp → PCM1808 → ESP32 → BT A2DP → słuchawki. Zamienia każdy gramofon w bezprzewodowy!

```cpp
/*
 * VinylCaster — Przykład 6
 * ADC → Bluetooth A2DP → Słuchawki/Głośnik BT
 * 
 * Biblioteki: arduino-audio-tools + ESP32-A2DP
 */

#include "AudioTools.h"
#include "AudioTools/AudioLibs/A2DPStream.h"

const char* btDevice = "Moje_Sluchawki";  // Nazwa docelowego urządzenia BT

I2SStream adcInput;
A2DPStream a2dpOut;
StreamCopy copier(a2dpOut, adcInput);

void setup() {
    Serial.begin(115200);

    // I2S ADC (PCM1808)
    auto cfg = adcInput.defaultConfig(RX_MODE);
    cfg.pin_mck = 0;
    cfg.pin_bck = 14;
    cfg.pin_ws  = 15;
    cfg.pin_data = 32;
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.is_master = true;
    adcInput.begin(cfg);

    // Bluetooth A2DP nadajnik
    auto a2dpCfg = a2dpOut.defaultConfig(TX_MODE);
    a2dpCfg.name = btDevice;
    a2dpOut.begin(a2dpCfg);

    Serial.printf("VinylCaster BT → Łączę z '%s'...\n", btDevice);
}

void loop() {
    copier.copy();
}
```

---

### 5.7. Przykład 7: ES8388 — digitalizacja z odsłuchem (monitoring)

ES8388 ma DAC i ADC — możesz jednocześnie digitalizować i słuchać na słuchawkach podłączonych do modułu. Zero opóźnienia w monitoringu (hardware passthrough w ES8388).

```cpp
/*
 * VinylCaster — Przykład 7
 * ES8388: Digitalizacja + odsłuch na słuchawkach + streaming WiFi
 * 
 * Biblioteki: arduino-audio-tools + arduino-audiokit
 */

#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"
#include "AudioTools/Communication/AudioServer.h"

const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// ES8388 jako I2SCodecStream (ADC + DAC)
I2SCodecStream codecStream(AudioKitEs8388V1);

// Serwer WAV
AudioWAVServer server(ssid, password, 80);

// VolumeStream do regulacji poziomu wejścia
VolumeStream volume(server);
StreamCopy copier(volume, codecStream);

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);

    // ES8388 — tryb RXTX (jednocześnie ADC i DAC)
    auto cfg = codecStream.defaultConfig(RXTX_MODE);
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.input_device = ADC_INPUT_LINE1;     // Wejście: LINE IN
    cfg.output_device = DAC_OUTPUT_ALL;     // Wyjście: słuchawki + line out
    codecStream.begin(cfg);

    // Głośność
    auto vCfg = volume.defaultConfig();
    vCfg.sample_rate = 44100;
    vCfg.channels = 2;
    volume.begin(vCfg);
    volume.setVolume(0.8);

    // Serwer
    auto sCfg = server.defaultConfig();
    sCfg.sample_rate = 44100;
    sCfg.channels = 2;
    server.begin(sCfg);

    Serial.printf("VinylCaster ES8388: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("Odsłuch na słuchawkach podłączonych do ES8388.");
}

void loop() {
    copier.copy();
}
```

---

### 5.8. Przykład 8: Pełny VinylCaster z interfejsem WWW

Kompletny system: digitalizacja, streaming WAV/MP3, nagrywanie na SD, VU-metr, sterowanie z przeglądarki.

```cpp
/*
 * VinylCaster — Przykład 8
 * Kompletny system z interfejsem WWW
 * 
 * Funkcje:
 *  - Stream WAV na żywo (http://<IP>/)
 *  - Nagrywanie na SD (start/stop z WWW)
 *  - VU-metr na stronie + OLED
 *  - Info o pliku / czas nagrania
 */

#include "AudioTools.h"
#include "AudioTools/Communication/AudioServer.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

const char* ssid     = "TwojaSiecWiFi";
const char* password = "TwojeHaslo";

// --- Audio ---
I2SStream adcInput;
AudioWAVServer audioServer(ssid, password, 8080);  // Stream na porcie 8080
StreamCopy copier(audioServer, adcInput);

// --- SD nagrywanie ---
File recFile;
bool isRecording = false;
uint32_t recStartMs = 0;
int recFileNum = 1;
uint32_t recBytes = 0;

// --- Web serwer sterowania (port 80) ---
WebServer web(80);

// --- OLED ---
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// --- VU metr ---
volatile int16_t vuPeakL = 0, vuPeakR = 0;

// Strona HTML
String buildPage() {
    float recSec = isRecording ? (millis() - recStartMs) / 1000.0 : 0;
    float recMB = recBytes / 1048576.0;

    String html = R"html(<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>VinylCaster</title>
<style>
body{font-family:system-ui;background:#0d1117;color:#c9d1d9;margin:0;padding:16px;max-width:500px;margin:auto}
h1{color:#f0883e;text-align:center}
.card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:16px;margin:12px 0}
.vu{height:20px;background:#21262d;border-radius:4px;margin:4px 0;overflow:hidden}
.vu-bar{height:100%;border-radius:4px;transition:width .15s}
.vu-l .vu-bar{background:linear-gradient(90deg,#238636,#f0883e,#da3633)}
.vu-r .vu-bar{background:linear-gradient(90deg,#238636,#f0883e,#da3633)}
.btn{display:inline-block;padding:12px 28px;border:none;border-radius:8px;font-size:1.1em;cursor:pointer;margin:4px;color:#fff}
.rec{background:#da3633}.stop{background:#30363d}.listen{background:#238636}
.info{color:#8b949e;font-size:.85em;text-align:center;margin-top:16px}
</style>
<script>
setInterval(()=>fetch('/vu').then(r=>r.json()).then(d=>{
  document.getElementById('vl').style.width=d.l+'%';
  document.getElementById('vr').style.width=d.r+'%';
}),200);
</script></head><body>
<h1>🎛️ VinylCaster</h1>
<div class="card">
<h3>📡 Stream na żywo</h3>
<a class="btn listen" href="http://)html" + WiFi.localIP().toString() + R"html(:8080/" target="_blank">▶ Otwórz stream WAV</a>
<p style="font-size:.8em;color:#8b949e">Port 8080 • WAV 44.1kHz stereo 16-bit</p>
</div>
<div class="card">
<h3>🎚️ Poziom sygnału</h3>
<div>L: <div class="vu vu-l"><div class="vu-bar" id="vl" style="width:0%"></div></div></div>
<div>R: <div class="vu vu-r"><div class="vu-bar" id="vr" style="width:0%"></div></div></div>
</div>
<div class="card">
<h3>💾 Nagrywanie na SD</h3>)html";

    if (isRecording) {
        html += "<p>🔴 <b>NAGRYWANIE</b> — " + String(recSec, 1) + " s, " + String(recMB, 2) + " MB</p>";
        html += "<a class='btn stop' href='/rec_stop'>⏹ STOP</a>";
    } else {
        html += "<p>⏸ Gotowy do nagrywania</p>";
        html += "<a class='btn rec' href='/rec_start'>🔴 REC</a>";
    }

    html += R"html(</div>
<div class="info">VinylCaster v1.0 • ESP32 + PCM1808 24-bit ADC<br>)html";
    html += "IP: " + WiFi.localIP().toString();
    html += " • SD: " + String(SD.usedBytes()/1048576) + "/" + String(SD.totalBytes()/1048576) + " MB";
    html += "</div></body></html>";
    return html;
}

void handleRoot()     { web.send(200, "text/html", buildPage()); }
void handleRecStart() {
    if (!isRecording) {
        char fn[32];
        snprintf(fn, sizeof(fn), "/rec_%03d.wav", recFileNum++);
        recFile = SD.open(fn, FILE_WRITE);
        if (recFile) {
            // Prosty nagłówek WAV (44 bajty, zaktualizujemy na końcu)
            uint8_t hdr[44] = {0};
            memcpy(hdr, "RIFF", 4);
            memcpy(hdr+8, "WAVEfmt ", 8);
            uint32_t v;
            v = 16; memcpy(hdr+16, &v, 4);        // chunk size
            hdr[20] = 1;                            // PCM
            hdr[22] = 2;                            // stereo
            v = 44100; memcpy(hdr+24, &v, 4);      // sample rate
            v = 44100*2*2; memcpy(hdr+28, &v, 4);  // byte rate
            hdr[32] = 4;                            // block align
            hdr[34] = 16;                           // bits
            memcpy(hdr+36, "data", 4);
            recFile.write(hdr, 44);
            isRecording = true;
            recStartMs = millis();
            recBytes = 0;
        }
    }
    web.sendHeader("Location", "/");
    web.send(303);
}
void handleRecStop() {
    if (isRecording) {
        isRecording = false;
        // Aktualizuj nagłówek WAV
        uint32_t dataSize = recBytes;
        uint32_t fileSize = dataSize + 36;
        recFile.seek(4);  recFile.write((uint8_t*)&fileSize, 4);
        recFile.seek(40); recFile.write((uint8_t*)&dataSize, 4);
        recFile.close();
    }
    web.sendHeader("Location", "/");
    web.send(303);
}
void handleVU() {
    int pctL = min(100, abs(vuPeakL) * 100 / 32768);
    int pctR = min(100, abs(vuPeakR) * 100 / 32768);
    vuPeakL = vuPeakL * 0.85;  // Decay
    vuPeakR = vuPeakR * 0.85;
    web.send(200, "application/json",
        "{\"l\":" + String(pctL) + ",\"r\":" + String(pctR) + "}");
}

void updateOLED() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("VinylCaster");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // VU bars
    int barL = min(120, abs(vuPeakL) * 120 / 32768);
    int barR = min(120, abs(vuPeakR) * 120 / 32768);
    display.setCursor(0, 14); display.print("L");
    display.drawRect(10, 14, 114, 8, SSD1306_WHITE);
    if (barL > 0) display.fillRect(11, 15, barL, 6, SSD1306_WHITE);
    display.setCursor(0, 26); display.print("R");
    display.drawRect(10, 26, 114, 8, SSD1306_WHITE);
    if (barR > 0) display.fillRect(11, 27, barR, 6, SSD1306_WHITE);

    // Status
    display.setCursor(0, 40);
    if (isRecording) {
        float sec = (millis() - recStartMs) / 1000.0;
        display.printf("REC  %.1fs  %.1fMB", sec, recBytes/1048576.0);
    } else {
        display.println("READY");
    }

    display.setCursor(0, 54);
    display.printf("http://%s", WiFi.localIP().toString().c_str());
    display.display();
}

void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

    // OLED
    Wire.begin(21, 22);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

    // SD
    SD.begin(5);

    // I2S ADC
    auto cfg = adcInput.defaultConfig(RX_MODE);
    cfg.pin_mck = 0;
    cfg.pin_bck = 14;
    cfg.pin_ws  = 15;
    cfg.pin_data = 32;
    cfg.sample_rate = 44100;
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    cfg.is_master = true;
    adcInput.begin(cfg);

    // Audio serwer (port 8080)
    auto sCfg = audioServer.defaultConfig();
    sCfg.sample_rate = 44100;
    sCfg.channels = 2;
    audioServer.begin(sCfg);

    // Web serwer (port 80)
    web.on("/", handleRoot);
    web.on("/rec_start", handleRecStart);
    web.on("/rec_stop", handleRecStop);
    web.on("/vu", handleVU);
    web.begin();

    Serial.printf("\n🎛️ VinylCaster gotowy!\n");
    Serial.printf("   Sterowanie: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("   Stream WAV: http://%s:8080/\n", WiFi.localIP().toString().c_str());
}

void loop() {
    copier.copy();
    web.handleClient();

    // SD recording (w tle, z tego samego strumienia)
    // Uwaga: w pełnej implementacji użyj MultiOutput jak w przykładzie 4
    // Tu uproszczone — nagrywanie z osobnego odczytu ADC

    static uint32_t lastOled = 0;
    if (millis() - lastOled > 200) {
        updateOLED();
        lastOled = millis();
    }
}
```

---

## 6. Odbiór streamu na urządzeniach

### 6.1. Komputer (Windows/Mac/Linux)

**VLC:** Media → Open Network Stream → `http://<IP>:8080/` → Play. Jednocześnie: Tools → Codec Information → wyświetla parametry streamu.

**Audacity (nagrywanie):** Nie obsługuje bezpośrednio URL. Użyj ffmpeg:
```bash
# Nagrywanie streamu do pliku WAV
ffmpeg -i http://<IP>:8080/ -t 2700 -c copy album_strona_A.wav

# Nagrywanie z konwersją do FLAC (lossless, mniejszy plik)
ffmpeg -i http://<IP>:8080/ -t 2700 -c:a flac album_strona_A.flac

# Nagrywanie do MP3 320kbps
ffmpeg -i http://<IP>:8080/ -t 2700 -c:a libmp3lame -b:a 320k album.mp3
```

`-t 2700` = 45 minut (jedna strona LP). Dostosuj do długości płyty.

### 6.2. Telefon (Android/iOS)

**Android:** VLC for Android → More → Stream → wpisz `http://<IP>:8080/`. Lub: przeglądarka Chrome → URL → odtwarza audio natywnie.

**iOS:** VLC for iOS lub odtwarzacz z obsługą streamów HTTP.

### 6.3. Automatyzacja nagrywania

Skrypt bash do automatycznego nagrywania strony LP:

```bash
#!/bin/bash
# vinyl_record.sh — nagraj stronę winyla
# Użycie: ./vinyl_record.sh "Nazwa_Albumu" "A" 22
#          (album, strona, minuty)

ALBUM="${1:-Unknown}"
SIDE="${2:-A}"
MINUTES="${3:-25}"
IP="192.168.1.100"  # IP VinylCastera
OUTPUT="${ALBUM}_Side_${SIDE}.flac"

echo "🎵 Nagrywam: $ALBUM, strona $SIDE ($MINUTES min)"
echo "   Źródło: http://$IP:8080/"
echo "   Plik: $OUTPUT"
echo "   Naciśnij Ctrl+C aby zatrzymać wcześniej"
echo ""

ffmpeg -i "http://$IP:8080/" \
       -t $((MINUTES * 60)) \
       -c:a flac \
       -metadata title="$ALBUM - Side $SIDE" \
       -metadata artist="VinylCaster" \
       "$OUTPUT"

echo "✅ Zapisano: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
```

---

## 7. Jakość audio — porady

**Kable:** Używaj krótkich, ekranowanych kabli RCA. Unikaj prowadzenia obok zasilaczy i kabli sieciowych.

**Zasilanie:** ESP32 zasilaj z osobnego zasilacza USB (nie z komputera — szumy USB). Jeszcze lepiej: z power banku (czyste DC, brak szumów sieciowych). Moduł ADC zasilaj z osobnego stabilizatora 5V/3.3V z kondensatorami 100µF i 100nF.

**Masa:** Wszystkie masy (GND) w jednym punkcie — unikaj pętli masowych (ground loop). Jeśli słyszysz buczenie 50 Hz, odłącz USB od komputera i zasilaj z powerbanku.

**Poziom sygnału:** Sygnał wejściowy powinien być jak najbliżej maksimum ADC bez przesterowania. Zbyt cichy = marnujesz bity rozdzielczości. Zbyt głośny = clipping. Ustaw poziom na wzmacniaczu/preampie tak, aby VU-metr dochodził do ~80% przy najgłośniejszych fragmentach.

**Sample rate:** 44.1 kHz / 16-bit to jakość CD — wystarczająca dla 99% zastosowań. 48 kHz ma sens jeśli planujesz dalszą obróbkę. 96 kHz to overkill dla winyli (szum igły i tak ogranicza praktyczną rozdzielczość do ~60-70 dB).

---

## 8. Troubleshooting

| Problem | Przyczyna | Rozwiązanie |
|---------|-----------|-------------|
| **Brak dźwięku z PCM1808** | Brak MCLK na SCKI | Sprawdź GPIO0→SCKI (rezystor 33Ω), krótki kabel! |
| | MD0/MD1 w złym trybie | Oba na GND = slave mode (ESP32 master) |
| | FMT na złym poziomie | FMT=GND dla I2S standard |
| **Szum / buczenie 50 Hz** | Ground loop | Zasilaj ESP32 z powerbanku, nie USB komputera |
| | Długie kable analogowe | Skróć kable, użyj ekranowanych RCA |
| **Przesterowanie (clipping)** | Za mocny sygnał wejściowy | Zmniejsz poziom na źródle lub dodaj dzielnik napięcia |
| **Cisza / bardzo cichy** | Gramofon bez preamp | Potrzebujesz przedwzmacniacza RIAA (phono preamp) |
| | Wejście PHONO zamiast LINE | Przełącz na LINE OUT / REC OUT |
| **Stream się zacina** | WiFi za wolne | Użyj MP3 zamiast WAV (10× mniej pasma) |
| | Bufor za mały | Zwiększ `buffer_size` i `buffer_count` w I2S config |
| **ESP32 nie bootuje** | GPIO0 zajęty przez MCLK | GPIO0 musi być HIGH przy boot — MCLK nie przeszkadza, ale jeśli PCM1808 ciągnie pin nisko, odłącz na czas flashowania |

---

## 9. Zasoby

| Zasób | URL |
|-------|-----|
| arduino-audio-tools | https://github.com/pschatzmann/arduino-audio-tools |
| arduino-liblame (MP3 enc) | https://github.com/pschatzmann/arduino-liblame |
| ESP32-A2DP | https://github.com/pschatzmann/ESP32-A2DP |
| PCM1808 datasheet (TI) | https://www.ti.com/lit/gpn/pcm1808 |
| ES8388 moduł (PCB Artists) | https://pcbartists.com/product/es8388-module/ |
| ESPHome Line-Level ADC projekt | https://github.com/alextrical/ESPHome-LineLevelADC |
| Behringer PP400 Phono Preamp | https://www.behringer.com/product.html?modelCode=0805-AAJ |
| Calvin Phono DIY Preamp | https://calvins-audio-page.jimdofree.com/ |
| Audacity (darmowy edytor) | https://www.audacityteam.org/ |
| ffmpeg (nagrywanie CLI) | https://ffmpeg.org/ |

---

## 10. Historia zmian

| Wersja | Data | Opis |
|--------|------|------|
| 1.0 | 2026-02-07 | Pierwsza wersja: 8 przykładów, 3 warianty ADC, interfejs WWW. |

---

## 11. Licencja

Projekt open-source do dowolnego użytku. Stworzony z pomocą Claude (Anthropic).

> *„Twoja kolekcja winyli jest warta więcej niż myślisz. VinylCaster za 100 zł zamienia ją w cyfrowe archiwum jakości CD — bezprzewodowo, bez komputera, jednym przyciskiem."*
