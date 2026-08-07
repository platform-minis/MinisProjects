# Architektura

Hydra jest warstwą pośrednią między aplikacją a sprzętem. Ten dokument opisuje,
z czego się składa, dlaczego akurat tak i co z tego wynika w codziennej pracy.

## Kształt

```
             ┌─────────────────────────────────────────┐
aplikacja    │  moduły użytkownika, ekrany, logika      │
             ├─────────────────────────────────────────┤
moduły       │  sense   net   ui   motion   ota        │  opcjonalne
opcjonalne   │                                          │
             ├─────────────────────────────────────────┤
rdzeń        │  App  Task  EventBus  Log  IModule       │  zawsze
             ├─────────────────────────────────────────┤
HAL          │  IGpio  IBus  IPwm  IAdc  IStorage  ITime│
             ├─────────────────────────────────────────┤
backend      │  Arduino / atrapy hostowe                │  wymienny
             └─────────────────────────────────────────┘
```

Pod spodem leży FreeRTOS i Arduino API, ale **kod aplikacji nie widzi ani
jednego, ani drugiego**. Ma to jeden wymierny skutek: 89% kodu frameworka jest
niezależne od platformy. Backend Arduino to 1183 z 11004 linii `src/`.

## Reguły zależności

Pięć reguł pilnowanych przez `tools/check_includes.sh` w CI:

1. Nagłówki Arduino wolno włączać wyłącznie w `src/*/arduino/`.
2. HAL sięga tylko po podstawy rdzenia — nigdy po `App`, `EventBus`, `IModule`,
   `Log`, `Task`.
3. FreeRTOS występuje wyłącznie w `src/core/rtos_freertos.cpp`.
4. `lvgl.h` wolno włączyć tylko w `include/hydra/ui/lvgl/LvglApi.hpp`.
5. `ArduinoBackend.hpp` nie przecieka do `include/` ani do `examples/`.

Każda z nich była kiedyś złamana albo bliska złamania. Reguła bez sprawdzenia
w CI jest życzeniem, nie regułą — dlatego skrypt jest uruchamiany przy każdej
zmianie i każda reguła ma test, że **wykrywa** naruszenie.

Kod użytkownika (`examples/`, `templates/`, `projects/`) jest z reguły 1
wyłączony: to, co pisze się na frameworku, może sięgać po Arduino wprost.

## Decyzje, które widać wszędzie

### Brak wyjątków i RTTI

Kompilacja idzie z `-fno-exceptions -fno-rtti`. Błędy propaguje
`expected<T, Err>`:

```cpp
HYDRA_TRY(u16 value, adc.read(pin));   // przypisuje albo zwraca błąd wyżej
HYDRA_CHECK(storage.commit());          // sprawdza Status bez wartości
```

Powód jest praktyczny: wyjątki na MCU kosztują kilkanaście kilobajtów kodu
i wprowadzają ścieżkę wykonania, której nie widać w źródle — a na pętli
czasu rzeczywistego liczy się i jedno, i drugie.

### Brak alokacji po starcie

Po `App::begin()` nic się nie alokuje. Wszystkie bufory są polami o rozmiarze
znanym w czasie kompilacji, a granice — stałymi w `Config.hpp`, które da się
nadpisać flagą. Fragmentacja sterty na urządzeniu chodzącym miesiącami jest
awarią, której nie da się odtworzyć przy biurku.

### Czas jako argument

Funkcje pracujące w pętli przyjmują czas argumentem: `tick(now)`, `step(now)`.
Dzięki temu backoff połączenia, limity czasu i watchdogi testuje się
w mikrosekundach zamiast czekać realne minuty. Ta jedna decyzja przesądziła
o tym, że 428 testów rdzenia wykonuje się poniżej sekundy.

### Moduły opcjonalne wycinane w preprocesorze

`HYDRA_ENABLE_SENSE`, `_NET`, `_UI`, `_MOTION`, `_OTA`. Wyłączony moduł nie
kosztuje ani bajta — nie ma go w binarce, nie ma go w czasie kompilacji.

### Jeden typ liczbowy, dwie arytmetyki

`real_t` to `float` tam, gdzie jest jednostka zmiennoprzecinkowa, i `Fixed`
(Q16.16) na RP2040. Kod regulatorów jest **identyczny** w obu przypadkach —
zmienia się alias, nie algorytm.

## Cykl życia

```
App::config().add(moduł).add(moduł);
App::begin()
   ├─ onInit()  dla każdego modułu, w kolejności rejestracji
   └─ onStart() dla każdego modułu, w tej samej kolejności
```

Niepowodzenie inicjacji zatrzymuje start i zwalnia to, co już ruszyło — nie
zostaje pół działającego systemu. Kolejność rejestracji jest kolejnością
uruchamiania, więc zależności między modułami wyraża się kolejnością, a nie
ukrytym mechanizmem.

Na STM32 `App::begin()` uruchamia scheduler i **nigdy nie wraca** — `loop()`
jest tam martwy z definicji. Na ESP32 i RP2 scheduler działa od resetu.

## Magistrala zdarzeń

Moduły nie wołają się nawzajem. Publikują zdarzenia:

```cpp
EventBus::subscribe<Reading>([](const Reading& e) { … });
EventBus::publish(Reading{1, 21.5f});      // wprost, w kontekście wywołującego
EventBus::publishFromIsr(Reading{1, 21.5f});  // z przerwania, odłożone
```

Zdarzenia są typami POD do 32 bajtów. Doręczenie bezpośrednie wykonuje
subskrybenta natychmiast; wariant kolejkowany odkłada je do taska odbiorcy —
używa się go, gdy odbiorca robi coś powolnego i nie może zablokować nadawcy.

Publikacja z przerwania nigdy nie wykonuje subskrybenta w przerwaniu: zdarzenie
trafia do kolejki i jest doręczane przez task `core.house`. ISR ma być krótki.

## Taski

| Task | Priorytet | Okres | Rdzeń |
|---|---|---|---|
| `motion.control` | Realtime | 1–5 ms | 1 |
| `sense.poll` | High | zależny od czujników | 1 |
| `net.worker` | Normal | zdarzeniowy | 0 |
| `ui.render` | Low | 30 Hz | 0 |
| `core.house` | Idle | 1 s | dowolny |

Każdy task mierzy własny termin i zapas stosu. Przekroczenie terminu jest
publikowane jako zdarzenie — nie jako cisza. Po ustalonej liczbie spóźnień
z rzędu framework przestaje udawać, że jest dobrze.

Pinowanie do rdzeni działa na ESP32 i RP2350; na układach jednordzeniowych
jest ignorowane, a API pozostaje to samo.

## Testowalność jako założenie, nie dodatek

Każda warstwa ma atrapę: HAL, sieć, interfejs, napęd, magazyn wsadu. Dzięki
temu cały framework — łącznie z protokołem MQTT, potokiem renderowania
i łańcuchem bezpieczeństwa napędu — działa na maszynie deweloperskiej pod
sanitizerami.

To nie jest wygoda. To jedyny sposób, żeby błąd taki jak „reakcja na usterkę
enkodera spóźnia się o jeden cykl" w ogóle dało się zauważyć: na sprzęcie
oznacza kilka milimetrów jazdy, na hoście — nieprzechodzący test.
