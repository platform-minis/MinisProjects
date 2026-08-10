<!-- Wygenerowane przez tools/gen_bindings.py. Nie edytuj ręcznie. -->

# Powierzchnia importów WebAssembly

Wszystko, co moduł WebAssembly widzi z urządzenia. Lista powstaje
z `tools/wasm_bindings.def` — tego samego pliku, z którego generują się
tablice w C++ i deklaracje dla AssemblyScript, więc nie ma jak się
rozjechać.

Grupa niewłączona w `BindingSet` nie zostaje zlinkowana. Moduł, który jej
żąda, **nie wczyta się** — z jasnym błędem, zamiast działać połowicznie.

## `core`

| Funkcja | Sygnatura AssemblyScript | Opis |
|---|---|---|
| `millis` | `millis(): i32` | Czas od startu urządzenia w milisekundach. |
| `micros` | `micros(): i32` | Czas od startu urządzenia w mikrosekundach. |
| `delay` | `delay(ms: i32): void` | Usypia task skryptu. Nie zatrzymuje reszty systemu. |

## `log`

| Funkcja | Sygnatura AssemblyScript | Opis |
|---|---|---|
| `log` | `log(level: i32, ptr: i32, len: i32): void` | Wpis do logu. Poziom 0–4 jak `LogLevel`; tekst jako (offset, długość). |

## `gpio`

| Funkcja | Sygnatura AssemblyScript | Opis |
|---|---|---|
| `gpio_mode` | `gpio_mode(pin: i32, mode: i32): i32` | Tryb pinu wg `hal::PinMode`: 0 in, 1 in_pullup, 2 in_pulldown, 3 out, 4 out_od, 5 analog. Zwraca 1 przy powodzeniu. |
| `gpio_write` | `gpio_write(pin: i32, value: i32): i32` | Ustawia stan wyjścia. Zwraca 1 przy powodzeniu. |
| `gpio_read` | `gpio_read(pin: i32): i32` | Stan pinu: 0, 1 albo -1 przy błędzie. |
| `gpio_toggle` | `gpio_toggle(pin: i32): i32` | Zmienia stan wyjścia na przeciwny. Zwraca 1 przy powodzeniu. |

## `adc`

| Funkcja | Sygnatura AssemblyScript | Opis |
|---|---|---|
| `adc_raw` | `adc_raw(pin: i32): i32` | Surowy odczyt przetwornika; -1 przy błędzie. |
| `adc_mv` | `adc_mv(pin: i32): i32` | Odczyt w miliwoltach po kalibracji; -1 przy błędzie. |

## `pwm`

| Funkcja | Sygnatura AssemblyScript | Opis |
|---|---|---|
| `pwm_setup` | `pwm_setup(pin: i32, freqHz: i32): i32` | Konfiguruje kanał PWM. Zwraca 1 przy powodzeniu. |
| `pwm_duty` | `pwm_duty(pin: i32, permille: i32): i32` | Wypełnienie w promilach (0–1000). |
| `pwm_us` | `pwm_us(pin: i32, us: i32): i32` | Szerokość impulsu w mikrosekundach — sterowanie serwem. |
| `pwm_release` | `pwm_release(pin: i32): i32` | Zwalnia kanał PWM. |

## `event`

| Funkcja | Sygnatura AssemblyScript | Opis |
|---|---|---|
| `event_emit` | `event_emit(namePtr: i32, nameLen: i32, value: f32, data: i32): void` | Publikuje sygnał na magistrali zdarzeń. |
| `event_name_id` | `event_name_id(namePtr: i32, nameLen: i32): i32` | Skrót nazwy (FNV-1a, 16 bit) — do porównań w `on_event`. |

## `i2c`

| Funkcja | Sygnatura AssemblyScript | Opis |
|---|---|---|
| `i2c_ping` | `i2c_ping(bus: i32, addr: i32): i32` | 1 gdy układ odpowiada, 0 gdy nie. |
| `i2c_scan` | `i2c_scan(bus: i32, outPtr: i32, capacity: i32): i32` | Zapisuje znalezione adresy pod `outPtr`; zwraca ich liczbę albo -1. |
| `i2c_read` | `i2c_read(bus: i32, addr: i32, reg: i32, outPtr: i32, len: i32): i32` | Odczyt rejestru do pamięci modułu; liczba bajtów albo -1. |
| `i2c_write` | `i2c_write(bus: i32, addr: i32, reg: i32, dataPtr: i32, len: i32): i32` | Zapis do rejestru z pamięci modułu; 1 albo -1. |

## Eksporty, których host szuka w module

| Eksport | Sygnatura | Kiedy wołany |
|---|---|---|
| `setup` | `(): void` | raz, po wczytaniu modułu |
| `loop` | `(): void` | w każdym przebiegu taska, z budżetem |
| `on_event` | `(nameId: i32, value: f32, data: i32): void` | dla każdego sygnału z magistrali |

Wszystkie trzy są opcjonalne. `on_event` zastępuje `hydra.event.on`
z Lua: WebAssembly nie ma domknięć, które dałoby się zarejestrować
w tabeli, więc odbiór jest eksportem, a nie wywołaniem zwrotnym.
