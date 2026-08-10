/**
 * Hydra — alokacja pamięci dla osadzonego wasm3.
 *
 * Nagłówek w czystym C, bo włącza go `src/wasm3/m3_core.c`, kompilowane jako C.
 * Implementacja jest w C++ (`src/script/WasmAlloc.cpp`) i stoi na `script::Heap`
 * — tej samej klasie, która obsługuje pulę interpretera Lua.
 *
 * **Dlaczego nie tryb własny wasm3.** wasm3 ma `d_m3FixedHeap`, ale to alokator
 * przyrostowy zwalniający wyłącznie ostatni przydział. `ScriptModule::reload()`
 * zamyka i otwiera silnik przy każdej aktualizacji skryptu, więc pula topniałaby
 * z każdą kolejną — do wyczerpania. `Heap` scala wolne bloki w obie strony
 * i mierzy zużycie co do bajta, czego wymaga reguła „po `App::begin()` nie
 * sięgamy po stertę systemową".
 *
 * **Jedna pula naraz.** wasm3 alokuje przez funkcje bez kontekstu — nie ma
 * gdzie przekazać wskaźnika na stertę, tak jak robi to `lua_Alloc` przez pole
 * użytkownika. Aktywną pulę ustawia więc `WasmEngine::open()` i zdejmuje
 * `close()`. To samo ograniczenie, co przy domyślnej puli `Interp`: dwa
 * silniki WASM naraz wymagają, żeby każdy dostał własną pulę i żeby nie
 * otwierały się jednocześnie.
 */

#ifndef HYDRA_SCRIPT_WASM_ALLOC_H
#define HYDRA_SCRIPT_WASM_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Przydział wyzerowany, jak `calloc` — wasm3 tego oczekuje. */
void* hydraWasm3Alloc(size_t bytes);

void  hydraWasm3Free(void* ptr);

/**
 * Zmiana rozmiaru. Kolejność argumentów jak w `Heap::reallocate()`:
 * najpierw stary rozmiar, potem nowy. Nadmiar jest zerowany.
 */
void* hydraWasm3Realloc(void* ptr, size_t oldBytes, size_t newBytes);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // HYDRA_SCRIPT_WASM_ALLOC_H
