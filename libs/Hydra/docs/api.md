# Hydra — przegląd API

Dokument opisuje publiczną powierzchnię frameworka warstwa po warstwie. Nie
zastępuje nagłówków: każdy z nich zaczyna się komentarzem tłumaczącym, po co
dany element istnieje i jakie decyzje projektowe za nim stoją. Tu jest mapa,
tam szczegóły.

Nazewnictwo: identyfikatory po angielsku, komentarze i komunikaty po polsku.
Wszystko żyje w `namespace hydra`; podprzestrzenie odpowiadają katalogom.

---

## Zasady przekrojowe

**Błędy.** Nie ma wyjątków (`-fno-exceptions`) i nie ma kodów zwrotnych
ignorowanych po cichu. Funkcja mogąca się nie udać zwraca `Status`
(czyli `expected<void, Err>`) albo `Result<T>`:

```cpp
Result<u16> raw = adc.read(pin);
if (!raw) return fail(raw.error());
u16 value = *raw;

// albo krócej — makro propaguje błąd wyżej
HYDRA_TRY(u16 value, adc.read(pin));   // przypisuje albo zwraca błąd
HYDRA_CHECK(storage.commit());         // sprawdza Status bez wartości
```

**Pamięć.** Po `App::begin()` nie ma alokacji. Wszystkie bufory są polami
o rozmiarze znanym w czasie kompilacji, a granice — stałymi w `Config.hpp`,
które da się nadpisać flagą kompilatora.

**Czas.** Funkcje pracujące w pętli przyjmują czas argumentem
(`tick(now)`, `step(now)`). Dzięki temu backoff, limity czasu i watchdogi
testuje się w mikrosekundach zamiast czekać realne minuty.

**Moduły opcjonalne.** `HYDRA_ENABLE_SENSE`, `_NET`, `_UI`, `_MOTION`, `_OTA`.
Wyłączony moduł znika w preprocesorze — nie kosztuje ani bajta.

---

## `core` — rdzeń

| Element | Rola |
|---|---|
| `App` | cykl życia: rejestracja modułów, `begin()`, `stop()`, metryki |
| `IModule` / `ModuleBase` | podsystem z fazami `onInit` → `onStart` → `onStop` |
| `Task` | task RTOS z pomiarem terminów i zapasu stosu |
| `EventBus` | publikacja i subskrypcja zdarzeń, także z przerwania |
| `Delegate<Sig>` | wywołanie zwrotne bez alokacji |
| `expected<T,E>` | wynik albo błąd |
| `Fixed`, `real_t` | arytmetyka Q16.16 i wybór typu pętli RT |
| `Log` | logowanie z poziomami, ujścia wymienne |
| `SecretString<N>` | hasła i klucze, kasowane przy zniszczeniu |

### Uruchomienie

```cpp
App::config()
    .name("czujnik-salon")     // trafia do logów, telemetrii i mDNS
    .logLevel(LogLevel::Info)
    .logSink(gConsole)
    .add(gSense)               // kolejność determinuje kolejność startu
    .add(gNet);

if (auto r = App::begin(); !r) { /* obsługa */ }
```

`App::begin()` inicjuje moduły w kolejności rejestracji, potem uruchamia je
w tej samej kolejności. Niepowodzenie inicjacji zatrzymuje start i zwalnia to,
co już ruszyło — nie zostaje pół działającego systemu.

### Moduł własny

```cpp
class MyModule : public ModuleBase {
public:
    MyModule() : ModuleBase("my") {}
protected:
    Status onInit()  override { return ok(); }          // konfiguracja
    Status onStart() override {                          // tu ruszają taski
        Task::Cfg cfg; cfg.name = "my.tick"; cfg.prio = Prio::Normal;
        return task_.startPeriodic(cfg, 100, [this] { tick(); });
    }
    void   onStop()  override { task_.stopAndWait(); }
private:
    void tick();
    Task task_;
};
```

### Zdarzenia

```cpp
struct Reading { u8 sensorId; float value; };   // POD, ≤ 32 B

EventBus::subscribe<Reading>([](const Reading& e) { /* ... */ });
EventBus::publish(Reading{1, 21.5f});           // wprost, w kontekście wywołującego
EventBus::publishFromIsr(Reading{1, 21.5f});    // z przerwania, odłożone
```

Doręczenie bezpośrednie wykonuje subskrybenta natychmiast. Wariant kolejkowany
(`Inbox`) odkłada zdarzenie do własnego taska odbiorcy — używa się go wtedy,
gdy odbiorca robi coś powolnego i nie może zablokować nadawcy.

---

## `hal` — warstwa sprzętowa

Jedyne miejsce dotykające API Arduino. Reszta frameworka widzi wyłącznie
interfejsy, dzięki czemu ten sam kod buduje się na hoście z atrapami.

| Interfejs | Zastosowanie |
|---|---|
| `IGpio` | wejścia i wyjścia cyfrowe, przerwania |
| `II2c`, `ISpi`, `IUart` | magistrale |
| `IPwm` | wyjścia z modulacją szerokości |
| `IAdc` | pomiar napięcia |
| `IStorage` | konfiguracja trwała (NVS, LittleFS, EEPROM) |
| `ITime` | czas, opóźnienia, źródło monotoniczne |

```cpp
// Pin nazwany logicznie w pliku płytki, nie numerem w kodzie.
hal::Hal::gpio().configure(hal::board::led, hal::PinMode::Output);
hal::Hal::gpio().write(hal::board::led, true);

if (hal::Hal::hasI2c(0)) {
    u8 id = 0;
    const u8 reg = drivers::Bme280::RegChipId;
    HYDRA_CHECK(hal::Hal::i2c(0).writeRead(0x76, CByteSpan{&reg, 1},
                                           ByteSpan{&id, 1}));
}
```

Piny nazywa się logicznie w pliku płytki (`include/hydra/boards/`), nie
numerem w kodzie. Zmiana płytki to zmiana jednego `#define`.

---

## `sense` — czujniki

| Element | Rola |
|---|---|
| `ISensor` | źródło pomiarów; wynik trafia do `Sample` (do 4 kanałów) |
| `SensorHub` | odpytywanie okresowe, publikacja na magistrali |
| `FilterKind` | filtr pomiaru: mediana, EMA, Butterworth |
| `AnomalyDetector` | odrzucanie odczytów niewiarygodnych i zamrożonych |
| `Calibration` | przesunięcie i skala per kanał |
| `ImuFusion` | scalenie żyroskopu z akcelerometrem |

Sterowniki wzorcowe: `Bme280` (środowisko), `Ina219` (prąd i napięcie),
`As5600` (kąt magnetyczny).

Czujnik rejestruje się razem z opisem tego, jak ma być odpytywany
i przetwarzany — okres, filtr i granice wiarygodności w jednym miejscu:

```cpp
// Sterownik nie dostaje magistrali w konstruktorze — adres i okres są
// częścią rejestracji, więc ten sam obiekt da się przepiąć bez rekompilacji.
drivers::Bme280 bme;

SensorHub::Registration weather;
weather.sensor.periodMs     = 2000;
weather.sensor.address      = drivers::Bme280::kDefaultAddress;
weather.filter.kind         = FilterKind::Ema;      // Median, Butterworth, None
weather.filter.emaAlpha     = 0.3f;
weather.anomaly.minValue    = -40.0f;               // poza zakresem → odrzucenie
weather.anomaly.maxValue    = 85.0f;
weather.anomaly.frozenLimit = 10;                   // tyle samo odczytów z rzędu = usterka

hub.add(bme, weather);
```

Wykrywanie anomalii jest częścią rejestracji, a nie czymś dopisywanym później:
czujnik zwracający w kółko tę samą wartość albo skok o 90° w 100 ms to
typowe objawy urwanego przewodu, nie pomiar.

---

## `net` — sieć

| Element | Rola |
|---|---|
| `INetworkInterface` | Wi-Fi lub Ethernet |
| `IClient` | gniazdo TCP |
| `TlsClient<T>` | nakładka szyfrująca na gniazdo platformy |
| `ConnectionManager` | łączenie, backoff, sieci zapasowe |
| `MqttClient` | MQTT 3.1.1 |
| `TelemetryBridge` | pomiary z magistrali → tematy MQTT |
| `IMdns` | rozgłaszanie usług |

```cpp
net::NetModule::Config cfg;
cfg.mqtt.clientId    = "rover-01";
cfg.mqtt.host        = "broker.local";
cfg.mqtt.port        = 1883;
cfg.mqtt.willTopic   = "hydra/rover-01/status";   // broker rozgłosi to za nas,
cfg.mqtt.willPayload = "offline";                 // gdy urządzenie zamilknie
cfg.mqtt.willRetain  = true;
cfg.mdnsHostname     = "rover-01";
gNet.configure(cfg);

NetworkCredentials home{"SSID", Secret{"hasło"}};
gNet.connection().addNetwork(home);               // kolejność = priorytet
```

Testament (`will`) ustawia się przy konfiguracji, bo tylko wtedy ma sens:
urządzenie, które straciło zasilanie, nie ogłosi swojej nieobecności samo.

Pomiary trafiają do tematów mostkiem, bez ręcznego składania ładunków:

```cpp
net::TelemetryBridge bridge(gNet.mqtt());
// formatSample: int(const Sample&, char* out, size_t cap)
bridge.publishOn<sense::Sample>("dom/salon/temp", 0, false, formatSample);
```

Szyfrowanie włącza się podmianą gniazda — warstwa protokołu nic o tym nie wie:

```cpp
WiFiClientSecure secure;
net::TlsClient<WiFiClientSecure> tls(secure);
tls.configure({.caCertificate = kIsrgRootX1});
// Gniazdo podaje się przy tworzeniu modułu sieciowego — MqttClient
// i OtaUpdater przyjmują IClient&, więc warstwa protokołu nic nie wie
// o szyfrowaniu. Zmienia się tylko port na 8883.
```

`allowInsecure` istnieje, bo bywa niezbędny przy własnym certyfikacie
w sieci lokalnej, ale wymaga jawnego włączenia i zostawia ostrzeżenie
w logu. TLS bez weryfikacji chroni tylko przed biernym podsłuchem.

---

## `gfx` i `ui` — obraz i interfejs

Warstwa graficzna jest własna: `ISurface` z prymitywami, `Framebuffer`,
czcionki bitmapowe, `Color` z konwersją do formatów wyświetlaczy.

Bibliotekę zewnętrzną podłącza się adapterem — szablonem, który dopasowuje
się do jej typu bez włączania jej nagłówków przez Hydrę:

| Adapter | Biblioteka |
|---|---|
| `AdafruitSurface<T>` | Adafruit_GFX |
| `TftEspiSurface<T>` | TFT_eSPI |
| `LovyanSurface<T>` | LovyanGFX |
| `U8g2Surface<T>` | U8g2 (monochromatyczne) |
| `MinisGfxSurface<T>` | MinisGfx z tego repozytorium |

Interfejs użytkownika buduje się deklaratywnie z widżetów
(`Label`, `Value`, `Bar`, `Button`, `List`, `Icon`, `Sparkline`) na `Screen`,
z wiązaniem danych: widżet odświeża się, gdy zmieni się źródło.

```cpp
Screen gHome("home");
Label  gTemperature("--");

gTemperature.setBounds(gfx::Rect(4, 20, 120, 16));
gHome.add(gTemperature);

// Wiązanie: widżet aktualizuje się sam, gdy na magistrali pojawi się
// zdarzenie danego typu. Przerysowany zostaje tylko ten jeden widżet,
// nie cały ekran.
// Wiązania odświeżają widżet przez kolejkę modułu, więc aktualizacja
// z dowolnego taska nie koliduje z rysowaniem.
BindingHub gBindings(gUi.queue());

gBindings.bind<Label, sense::Sample>(
    gTemperature, [](Label& label, const sense::Sample& e) {
        label.setValue(e.first(), 1, "°C");
    });
```

Alternatywnie LVGL — przez `LvglModule`, który przejmuje rysowanie
i pracuje na tym samym backendzie wyświetlacza.

---

## `motion` — napęd

| Element | Rola |
|---|---|
| `Pid<T>` | regulator z ograniczeniem całki i wysyceniem |
| `IMotor`, `IEncoder` | siłowniki i pomiar obrotu |
| `DifferentialDrive` | kinematyka i odometria napędu różnicowego |
| `Safety` | zatrzymanie awaryjne, watchdog komendy, nadprąd |
| `MotionModule` | pętla czasu rzeczywistego 1–5 ms |

Pętla pilnuje własnego terminu i zgłasza jego przekroczenie na magistralę.
Łańcuch bezpieczeństwa jest przeliczany po pomiarze, a nie przed — inaczej
reakcja na usterkę spóźniałaby się o cykl, czyli o kilka milimetrów jazdy.

---

## `shell`, `diag`, `ota` — utrzymanie

`Shell` daje komendy `help ps top log uptime version reboot i2c gpio adc cfg hal`.
Wynik każdej pojawia się w dwóch postaciach: czytelnej i jako pary
`klucz=wartość` — tę drugą rozbiera harness testów sprzętowych
(`tools/hil_run.py`).

`CrashRecorder` zapisuje licznik uruchomień, przyczynę resetu i ogon logu
sprzed awarii do pamięci trwałej, a po starcie publikuje to na magistrali.

`OtaUpdater` pobiera wsad po HTTP, liczy skrót w locie i sprawdza go **przed**
zatwierdzeniem. Tryb próbny cofa aktualizację, jeśli nowy wsad nie potwierdzi
poprawnego startu — działa na ESP32; RP2 i STM32 wymagają wsparcia w bootloaderze.

---

## Konfiguracja kompilacji

| Symbol | Znaczenie |
|---|---|
| `HYDRA_BOARD_HEADER` | plik opisu płytki |
| `HYDRA_ENABLE_*` | włączenie modułu opcjonalnego |
| `HYDRA_LOG_LEVEL` | próg logowania wycinany w kompilacji |
| `HYDRA_FORCE_HOST` | build hostowy z atrapami |
| `HYDRA_MAX_MODULES`, `HYDRA_MAX_TASKS`, ... | granice buforów statycznych |

Pełna lista z wartościami domyślnymi: `include/hydra/core/Config.hpp`.
