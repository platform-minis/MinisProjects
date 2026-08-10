#pragma once
/*
 * Hydra — ustawienia wasm3.
 *
 * Włączane z m3_config.h (patrz łatka w tools/vendor_wasm3.sh), żeby wartości
 * nie musiały jechać przez flagi kompilacji — inaczej każdy z trzech systemów
 * budowania (PlatformIO, CMake, Makefile testów) musiałby je powtarzać, a jeden
 * pominięty przełącznik daje inny układ struktur w różnych jednostkach
 * translacji i awarię, która nie wygląda na swoją przyczynę.
 */

/*
 * Strona kodu: 32 KB w oryginale, 4 KB u nas.
 *
 * wasm3 przydziela strony kodu w trakcie kompilacji modułu. Domyślne 32 KB to
 * rozsądna wartość na PC, ale na układzie z 320 KB RAM oznacza, że najmniejszy
 * moduł zabiera dziesiątą część pamięci, zanim cokolwiek zrobi.
 */
#define d_m3CodePageAlignSize 4096

/*
 * Wysokość stosu funkcji. Oryginalne 2000 slotów to 16 KB przy 64-bitowych
 * slotach; 512 wystarcza na logikę sterującą, o którą tu chodzi, i przycina
 * to do czterech kilobajtów.
 */
#define d_m3MaxFunctionStackHeight 512

/*
 * Alokacje kierowane na pulę Hydry (łatka 2 w vendor_wasm3.sh).
 *
 * Definicja stoi tutaj, a nie we flagach budowania, i to jest ten sam powód
 * co wyżej: pominięcie jej w jednym z trzech systemów dałoby część jednostek
 * translacji wołających `malloc()`, a część pulę — czyli zwalnianie wskaźnika
 * do cudzego alokatora.
 */
#define HYDRA_WASM3_ALLOC 1

/* Diagnostyka śledzenia wywołań — narzędzie do pracy nad wasm3, nie nad grą. */
#define d_m3EnableStrace 0

/* Rozszerzenia, których nie używamy; każde to kod w obrazie. */
#define d_m3HasWASI 0
#define d_m3HasTracer 0
