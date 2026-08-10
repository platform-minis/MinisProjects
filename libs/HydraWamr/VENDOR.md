# WAMR osadzony dla Hydry

Kopia wydania **WAMR-2.4.5** z
<https://github.com/bytecodealliance/wasm-micro-runtime>, licencja Apache-2.0
z wyjątkiem LLVM. Nie edytuj tych plików — całe drzewo odtwarza
`libs/Hydra/tools/vendor_wamr.sh`.

## Dlaczego osobna biblioteka, a nie `src/` Hydry

wasm3 to 14,5 tysiąca wierszy przenośnego C99. WAMR to ~80 tysięcy i katalog na
system operacyjny. PlatformIO kompiluje całe `src/` biblioteki, więc WAMR
w Hydrze trafiałby do budowy **każdego** projektu — także tego bez skryptów.
Jako osobna biblioteka wchodzi wyłącznie tam, gdzie wpisano go w `lib_deps`.

## Zakres

Wyłącznie interpreter klasyczny. Wyłączone: AOT, JIT, GC, WASI, wątki, pamięć
dzielona. Włączone: `WASM_ENABLE_INSTRUCTION_METERING` — dzięki niemu budżet
wykonania jest w WAMR wbudowany i **nie wymaga łatki**, w przeciwieństwie do
wasm3.

## Pułapka, która kosztuje najwięcej czasu

`BH_MALLOC` i `BH_FREE` muszą wskazywać na `wasm_runtime_malloc`
i `wasm_runtime_free`. WAMR sprawdza to makrem i przerywa budowę komunikatem
„unexpected BH_MALLOC", który nie mówi, czego brakuje. Definicje są
w `library.json`.

## Stan platform

| Cel | Warstwa | Stan |
|---|---|---|
| host (macOS/Linux) | `darwin` + `common/posix` | **zweryfikowane** — 29/29 jednostek |
| ESP32 / ESP32-S3 | `esp-idf` | **nieprzetestowane** |

Warstwa ESP-IDF została skopiowana z listy plików, bez uruchomionej budowy —
w środowisku, w którym powstawało to osadzenie, nie było ani `cmake`, ani
toolchaina Espressifa. Traktuj ją jako punkt wyjścia do pierwszego
`pio run`, a nie jako rzecz gotową.

RP2040/RP2350 i STM32 zostają na wasm3: są poniżej progu 256 kB RAM, od którego
WAMR zaczyna mieć sens.
