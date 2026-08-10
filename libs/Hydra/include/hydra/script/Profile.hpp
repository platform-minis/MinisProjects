#pragma once
/**
 * Hydra — profil pamięciowy podsystemu skryptów.
 *
 * Wyłącznie dyrektywy preprocesora, bez ani jednej konstrukcji C++. To celowe:
 * nagłówek wciąga zarówno kod C++ Hydry, jak i `hydra_lua_conf.h` kompilowany
 * jako C razem ze źródłami Lua. Gdyby profil był zdefiniowany w dwóch miejscach,
 * prędzej czy później rozjechałby się i biblioteka zbudowałaby się z jednym
 * układem struktur, a jej użytkownik z drugim.
 */

#include "hydra/core/Config.hpp"

/**
 * Duży profil włącza się tam, gdzie RAM liczy się w setkach kilobajtów.
 *
 * Na RP2040/RP2350 i STM32 obowiązuje profil mały — nie dlatego, że interpreter
 * tam nie działa (działa, i to jest cały sens osadzenia oficjalnego Lua), tylko
 * dlatego, że skrypt nie ma prawa zabrać pamięci pętli sterowania. Kto ma
 * urządzenie z zapasem RAM-u i chce większych budżetów, podnosi je jedną flagą.
 */
#ifndef HYDRA_SCRIPT_LARGE_PROFILE
#  if HYDRA_PLAT_HOST || HYDRA_PLAT_ESP32
#    define HYDRA_SCRIPT_LARGE_PROFILE 1
#  else
#    define HYDRA_SCRIPT_LARGE_PROFILE 0
#  endif
#endif

/**
 * Który runtime WebAssembly wykonuje moduły.
 *
 * Wybór jest funkcją zapasu pamięci, a nie preferencji:
 *
 * | Runtime | Sam runtime | Kiedy |
 * |---|---|---|
 * | wasm3 | ~64 kB | poniżej 256 kB RAM — RP2040/RP2350, STM32 |
 * | WAMR  | 200 kB+ | powyżej; szybszy interpreter i kompilacja z wyprzedzeniem |
 *
 * Ta sama myśl, co przy `HYDRA_SCRIPT_LARGE_PROFILE`: skrypt nie ma prawa
 * zabrać pamięci pętli sterowania, więc na małej płytce dostaje mniejszy
 * runtime, a nie mniejszy budżet czasu.
 *
 * **Dziś zaimplementowany jest wyłącznie wasm3.** Stała istnieje, bo wybór
 * silnika ma być zmianą konfiguracji, a nie zmianą kodu — a próba wybrania
 * WAMR ma skończyć się błędem kompilacji z wyjaśnieniem, nie cichym
 * podstawieniem czegoś innego.
 */
#define HYDRA_WASM_ENGINE_WASM3 1
#define HYDRA_WASM_ENGINE_WAMR  2

#ifndef HYDRA_SCRIPT_WASM_ENGINE
#  define HYDRA_SCRIPT_WASM_ENGINE HYDRA_WASM_ENGINE_WASM3
#endif

/**
 * Pula pamięci silnika WebAssembly w bajtach.
 *
 * Osobna od puli Lua i większa: runtime trzyma w niej skompilowany kod modułu
 * oraz jego pamięć liniową, więc apetyt ma inny niż interpreter źródła.
 *
 * **PSRAM nie podnosi tej wartości automatycznie.** Domyślna pula jest tablicą
 * statyczną, a taka na ESP32 ląduje w RAM-ie wewnętrznym niezależnie od tego,
 * czy płytka ma pamięć zewnętrzną — podniesienie progu zjadłoby więc dokładnie
 * tę pamięć, której chcemy oszczędzić. Kto ma PSRAM i chce z niej skorzystać,
 * przydziela bufor sam i podaje go przez `ScriptModule::Config::pool`; wtedy
 * ta stała nie ma znaczenia.
 */
#ifndef HYDRA_WASM_HEAP_BYTES
#  if HYDRA_SCRIPT_LARGE_PROFILE
#    define HYDRA_WASM_HEAP_BYTES (128 * 1024)
#  else
#    define HYDRA_WASM_HEAP_BYTES (48 * 1024)
#  endif
#endif
