# MinisGPS-RTK

> Kompaktowy odbiornik GNSS RTK o dokładności centymetrowej — ESP32-S3 jako klient NTRIP (ASG-EUPOS) zasilający poprawkami moduł Quectel LC29H, z logowaniem na kartę TF do PPK.

---

## 📋 Metryka projektu

| Pole | Wartość |
|---|---|
| **Wersja** | v0.2.0 |
| **Status** | 🟡 W trakcie |
| **Data rozpoczęcia** | 2026-08-01 |
| **Data ukończenia** | — |
| **Czas realizacji** | ~30 h (szacowany) |
| **Repozytorium** | [github.com/mhersztowski/minisgps-rtk](https://github.com/mhersztowski/minisgps-rtk) |
| **Licencja** | MIT |
| **Autor** | Marcin Hersztowski |

---

## 📖 Opis

Przenośny odbiornik RTK GNSS zbudowany na gotowej płytce rozwojowej łączącej **ESP32-S3** z modułem **Quectel LC29HDA** (dwuzakresowy L1+L5, RTK + wyjście RAW do PPK), w zestawie z anteną GNSS.

- **Cel** — pozycjonowanie z dokładnością centymetrową w terenie (pomiary działki, agro-IoT, mapowanie tras w Beskidach) bez drogiego sprzętu geodezyjnego
- **Główne funkcje:**
  - Klient NTRIP na ESP32-S3 — pobieranie poprawek RTCM3 z **ASG-EUPOS** przez WiFi (lub hotspot z telefonu)
  - Przekazywanie poprawek do LC29H przez UART → tryb RTK Fixed
  - Logowanie surowych danych na kartę **TF** → post-processing **PPK**
  - Publikacja pozycji przez **MQTT** do homelaba (Proxmox) / Bluetooth do telefonu
- **Motywacja** — kontynuacja researchu RTK (LC29H + ASG-EUPOS NTRIP); gotowa płytka eliminuje etap projektowania PCB dla pierwszego prototypu

### Kluczowe parametry

| Parametr | Wartość |
|---|---|
| MCU | ESP32-S3 N16R8 (16 MB Flash / 8 MB PSRAM) |
| Moduł GNSS | Quectel LC29HDA, dwuzakresowy L1+L5 |
| Systemy | GPS, BeiDou, Galileo, GLONASS, QZSS |
| Dokładność | poziom cm (RTK Fixed z poprawkami NTRIP) |
| Wyjście RAW | tak (obserwacje do post-processingu PPK) |
| Komunikacja | WiFi 2.4 GHz, Bluetooth 5.0 (BLE) |
| Pamięć | slot karty TF (logi PPK) |
| Interfejs / zasilanie | USB-C, 5 V |
| Antena | GNSS aktywna (w zestawie), złącze IPEX/SMA |

---

## 🔩 Sprzęt (Hardware)

### Płytka bazowa (gotowa)

| Nazwa | Wariant | Źródło | Cena | Link |
|---|---|---|---|---|
| Płytka rozwojowa ESP32-S3 + LC29H RTK | ESP32-S3 + LC29HDA + antena (ANT) | AliExpress (Mozihao Tech Store) | 150,79 zł + 12,58 zł wysyłka | [oferta](https://pl.aliexpress.com/item/1005011979965293.html) |

**Uwagi:**
- Wariant **DA** = rover RTK z wyjściem surowych obserwacji (RAW) — idealny do PPK; brak DR (dead reckoning, to wariant EA)
- Do trybu własnej stacji bazowej potrzebny byłby wariant **BS**
- Płytka z obsługą PPK i zapisem na kartę TF

### Elementy dodatkowe (BOM)

| Komponent | Ilość | Źródło | Uwagi |
|---|---|---|---|
| Karta microSD (TF) 16–32 GB | 1 | — | logi RAW do PPK |
| Akumulator Li-Ion 18650 + moduł ładowania | 1 | zapasy warsztatowe | praca terenowa |
| Przewód/pigtail IPEX → SMA | 1 | AliExpress | jeśli antena z zestawu ma inne złącze |

### Modele 3D (obudowa)

Planowane (pliki pojawią się w `3d/` po pierwszym prototypie):

| Model | Drukarka / technologia | Materiał | Status |
|---|---|---|---|
| Obudowa terenowa (dół) | Bambu A1 Mini (FDM) | PETG | TODO |
| Obudowa (góra, okno LED) | Bambu A1 Mini (FDM) | PETG | TODO |
| Podstawka anteny / ground plane | Bambu A1 Mini (FDM) | PETG | TODO |

**Parametry druku:** warstwa 0.2 mm, wypełnienie 20%, PETG (odporność UV w terenie).

---

## 📁 Struktura repozytorium

```
MinisGPS-RTK/
├── src/                      # biblioteka (wchodzi do builda firmware)
│   ├── LocationSensor.h      # interfejs + LocationData + FixQuality
│   ├── LocationJson.h        # serializacja do JSON (payload MQTT)
│   ├── SerialPort.h          # abstrakcja UART/zegara (Arduino + host)
│   ├── Lc29hSensor.h
│   └── Lc29hSensor.cpp       # parser NMEA + komendy PAIR + RTCM
├── examples/
│   └── Lc29hBasic/           # szkic .ino (NIE kompiluje się z biblioteką)
├── firmware/
│   └── config.example.h      # → skopiuj do firmware/config.h
├── test/
│   └── test_nmea_parser/     # testy parsera na hoście (pio test -e native)
├── doc/
│   └── README.md
├── library.properties        # Arduino Library Manager
├── library.json              # PlatformIO
└── platformio.ini
```

> **Uwaga:** przykład leży w `examples/`, a nie w `src/` — inaczej jego `setup()`/`loop()`
> trafiałyby do builda biblioteki i kolidowały z firmware'em.

---

## 💻 Oprogramowanie (Firmware)

- **Framework:** **Arduino** (core arduino-esp32) — Arduino IDE 2.x lub PlatformIO z `framework = arduino`
- **Język:** C++17 (`-std=gnu++17`)
- **Kluczowe komponenty:**
  - Parser NMEA dla LC29H: **GGA, RMC, GSA, GST** + ACK komend `$PAIR001`
  - Diagnostyka RTK: wiek poprawek RTCM i ID stacji bazowej (GGA p.13/14), `hAcc`/`vAcc` z GST
  - Nieblokujące wejście RTCM3 (`writeRtcm()`, chunkowane) — pod klienta NTRIP
  - Logger RAW → karta TF (PPK) — *TODO*
  - Publisher MQTT (pozycja, status fix, HDOP) → broker na Proxmoksie — *TODO*

**Biblioteki:** `WiFi.h`, `HTTPClient` (w core), `PubSubClient` (MQTT), `SD_MMC` (logi PPK).

**Board:** `ESP32S3 Dev Module` (Flash 16 MB, PSRAM OPI, USB CDC On Boot: Enabled)

### Build

```bash
cp firmware/config.example.h firmware/config.h   # uzupełnij WiFi / NTRIP / MQTT

pio run -e esp32s3 -t upload && pio device monitor -b 115200
pio run -e esp32s3-example -t upload             # sam przykład
pio test -e native                               # testy parsera NMEA na hoście
```

### Użycie biblioteki

```cpp
#include <Lc29hSensor.h>
#include <LocationJson.h>
using namespace minis;

static minis::ArduinoSerialPort gnssPort(Serial1);

static Lc29hSensor::Config makeCfg() {
    Lc29hSensor::Config c;
    c.withPort(gnssPort).withPins(17, 18).withBaudRate(115200).withRateMs(1000);
    return c;
}
static Lc29hSensor gnss(makeCfg());

void setup() {
    gnss.begin();
    gnss.onUpdateRaw([](const LocationData& d, void*) {
        char json[320];
        if (toJson(d, json, sizeof(json))) Serial.println(json);
    });
}
void loop() { gnss.update(); delay(5); }
```

**Ważne uwagi eksploatacyjne:**
- `rateMs < 500` przy pełnym NMEA **nie zmieści się w 115200 baud** — driver sam wyłącza wtedy GSV, ale przy 5–10 Hz podnieś `baudRate`.
- Epoka jest publikowana po GGA+RMC albo po `epochTimeoutMs` (domyślnie 400 ms), więc wyłączony RMC nie blokuje fixa.
- Gdy `quality == NoFix`, pozycja jest **zerowana** — nie ma ryzyka odczytania starych współrzędnych jako aktualnych.
- `writeRtcm()` zwraca liczbę wysłanych bajtów; resztę podaj ponownie w kolejnym obiegu `loop()`.
- Diagnostyka łącza: `gnss.stats()` (checksum errors, overflow, epoki, bajty RTCM).

---

## 🔗 Projekty powiązane

| Projekt | Relacja | Link |
|---|---|---|
| Hydra Framework | docelowa warstwa aplikacyjna firmware | github.com/mhersztowski/... |
| Agro-IoT (LoRaWAN) | precyzyjne pozycjonowanie czujników polowych | — |
| Quarto GPS RTK | dokumentacja teoretyczna + symulacje Python | — |
| MyCastle | wizualizacja pozycji na mapie (Leaflet, MQTT) | github.com/mhersztowski/MyCastle |

---

## 📝 Dziennik zmian (Changelog)

### v0.2.0 — przegląd i utwardzenie drivera
- **Fix:** pozycja z poprzedniej epoki nie wycieka do `data()` po utracie fixa (zerowanie przy `NoFix`, czysty `_pending` na start epoki)
- **Fix:** epoka publikowana po timeoucie — brak/zgubiony RMC nie blokuje już `hasFix()` na zawsze
- **Fix:** filtrowanie talkera (preferencja `GN`) — `$GPGGA` + `$GNGGA` nie mieszają epok
- **Fix:** walidacja długości nagłówka i formatu checksumy — koniec czytania poza `$xx`
- **Fix:** `end()` czyści stan; `begin()` odczekuje boot modułu i weryfikuje ACK `$PAIR001`
- **Fix:** `LINE_MAX` 120 → 160 B (długie GSV L1+L5), licznik `overflows`
- **Nowe:** wiek poprawek RTCM + ID stacji bazowej, `hAcc`/`vAcc` (GST), `pdop`/`vdop` (GSA), `geoidSep`
- **Nowe:** `writeRtcm()` nieblokujące/chunkowane, `Stats`, `enableGsv()`, callback bez alokacji (`onUpdateRaw`)
- **Nowe:** `SerialPort.h` (abstrakcja UART) → testy parsera na hoście, `LocationJson.h`
- **Refactor:** przykład do `examples/`, dodane `library.properties`, `library.json`, `platformio.ini`, `.gitignore`, `firmware/config.example.h`

### v0.1.0 — 2026-08-01
- Wybór platformy sprzętowej (ESP32-S3 + LC29HDA + antena, płytka gotowa z AliExpress)
- Założenie dokumentacji projektu
- Pierwsza wersja drivera LC29H (GGA + RMC)

---

## ✅ TODO / Plany rozwoju

- [ ] Zamówienie płytki + anteny wielopasmowej
- [ ] Pierwsze uruchomienie: NMEA po USB, fix bez poprawek
- [ ] Konto ASG-EUPOS + klient NTRIP (RTK Fixed) — wykorzystać `writeRtcm()`
- [ ] Wysyłanie własnej GGA do serwera VRS (co 10 s)
- [ ] Logowanie RAW na TF i test workflow PPK (RTKLIB)
- [ ] Zasilanie bateryjne 18650 + pomiar czasu pracy
- [ ] Obudowa terenowa (FDM, PETG) → pliki do `3d/`
- [ ] Zdjęcia prototypu i testu terenowego → `doc/img/`
- [ ] Publikacja MQTT → mapa Leaflet w MyCastle
- [ ] (opcjonalnie) druga płytka jako lokalna baza RTK (wariant BS)

---

## 📚 Źródła i inspiracje

- Oferta płytki: [AliExpress — ESP32-S3 + LC29H RTK](https://pl.aliexpress.com/item/1005011979965293.html) (zrzut: `doc/StronaAlliexpress.mht`)
- Quectel LC29H — dokumentacja serii (Hardware Design, GNSS Protocol)
- ASG-EUPOS — serwis poprawek NTRIP dla Polski
- RTKLIB — post-processing PPK
