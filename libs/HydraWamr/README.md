# HydraWamr

WebAssembly Micro Runtime (WAMR) w konfiguracji dla Hydry, spakowany jako
osobna biblioteka PlatformIO.

Osadzone drzewo odtwarza `libs/Hydra/tools/vendor_wamr.sh` — nie edytuj plików
w `src/` ręcznie. Powody wyborów i pułapki: [`VENDOR.md`](VENDOR.md).

## Po co osobno

wasm3 mieszka wprost w `libs/Hydra/src/wasm3/`, bo to 14,5 tysiąca wierszy
przenośnego C99. WAMR ma ~80 tysięcy i katalog na system operacyjny.
PlatformIO kompiluje całe `src/` biblioteki, więc WAMR w Hydrze trafiałby do
budowy **każdego** projektu, także tego bez skryptów.

Tutaj wchodzi wyłącznie tam, gdzie ktoś go zażąda:

```ini
lib_deps =
    Hydra
    HydraWamr
build_flags =
    -D HYDRA_ENABLE_SCRIPT=1
    -D HYDRA_SCRIPT_ENGINE_WASM=1
    -D HYDRA_SCRIPT_WASM_ENGINE=HYDRA_WASM_ENGINE_WAMR
```

## Kiedy WAMR, a kiedy wasm3

| Runtime | Sam runtime | Kiedy |
|---|---|---|
| wasm3 | ~64 kB | poniżej 256 kB RAM — RP2040/RP2350, STM32 |
| WAMR | 200 kB+ | powyżej; szybszy interpreter, wbudowany licznik instrukcji |

Wybór robi `HYDRA_SCRIPT_WASM_ENGINE` w `hydra/script/Profile.hpp`.

## Stan

| Cel | Warstwa | Stan |
|---|---|---|
| host (macOS/Linux) | `darwin` + `common/posix` | **zweryfikowane** — 29/29 jednostek, 351 symboli |
| ESP32 / ESP32-S3 | `esp-idf` | **nieprzetestowane** |

Warstwa ESP-IDF powstała z listy plików, bez uruchomionej budowy — w środowisku,
w którym to osadzenie powstawało, nie było toolchaina Espressifa. Pierwsze
`pio run` dla ESP32 należy traktować jako część pracy, nie jako formalność.
