# Hydra

Uniwersalny framework robotyczno-IoT dla ESP32 (C3/S3), RP2040/RP2350 i STM32.
Implementacja dokumentu architektury *Hydra Framework v0.1 (draft)*.
Wszystkie etapy mapy drogowej są zrealizowane — wersja **1.0.0**.

Ten sam kod aplikacji kompiluje się bez zmian na wszystkich platformach
docelowych. Pod spodem leży FreeRTOS i Arduino API, ale kod użytkownika nie
widzi ani jednego, ani drugiego — backend da się w przyszłości wymienić
(np. na Zephyra) bez zmian w aplikacjach.

## Stan realizacji

Mapa drogowa z rozdz. 14 specyfikacji:

| Etap | Zakres | Stan |
|------|--------|------|
| **1a — rdzeń** | `App`, `Task`, `EventBus`, `Log`, `IModule`, warstwa RTOS | ✅ gotowe |
| **1b — HAL + build** | `IGpio`, `IBus`, `IPwm`, `IAdc`, `IStorage`, `ITime`, backend Arduino, backend atrapowy, płytki, `platformio.ini`, CI | ✅ gotowe |
| **M2 — sense** | `ISensor`, `SensorHub` (grupowanie po GCD), filtry, kalibracja, detekcja anomalii, fuzja IMU, 3 sterowniki | ✅ gotowe |
| **M3 — net** | `ConnectionManager`, MQTT 3.1.1, mDNS, mostek telemetrii, poświadczenia w pamięci trwałej | ✅ gotowe |
| **M4a — gfx** | `ISurface`, prymitywy z przycinaniem, `Framebuffer`, adaptery 5 bibliotek graficznych | ✅ gotowe |
| **M4b — potok renderowania** | `IDisplayBackend`, obszary zmienione, podwójne buforowanie, kolejka poleceń, wejście, task `ui.render` | ✅ gotowe |
| **M4c — warstwa deklaratywna** | `Screen` push/pop, 7 widżetów, wiązanie z EventBusem, motywy | ✅ gotowe |
| **M4d — renderer LVGL** | most LVGL 9 do dowolnego panelu Hydry, kolejka poleceń, wejście | ✅ gotowe |
| **M5 — motion** | PID, enkodery, napęd różnicowy, odometria, łańcuch bezpieczeństwa, pętla RT | ✅ gotowe |
| **M6a — diagnostyka** | shell (`ps`, `top`, `i2c scan`, `cfg`, `log`), zapis okoliczności awarii | ✅ gotowe |
| **M6b — OTA** | aktualizacja przez HTTP, SHA-256, tryb próbny z powrotem | ✅ gotowe |
| **M6c — TLS i wydanie** | `TlsClient`, szablon projektu, testy na sprzęcie w CI, dokumentacja API | ✅ gotowe |

## Środowisko budowania

Wsady na wszystkie pięć platform buduje obraz Dockera z wpieczonym PlatformIO
Core i kompletem toolchainów. Sama budowa wsadu nie rusza wtedy sieci, a to,
co powstaje na stanowisku programisty i w CI, wychodzi z tego samego obrazu.

```bash
./docker/hydra.sh pull                      # gotowy obraz z rejestru
./docker/hydra.sh test                      # testy hostowe, sanitizery, dokumentacja
./docker/hydra.sh fw esp32s3 blink-task     # jeden wsad
./docker/hydra.sh ci                        # komplet: testy + wszystkie wsady
```

Na serwerze to samo przez compose, bez zakładania konsoli:

```bash
UID=$(id -u) GID=$(id -g) docker compose -f docker/compose.yaml run --rm ci
```

Obraz jest dwuarchitekturowy — `linux/amd64` dla typowego serwera i
`linux/arm64` dla Maca z układem Apple oraz Windows 11 arm64 pod WSL.
Docker sam wybiera właściwy wariant, więc tag jest jeden.

Budowa lokalna (kilka gigabajtów pobierania, więc raczej rzadko):

```bash
./docker/hydra.sh build                     # wszystkie platformy
HYDRA_ENVS="esp32s3" ./docker/hydra.sh build   # tylko jedna
```

Wersje platform są przypięte w `docker/Dockerfile` (`PLATFORM_ESPRESSIF32`,
`PLATFORM_STSTM32`, `PLATFORM_RASPBERRYPI` — ta ostatnia commitem, bo platforma
dla RP2040/RP2350 pochodzi z forka, nie z rejestru). Bez przypięcia obraz
zbudowany za miesiąc zawierałby inny kompilator niż ten, na którym wsad
został sprawdzony.

Kontener działa jako wywołujący (`--user`), więc artefakty w podmontowanym
drzewie należą do właściciela repozytorium — nie trzeba ich odzyskiwać
`chown`-em ani sprzątać przez `sudo`.

## Co jest naprawdę sprawdzone

| Weryfikacja | Zakres |
|---|---|
| Testy hostowe | 428 przypadków, 2566 asercji — zwykle, pod ASan+UBSan i pod TSan |
| Wsady | 6 przykładów × 5 platform przez `pio ci` w obrazie Dockera |
| Backend Arduino | kontrola składni na atrapach: ESP32 (rdzeń 2.x i 3.x), RP2040, STM32 |
| Dokumentacja | fragmenty z `docs/api.md` kompilowane przez `make -C test docs` |
| Reguły zależności | `tools/check_includes.sh` |

Dwa przykłady są świadomie pomijane na `stm32g4`: Nucleo-G474RE nie ma żadnego
sprzętu sieciowego, więc `mqtt-telemetry` i `smart-display` nie mają tam prawa
działać. Powód zapisany jest w pliku `skip` obok przykładu i pojawia się
w wyniku przebiegu — pominięcie nigdy nie udaje sukcesu.

Czego nie sprawdzono: **nic nie zostało uruchomione na fizycznej płytce.**
Wsady się budują i mieszczą w pamięci, ale zachowanie na sprzęcie zweryfikuje
dopiero `tools/hil_run.py` na prawdziwym urządzeniu.

## Nowy projekt

```bash
cp -r libs/Hydra/templates/starter moj-projekt
cd moj-projekt && pio run -e esp32s3 -t upload && pio device monitor
```

Szablon zawiera moduł aplikacji z taskiem okresowym, logowanie i shell
diagnostyczny. Moduły opcjonalne włącza się flagą w `platformio.ini` —
szczegóły w [templates/starter/README.md](templates/starter/README.md).

## Dokumentacja

- [docs/](docs/) — **dokumentacja techniczna**: architektura, płytki, budowanie,
  testy, format `.hydra`, paczki, schemat, Studio i zebrane pułapki.
- [docs/api.md](docs/api.md) — przegląd publicznego API warstwa po warstwie.
  Każdy fragment kodu w tym dokumencie jest kompilowany przez `make -C test docs`,
  więc nie może opisywać funkcji, których nie ma.
- Nagłówki — każdy zaczyna się komentarzem tłumaczącym, po co dany element
  istnieje i jakie decyzje projektowe za nim stoją.
- [examples/](examples/) — sześć działających przykładów, po jednym na moduł.

## Połączenia szyfrowane

`net::TlsClient<T>` opakowuje gniazdo bezpieczne platformy (mbedTLS, BearSSL).
Warstwa protokołu przyjmuje `IClient&`, więc szyfrowanie włącza się podmianą
obiektu — `MqttClient` i `OtaUpdater` nie wymagają żadnych zmian:

```cpp
WiFiClientSecure secure;
net::TlsClient<WiFiClientSecure> tls(secure);
tls.configure({.caCertificate = kIsrgRootX1});
```

Tryb `allowInsecure` istnieje, bo bywa potrzebny przy własnym certyfikacie
w sieci lokalnej, ale wymaga jawnego włączenia i zostawia ostrzeżenie w logu.
TLS bez weryfikacji tożsamości serwera chroni wyłącznie przed biernym
podsłuchem — wygląda na bezpieczny, a nie zatrzyma nikogo, kto stanie
w środku połączenia.

## Testy na fizycznym sprzęcie

`tools/hil_run.py` steruje urządzeniem przez shell i sprawdza odpowiedzi.
To dlatego każda komenda wypisuje wynik także jako pary `klucz=wartość`:

```bash
tools/hil_run.py --port /dev/ttyUSB0 --suite all
```

Zestawy: `basic` (odpowiedź, taski, zapas stosu, gubione zdarzenia),
`i2c` (obecność układów), `storage` (trwałość konfiguracji), `leak`
(sterta po serii komend — po `App::begin()` nie ma alokacji, więc każdy
stały ubytek jest błędem). Uruchamiane workflow `hydra-hil` na własnym
runnerze z podłączonymi płytkami.

## Struktura

```
include/Hydra.h            # jedyny nagłówek dla aplikacji
include/hydra/core/        # publiczne API rdzenia
include/hydra/hal/         # interfejsy sprzętowe + atrapy do testów
include/hydra/boards/      # definicje pinów płytek
src/core/                  # implementacja rdzenia (czysty C++17)
  rtos_host.cpp            #   backend RTOS: POSIX/pthread (testy na PC)
  rtos_freertos.cpp        #   backend RTOS: ESP32 / RP2 / STM32
src/hal/                   # część wspólna HAL i rejestr sterowników
  arduino/                 #   JEDYNE miejsce z #include <Arduino.h>
  mock/                    #   backend atrapowy (build hostowy)
include/hydra/sense/       # moduł czujników (opcjonalny)
include/hydra/net/         # moduł sieciowy (opcjonalny)
include/hydra/gfx/         # warstwa graficzna
  adapters/                #   adaptery bibliotek jako szablony (bez ich nagłówków)
include/hydra/ui/          # potok renderowania i wejście
include/hydra/motion/      # napęd: regulacja, kinematyka, bezpieczeństwo
include/hydra/shell/       # shell diagnostyczny
include/hydra/diag/        # zapis okoliczności awarii
include/hydra/ota/         # aktualizacja oprogramowania przez sieć
include/hydra/util/        # SHA-256 i HMAC
include/hydra/drivers/     # adaptery konkretnych układów
src/net/arduino/           #   drugi katalog backendu z nagłówkami Arduino
examples/                  # blink-task, i2c-scan, telemetry, mqtt-telemetry,
                           # smart-display, rover
test/                      # testy jednostkowe + harness + atrapy nagłówków
tools/check_includes.sh    # test reguł zależności między warstwami
```

Pliki płytek leżą w `include/hydra/boards/`, a nie w `boards/` jak w szkicu
z rozdz. 12. Powód jest praktyczny: `pio ci` buduje przykład w katalogu
tymczasowym, więc względne `-I boards` przestałoby działać. Na publicznej
ścieżce include działa bez dodatkowych flag.

Zależności biegną wyłącznie w dół i jest to sprawdzane, a nie tylko zadeklarowane
(`tools/check_includes.sh`, uruchamiany w CI):

- nagłówki Arduino wyłącznie w katalogach backendów (`src/*/arduino/`),
- `lvgl.h` wyłącznie w `LvglApi.hpp`,
- warstwa HAL używa tylko fundamentów rdzenia (`Types`, `Expected`, `Delegate`,
  `Rtos`, `Config`) — nigdy `App`, `EventBus`, `IModule` czy `Log`,
- FreeRTOS widoczny wyłącznie w `src/core/rtos_freertos.cpp`.

## Testy

Rdzeń kompiluje się natywnie na PC i tam jest testowany — bez sprzętu,
bez FreeRTOS-a, bez Arduino.

```bash
cd test
make                  # kompilacja i uruchomienie
make FILTER=EventBus  # tylko wybrane przypadki
make asan             # sanitizer adresów i UB
make tsan             # sanitizer wyścigów
make examples         # czy przykłady kompilują się bez Arduino
make stub             # kontrola składni backendu Arduino na atrapach nagłówków
```

Stan: **419 przypadków, 2531 asercji, 0 błędów** — czysto również pod ASan,
UBSan i TSan.

Backendu Arduino nie da się skompilować bez toolchaina embedded, więc
`make stub` przepuszcza go przez kompilator na atrapach nagłówków dla trzech
rodzin platform (ESP32, RP2, STM32). To łapie literówki i błędy typów
w gałęziach platformowych; pełną weryfikacją pozostaje `pio ci` w CI.

Testy budują się z `-fno-exceptions -fno-rtti`, tak samo jak build na MCU.
Inaczej sprawdzałyby inny kod niż ten, który trafia na urządzenie.

## Rdzeń w skrócie

**Cykl życia.** `App::begin()` ukrywa różnicę w starcie schedulera między
platformami (na STM32 startuje go ręcznie i nigdy nie wraca). Moduły
implementują `init()` / `start()` / `stop()` i są uruchamiane w kolejności
rejestracji; nieudany start cofa to, co już działa.

```cpp
#include <Hydra.h>

class Blink : public hydra::ModuleBase {
public:
    Blink() : ModuleBase("blink") {}
protected:
    hydra::Status onInit() override { return hydra::ok(); }
    hydra::Status onStart() override {
        hydra::Task::Cfg cfg;
        cfg.name = "blink.tick";
        cfg.prio = hydra::Prio::Low;
        return task_.startPeriodic(cfg, 500, [this] { toggle(); });
    }
    void onStop() override { task_.stopAndWait(); }
private:
    void toggle() { /* ... */ }
    hydra::Task task_;
};

Blink blink;

void setup() {
    hydra::App::config().name("rover-01").add(blink);
    hydra::App::begin();
}
void loop() {}   // nieużywane; na STM32 martwe z definicji
```

**Magistrala zdarzeń.** Moduły nie wołają się nawzajem. Zdarzenia to POD-y do
32 B; dostarczenie w kontekście nadawcy (`Direct`) albo subskrybenta (`Queued`,
przez `Inbox`). Przerwania wyłącznie zgłaszają zdarzenia — `publishFromIsr()`
odkłada je do kolejki opróżnianej przez task `core.house`.

```cpp
hydra::EventBus::subscribe<BatteryEvent>([](const BatteryEvent& e) {
    if (e.percent < 15) hydra::EventBus::publish(LowPowerEvent{});
});
hydra::EventBus::publish(BatteryEvent{.percent = 12, .mv = 3512});
```

**Taski.** Okres egzekwuje `delayUntil`, więc nie dryfuje wraz z czasem
wykonania ciała pętli. Spóźniona iteracja podnosi licznik, a po przekroczeniu
progu trafia na magistralę jako `TaskDeadlineMissed`.

**Logi.** Filtr kompilacyjny i runtime (globalny + per moduł), wymienne sinki,
bufor pierścieniowy zrzucany po awarii oraz tryb deferowany — kosztowny zapis
na UART przenosi się do `core.house`, żeby nie wprowadzał jitteru w pętlach RT.

**Brak alokacji po starcie.** Tablica subskrypcji, kolejki i bufory mają stały
rozmiar. Wyjątki i RTTI wyłączone — błędy propaguje `expected<T, Err>`.
Callbacki trzyma `Delegate` z buforem inline; zbyt duże domknięcie nie
kompiluje się, zamiast po cichu sięgnąć na stertę.

**Bez FPU.** `real_t` to `float` tam, gdzie jest FPU, i `Fixed` (Q16.16) na
RP2040. Kod regulatorów pozostaje ten sam.

## Warstwa HAL

**Magistrale zawsze pod blokadą.** Nie istnieje publiczne API pozwalające
dotknąć magistrali bez mutexu — jedyną drogą jest `transaction()`, które
przyjmuje ciało operacji i wykonuje je pod blokadą. O blokadzie nie da się
zapomnieć, bo bez sesji nie ma jak wykonać transferu. To zamyka całą klasę
błędów wynikającą z tego, że biblioteki Arduino nie są thread-safe.

```cpp
auto r = hal::Hal::i2c(0).transaction([](hal::II2cBus::Session& s) -> Status {
    HYDRA_TRY(const u8 who, s.readReg8(0x68, 0x75));
    return who == 0x71 ? ok() : fail(Err::NotFound);
});
```

SPI dokłada obsługę CS: sygnał wraca w stan wysoki także wtedy, gdy ciało
transakcji zwróci błąd — zapomniana deselekcja zablokowałaby magistralę
pozostałym układom.

**Brak sterownika to nie awaria.** Akcesory `Hal::` nigdy nie zwracają
nullptra; nieobecna peryferia daje obiekt pusty, który kwituje operacje
`Err::NotSupported`. Kod aplikacji nie sprawdza wskaźników, a build dla płytki
bez ADC dalej się linkuje.

**Jednostki fizyczne zamiast rejestrów.** ADC zwraca miliwolty źródła —
kalibracja i dzielnik napięcia są częścią HAL, nie obowiązkiem wołającego.
PWM przyjmuje promile wypełnienia albo mikrosekundy impulsu dla serw.
Całość przeliczeń na liczbach całkowitych, żeby działała bez FPU.

**Piny mają nazwy.** Aplikacja mówi `Pin::MotorLeftPwm`, nigdy „GPIO 17".
Mapowanie żyje w pliku płytki wskazywanym flagą kompilacji:

```ini
build_flags = -D HYDRA_BOARD_HEADER='"hydra/boards/rover_s3.hpp"'
```

**Backend atrapowy.** Build hostowy dostaje komplet atrap: I2C z mapą
rejestrów, GPIO z wyzwalaniem przerwań, UART z wstrzykiwaniem danych,
sterowalny ADC i pamięć trwałą w RAM. Dzięki temu sterownik czujnika da się
przetestować w całości na PC — to na tym oprze się etap M2.

```cpp
auto& mock = hal::mock::backend();
mock.i2c.addDevice(0x76);
mock.i2c.setReg(0x76, 0xD0, 0x60);   // BME280 chip id
```

## Moduł czujników (`hydra::sense`)

Włączany flagą `-D HYDRA_ENABLE_SENSE=1`; bez niej nie wchodzi do binarki.

**Jeden task na wszystkie czujniki.** Hub grupuje czujniki okresowe według
największego wspólnego dzielnika okresów. Czujniki 2000, 500 i 100 ms dają
tyknięcie 100 ms i dzielniki 20, 5 i 1 — jeden task zamiast trzech, bez dryfu
i bez budzenia procesora częściej, niż trzeba.

**Znacznik czasu z chwili pomiaru, nie publikacji.** W trybie data-ready
pochodzi z ISR, w okresowym — sprzed transferu. Odczyt przez I2C przy 100 kHz
trwa setki mikrosekund; stemplowanie po nim przesuwałoby wszystkie próbki
i psuło całkowanie w odometrii i fuzji IMU.

**Łańcuch przetwarzania**: kalibracja (offset/gain z `IStorage`) → filtr
(mediana / EMA / Butterworth 2. rzędu) → detekcja anomalii (zamrożona wartość,
skok, zakres) → publikacja na EventBus. Wykryta nieprawidłowość obniża
`Sample::quality` i osobno trafia na magistralę jako `SensorAnomaly`.

**Brakujący czujnik nie blokuje startu.** Nieudany `probe()` oznacza wpis jako
niedostępny i zostawia ślad w logu. Robot ma pojechać bez jednego czujnika
odległości, a nie odmówić uruchomienia.

```cpp
sense::SensorHub::Registration reg;
reg.sensor.periodMs      = 500;
reg.filter.kind          = sense::FilterKind::Median;
reg.anomaly.frozenLimit  = 10;
hub.add(powerMeter, reg);

EventBus::subscribe<sense::Sample>([](const sense::Sample& s) {
    // jedna subskrypcja, wszystkie czujniki — rozróżnia je s.topic
});
```

**Adapter czujnika to kilkanaście linii** — cała logika harmonogramu
i przetwarzania jest w hubie. Sterowniki referencyjne: `Bme280` (temperatura,
ciśnienie, wilgotność), `Ina219` (napięcie, prąd, moc), `As5600` (kąt).
Zamiast VL53L0X ze specyfikacji wybrany został AS5600: jego sterownik pokazuje
tezę o krótkim adapterze, podczas gdy VL53L0X wymaga dwustustronicowej sekwencji
inicjalizacyjnej producenta, której i tak nie da się zweryfikować bez sprzętu.

**Fuzja IMU** (`sense::ImuFusion`) jest czujnikiem wirtualnym: nie dotyka
magistrali, subskrybuje próbki akcelerometru i żyroskopu, publikuje kwaternion.
Filtr komplementarny zamiast Madgwicka — przy jednym mnożeniu na oś daje
w zastosowaniach robotycznych wynik nieodróżnialny, a mieści się w budżecie
czasu także na RP2040. Korekta akcelerometrem działa tylko wtedy, gdy moduł
wektora jest bliski 1 g; podczas gwałtownego ruchu mierzy on grawitację razem
z przyspieszeniem własnym i „poprawiłby" orientację w złą stronę.

Kompensacja BME280 jest transkrypcją formuł całkowitoliczbowych z dokumentacji
Boscha. Testy sprawdzają identyfikację układu, sekwencję konfiguracji,
składanie słów z rejestrów, monotoniczność i zakresy fizyczne wyników;
zgodność bezwzględna wymaga porównania z fizycznym układem i należy do HIL.

## Moduł sieciowy (`hydra::net`)

Włączany flagą `-D HYDRA_ENABLE_NET=1`.

**Czas wchodzi argumentem.** `ConnectionManager::tick(now)` dostaje bieżącą
chwilę od wołającego, zamiast czytać zegar w środku. Dzięki temu wykładniczy
backoff, przełączanie sieci zapasowych i zrywanie martwej sesji MQTT testują
się deterministycznie, w mikrosekundach zamiast realnych minut.

**Warstwy są rozdzielone.** Utrata brokera przy sprawnym Wi-Fi przenosi
połączenie w `Degraded` i uruchamia ponowne łączenie z brokerem — ale nie
zrywa łącza. Zrywanie Wi-Fi z powodu problemów brokera tylko wydłużałoby
przestój.

**Poświadczenia nie wyciekają do logów** i wymusza to typ, a nie dyscyplina:
`SecretString` nie ma niejawnej konwersji do `const char*`, a jego
reprezentacja tekstowa jest zawsze maską o stałej długości (nie zdradza nawet
długości sekretu). Dostęp do zawartości wymaga jawnego `reveal()` — wywołania
widocznego w przeglądzie kodu.

**MQTT 3.1.1** z QoS 0/1, Last Will, retransmisją po timeoucie i automatyczną
resubskrypcją po każdym zerwaniu. Format ramek weryfikowany jest w testach na
poziomie bajtów, a atrapa gniazda gra rolę brokera.

**Mostek telemetrii jest deklaratywny** — mapowanie tematów MQTT na zdarzenia
i z powrotem deklaruje się raz, bez kodu w pętli:

```cpp
bridge.publishOn<SysHeartbeat>("hydra/rover-01/sys", 0, false, formatHeartbeat);
bridge.subscribeTo<SetInterval>("hydra/rover-01/cmd", 1, parseInterval);
```

Formatery i parsery przyjmowane są jako wskaźniki na funkcje, nie domknięcia:
mostek trzyma je w tablicy o stałym rozmiarze. Obcięty ładunek jest porzucany,
a nie wysyłany uszkodzony.

## Warstwa graficzna (`hydra::gfx`)

**Jedna wymagana metoda.** Backend implementuje `writePixel()`; wszystkie
pozostałe prymitywy mają implementacje programowe i nadpisuje się tylko te,
które biblioteka robi sprzętowo. Kształt wzięty z `MinisGfx` — napisanie
adaptera nad nową biblioteką kosztuje kilkanaście linii.

**Adaptery są szablonami, nie klasami.** To nie kosmetyka: dzięki temu Hydra
nie włącza ani jednego nagłówka biblioteki graficznej, reguła zależności
z rozdz. 3 pozostaje nienaruszona, a adaptery da się przetestować na hoście
atrapą urządzenia o tym samym API. Aplikacja włącza nagłówek producenta
przed adapterem — i tak by to zrobiła, bo tworzy obiekt wyświetlacza.

```cpp
#include <TFT_eSPI.h>
#include <hydra/gfx/adapters/TftEspiSurface.hpp>

TFT_eSPI tft;
hydra::gfx::TftEspiSurface<TFT_eSPI> screen(tft);
```

Gotowe adaptery: **Adafruit_GFX**, **TFT_eSPI**, **LovyanGFX**, **U8g2**
(monochromatyczne) oraz **MinisGfx** — ten ostatni jest mostem do
`libs/MinisLib`, dzięki któremu backendy napisane już dla MinisGfx
(Adafruit, LovyanGFX, GxEPD2, Qt) działają w Hydrze bez przenoszenia kodu.
MinisLib pozostaje ich jedynym źródłem.

**Trzy rzeczy w kontrakcie, których MinisGfx nie miał:**

- **przycinanie** — obowiązuje wszystkie prymitywy, także wtedy gdy biblioteka
  producenta go nie zna; bez tego widżet zamazywałby sąsiada,
- **kody błędów** — panel na SPI potrafi nie odpowiedzieć,
- **obszar zmieniony** — na e-papierze i wolnym SPI to różnica między
  przerysowaniem całego ekranu a jednego napisu.

**Framebuffer nigdy nie alokuje.** Bufor dostarcza wołający:

```cpp
static u8 vram[Framebuffer::bytesNeeded(128, 64, PixelFormat::Mono1)];
Framebuffer fb;
fb.attach(ByteSpan{vram, sizeof(vram)}, 128, 64, PixelFormat::Mono1);
```

Formaty: `Mono1`, `Rgb565`, `Rgb888`, `Rgba8888`. Kolor przenoszony jest zawsze
jako RGBA8888 i konwertowany leniwie — na panelu jednobitowym progowany wg
Rec.601, więc ten sam kod rysujący w kolorze daje czytelny obraz i tam.

**Wypełnianie trójkąta jest zachowawcze** — obejmuje pełny zakres, jaki krawędź
pokrywa w danym wierszu, a nie jeden punkt przecięcia. Dzięki temu wypełnienie
zawsze pokrywa własny obrys; klasyczne wypełnienie skanowe zostawia w tym
miejscu szczelinę szerokości piksela. Kosztem jest figura szersza o najwyżej
jeden piksel na płaskich skosach niż w Adafruit_GFX.

## Potok renderowania (`hydra::ui`)

**Klatka bez zmian nie kosztuje nic.** Jeśli nikt niczego nie unieważnił,
renderer nie rysuje i nie transferuje. Nieruchomy ekran statusu — stan,
w którym urządzenie IoT spędza większość życia — zużywa tyle procesora,
co zatrzymany task.

**Zmiany spoza `ui.render` idą kolejką poleceń** (wzorzec „UI thread" z Qt,
rozdz. 6). Powód jest twardy: LVGL, LovyanGFX i TFT_eSPI nie są thread-safe,
a mutacja z drugiego taska w trakcie renderowania objawia się kilka klatek
później, w zupełnie innym miejscu.

```cpp
ui.queue().post([&] { renderer.invalidate(statusBar); });
```

Przepełniona kolejka porzuca **najstarsze** polecenie — w interfejsie świeższa
informacja jest cenniejsza od starszej.

**Sterowniki wejścia zgłaszają stan, nie zdarzenia.** Wykrywanie zboczy,
liczenie różnic enkodera i mierzenie czasu przytrzymania robi `InputRouter` —
raz, tak samo dla panelu pojemnościowego i rezystancyjnego. Oderwanie palca
zgłaszane jest w ostatnim znanym punkcie kontaktu, bo współrzędne po oderwaniu
bywają śmieciowe, a to na nich opiera się trafienie w przycisk.

**Podwójne buforowanie odświeża także obszar poprzedniej klatki.** Bufor,
do którego renderer właśnie rysuje, pamięta stan sprzed dwóch klatek — to
jedyne miejsce, w którym liczba buforów przecieka do logiki, i najczęstsze
źródło „duchów" w podwójnie buforowanych interfejsach.

Każdy adapter z `gfx/adapters/` staje się wyświetlaczem przez `SurfaceDisplay`,
bez pisania osobnego sterownika.

## Warstwa deklaratywna interfejsu

**Kod ekranu nie zawiera logiki odświeżania.** Widżet dostaje wartość,
unieważnia swój obszar i wraca; renderer robi resztę. Nie ma pętli, nie ma
porównywania starych wartości z nowymi, nie ma wywołań „przerysuj teraz".

```cpp
bindings.bind<Label, sense::Sample>(tempLabel,
    [](Label& l, const sense::Sample& s) { l.setValue(s.value[0], 1, "degC"); });
```

Wiązanie robi po cichu dwie rzeczy, bez których nie działałoby poprawnie:
przenosi aktualizację z taska nadawcy do `ui.render` przez kolejkę poleceń,
oraz **scala nadmiarowe aktualizacje** — czujnik nadający 100 razy na sekundę
przy 30 klatkach nadpisuje wartość zamiast zalewać kolejkę.

**Widżety** (rozdz. 6): `Label`, `BatteryIndicator`, `SignalBars`, `Sparkline`
(wykres przebiegu z automatycznym doborem skali), `Button`, `ListView`
(lista ustawień), `Joystick` (wychylenie w promilach — tej samej jednostce,
w której HAL przyjmuje wypełnienie PWM).

**Motyw monochromatyczny nie jest ozdobnikiem.** Na panelu jednobitowym paleta
sprowadza się do dwóch barw, więc widżety odróżniają stany kształtem:
ładowanie baterii to kreska, a nie kolor; zaznaczenie na liście to obrys,
a nie wypełnienie.

**Ekran chroni sąsiadów przycinaniem** — widżet nie ma jak zamazać cudzego
obszaru, nawet gdyby rysował poza swoim prostokątem. Przesunięcie widżetu
unieważnia stare i nowe położenie, więc nie zostawia po sobie śladu.

## LVGL jako alternatywny renderer

Rozdz. 6 specyfikacji wskazuje LVGL 9 jako backend renderowania. Hydra nie
zastępuje LVGL własnym drzewem widżetów — daje mu to, czego sam nie ma:

**Panel.** Most przenosi fragmenty obrazu z bufora LVGL na dowolną powierzchnię
`gfx::ISurface`. LVGL zaczyna więc działać na **każdym** panelu, dla którego
istnieje adapter — Adafruit_GFX, TFT_eSPI, LovyanGFX, U8g2, MinisGfx — bez
pisania dla niego osobnego sterownika wyświetlacza. Także na jednobitowym,
bo konwersja formatu jest po drodze.

**Bezpieczeństwo wątkowe.** Cały LVGL żyje w tasku `ui.render`, a jedyną drogą
zmiany interfejsu spoza niego jest ta sama kolejka poleceń, co w rendererze
programowym. `lv_label_set_text` wywołany z taska czujników w trakcie
`lv_timer_handler` uszkadza drzewo obiektów i objawia się kilka klatek później,
w innym miejscu — to najczęstszy błąd w projektach z LVGL.

**Cykl życia i wejście.** Odmierzanie czasu dla animacji, kolejność kroków
pętli, karmienie urządzeń wejściowych stanem (LVGL sam rozpoznaje gesty).

```cpp
#include <hydra/ui/lvgl/LvglApi.hpp>
#include <hydra/ui/lvgl/LvglModule.hpp>

hydra::ui::lvgl::LvglModule<hydra::ui::lvgl::LvglApi> ui(display);
ui.queue().post([] { lv_label_set_text(label, "gotowe"); });
```

Moduł jest szablonem po typie cech opisującym API LVGL, więc `lvgl.h` włącza
wyłącznie `LvglApi.hpp` — pilnuje tego CI. Dzięki temu logika pomostu
(przeliczanie obszarów, konwersja formatów, kolejność kroków) jest w całości
przetestowana atrapą, mimo że sama biblioteka nie kompiluje się na hoście.

## Moduł ruchu (`hydra::motion`)

Pętla sterowania działa w tasku `motion.control` z okresem 1–5 ms, o priorytecie
czasu rzeczywistego, przypięta do rdzenia 1 — z dala od sieci i interfejsu.
Kolejność kroków w cyklu nie jest dowolna: bezpieczeństwo, pomiar, regulacja,
wyjście. Ocena bezpieczeństwa powtarza się **po** pomiarze, żeby awaria wykryta
przed chwilą zadziałała jeszcze w tym samym cyklu, a nie po następnych 5 ms.

**Regulator jest szablonem po typie liczbowym**, nie klasą na float. Testy
przepuszczają ten sam przebieg przez `Pid<float>` i `Pid<Fixed>` i porównują
wyniki — twierdzenie „ten sam kod działa na obu" ma dowód, nie deklarację.
Dwa mechanizmy odróżniają go od podręcznikowego wzoru:

- **ograniczenie całkowania** — robot dociśnięty do przeszkody nie gromadzi
  zapasu, który po jej usunięciu wystrzeliłby go do przodu,
- **pochodna z pomiaru, nie z uchybu** — skokowa zmiana zadania nie daje
  impulsu na wyjściu, więc napęd nie szarpie przy każdej nowej komendzie.

**Odometria całkuje po łuku**, nie po prostej: kierunek liczony jest w połowie
kroku. Przy jeździe po okręgu przybliżenie prostoliniowe daje błąd rzędu
kilku procent promienia — test przejeżdża ćwiartkę okręgu i sprawdza położenie
końcowe z dokładnością do 2 cm.

**Nadmierne zadanie jest skalowane, nie przycinane.** Przycięcie prędkości
liniowej i kątowej osobno zmieniłoby ich stosunek, czyli promień skrętu —
robot pojechałby w inną stronę, niż mu kazano, zamiast po prostu wolniej.

**Łańcuch bezpieczeństwa** — trzy niezależne mechanizmy: zatrzymanie awaryjne
(flaga, nie magistrala; wolno je ustawić z przerwania, kasowanie wymaga jawnej
decyzji), watchdog komend (zerwane łącze zatrzymuje pojazd; kasuje się sam)
oraz limit prądu ze zwłoką, która przepuszcza prąd rozruchowy.

Trygonometria dla odometrii ma wariant stałoprzecinkowy — RP2040 nie ma ani
FPU, ani tablic funkcji przestępnych. Przybliżenie wielomianowe daje błąd
poniżej 0,002, o rząd wielkości mniej niż niepewność poślizgu kół.

## Shell diagnostyczny

Wgląd w stan działającego urządzenia bez debuggera i bez przerywania pracy.

```
hydra> ps
task              okres iteracje spóźnienia    stos[B]
motion.control        5    24019          2       1840
sense.poll          100     1201          0       2210
net.worker           50     2402          0       1520
tasks=4

hydra> i2c scan
0x3C
0x76
found=2
clock_hz=400000

hydra> cfg set net ssid domowa
ssid=domowa
```

Wyjście ma postać `klucz=wartość` obok formy czytelnej dla człowieka — shell
jest jedynym interfejsem, przez który testy sprzętowe w CI będą sterować
urządzeniem i odczytywać wynik, więc musi dać się przetworzyć maszynowo.

Komendy rejestrują się same, więc build bez modułu sieciowego nie ma komend
sieciowych i nie kosztuje po nich ani bajta. `reboot` nie restartuje urządzenia
sam — zgłasza żądanie na magistralę, a moment wybiera warstwa, która wie,
co jest w toku.

## Zapis okoliczności awarii

Urządzenie, które się zrestartowało, zwykle nie ma komu o tym powiedzieć.
Przez reset przenoszone są trzy rzeczy: przyczyna z rejestrów procesora
(dostępna nawet wtedy, gdy oprogramowanie nie zdążyło nic zapisać), kontekst
podany jawnie przed awarią oraz ogon bufora logów. Licznik rozruchów pokazuje
pętlę restartów, która w pojedynczym logu wygląda niewinnie.

Framework **nie zbiera śladu stosu** — wymagałoby to przechwycenia procedury
obsługi wyjątku procesora, która na każdej platformie wygląda inaczej i bywa
już zajęta przez SDK vendora. Zapisywany jest kontekst podany jawnie: nazwa
taska, kod błędu, krótki opis.

## Aktualizacja przez sieć

Trzy właściwości decydują o tym, czy mechanizm nadaje się do urządzenia,
którego nikt nie odwiedzi po wgraniu wadliwej wersji:

**Weryfikacja przed przełączeniem.** Skrót liczony jest w trakcie pobierania,
a porównanie następuje przed `commit()`. Obraz uszkodzony w drodze nigdy nie
staje się aktywny — test przekłamuje jeden bajt z tysiąca i sprawdza, że
urządzenie nadal działa na starym.

**Tryb próbny po restarcie.** Nowy obraz startuje warunkowo i musi potwierdzić
sprawność. Brak potwierdzenia w zadanym czasie cofa aktualizację przy kolejnym
rozruchu. Bez tego pierwsza wersja, która wstaje i natychmiast się wywraca,
kończy wizytą z programatorem. Aktualizacja w trakcie trybu próbnego jest
odrzucana — urządzenie ma wtedy jeden znany sprawny obraz i nadpisanie go
zostawiłoby je bez żadnego.

**Pobieranie nie blokuje.** `step()` wykonuje ograniczoną porcję pracy, więc
mieści się w tasku sieciowym obok pozostałych zadań.

SHA-256 i HMAC-SHA256 są zaimplementowane w frameworku, bez zależności od
biblioteki kryptograficznej: weryfikacja aktualizacji musi działać także tam,
gdzie mbedTLS nie mieści się w budżecie pamięci. Poprawność sprawdzana jest
wektorami z FIPS 180-4 i RFC 4231. Porównanie skrótów jest odporne na pomiar
czasu — zwykłe `memcmp` zdradza, ile początkowych bajtów się zgadza.

**Ograniczenie**: HMAC używa klucza współdzielonego, więc każde urządzenie
w parku potrafi podpisać aktualizację dla pozostałych. Podpis asymetryczny
usuwa ten problem, ale wymaga biblioteki kryptograficznej.

## Budowanie na sprzęt

```bash
pio ci --project-conf platformio.ini --lib . -e esp32s3 examples/blink-task
pio ci --project-conf platformio.ini --lib . -e pico2   examples/i2c-scan

# Moduły opcjonalne włącza się flagą — przykład bez czujników nie wciąga
# ani bajta modułu sense.
PLATFORMIO_BUILD_FLAGS=-DHYDRA_ENABLE_SENSE=1 \
  pio ci --project-conf platformio.ini --lib . -e esp32s3 examples/telemetry
```

Środowiska: `esp32s3`, `esp32c3`, `pico`, `pico2`, `stm32g4`.
