// Wygenerowane przez tools/gen_bindings.py z tools/wasm_bindings.def.
// Nie edytuj ręcznie — zmiany przepadną przy regeneracji.

//
// Deklaracje importów Hydry dla modułów pisanych w AssemblyScript.
//
// Wszystkie funkcje leżą w module `hydra`. Moduł deklaruje tylko te,
// których naprawdę używa — grupa niewłączona w `BindingSet` po stronie
// urządzenia nie zostaje zlinkowana i moduł jej żądający się nie wczyta.
//
// Napisy przekazuje się parą (offset, długość): pamięć modułu jest jego
// własną przestrzenią adresową.

// --- core ----------------------------------------------------------------

/** Czas od startu urządzenia w milisekundach. */
@external("hydra", "millis")
export declare function millis(): i32;

/** Czas od startu urządzenia w mikrosekundach. */
@external("hydra", "micros")
export declare function micros(): i32;

/** Usypia task skryptu. Nie zatrzymuje reszty systemu. */
@external("hydra", "delay")
export declare function delay(ms: i32): void;

// --- log -----------------------------------------------------------------

/** Wpis do logu. Poziom 0–4 jak `LogLevel`; tekst jako (offset, długość). */
@external("hydra", "log")
export declare function log(level: i32, ptr: i32, len: i32): void;

// --- gpio ----------------------------------------------------------------

/** Tryb pinu wg `hal::PinMode`: 0 in, 1 in_pullup, 2 in_pulldown, 3 out, 4 out_od, 5 analog. Zwraca 1 przy powodzeniu. */
@external("hydra", "gpio_mode")
export declare function gpio_mode(pin: i32, mode: i32): i32;

/** Ustawia stan wyjścia. Zwraca 1 przy powodzeniu. */
@external("hydra", "gpio_write")
export declare function gpio_write(pin: i32, value: i32): i32;

/** Stan pinu: 0, 1 albo -1 przy błędzie. */
@external("hydra", "gpio_read")
export declare function gpio_read(pin: i32): i32;

/** Zmienia stan wyjścia na przeciwny. Zwraca 1 przy powodzeniu. */
@external("hydra", "gpio_toggle")
export declare function gpio_toggle(pin: i32): i32;

// --- adc -----------------------------------------------------------------

/** Surowy odczyt przetwornika; -1 przy błędzie. */
@external("hydra", "adc_raw")
export declare function adc_raw(pin: i32): i32;

/** Odczyt w miliwoltach po kalibracji; -1 przy błędzie. */
@external("hydra", "adc_mv")
export declare function adc_mv(pin: i32): i32;

// --- pwm -----------------------------------------------------------------

/** Konfiguruje kanał PWM. Zwraca 1 przy powodzeniu. */
@external("hydra", "pwm_setup")
export declare function pwm_setup(pin: i32, freqHz: i32): i32;

/** Wypełnienie w promilach (0–1000). */
@external("hydra", "pwm_duty")
export declare function pwm_duty(pin: i32, permille: i32): i32;

/** Szerokość impulsu w mikrosekundach — sterowanie serwem. */
@external("hydra", "pwm_us")
export declare function pwm_us(pin: i32, us: i32): i32;

/** Zwalnia kanał PWM. */
@external("hydra", "pwm_release")
export declare function pwm_release(pin: i32): i32;

// --- event ---------------------------------------------------------------

/** Publikuje sygnał na magistrali zdarzeń. */
@external("hydra", "event_emit")
export declare function event_emit(namePtr: i32, nameLen: i32, value: f32, data: i32): void;

/** Skrót nazwy (FNV-1a, 16 bit) — do porównań w `on_event`. */
@external("hydra", "event_name_id")
export declare function event_name_id(namePtr: i32, nameLen: i32): i32;

// --- i2c -----------------------------------------------------------------

/** 1 gdy układ odpowiada, 0 gdy nie. */
@external("hydra", "i2c_ping")
export declare function i2c_ping(bus: i32, addr: i32): i32;

/** Zapisuje znalezione adresy pod `outPtr`; zwraca ich liczbę albo -1. */
@external("hydra", "i2c_scan")
export declare function i2c_scan(bus: i32, outPtr: i32, capacity: i32): i32;

/** Odczyt rejestru do pamięci modułu; liczba bajtów albo -1. */
@external("hydra", "i2c_read")
export declare function i2c_read(bus: i32, addr: i32, reg: i32, outPtr: i32, len: i32): i32;

/** Zapis do rejestru z pamięci modułu; 1 albo -1. */
@external("hydra", "i2c_write")
export declare function i2c_write(bus: i32, addr: i32, reg: i32, dataPtr: i32, len: i32): i32;

