# wasm3 osadzony w Hydrze

Kopia wydania **v0.5.0** z <https://github.com/wasm3/wasm3>, licencja MIT.
Nie edytuj tych plików ręcznie — całe drzewo odtwarza `tools/vendor_wasm3.sh`,
a zmiany nanoszone poza skryptem zniknie przy najbliższej aktualizacji.

## Czego tu nie ma

- `m3_api_wasi.c`
- `m3_api_uvwasi.c`
- `m3_api_libc.c`
- `m3_api_meta_wasi.c`
- `m3_api_tracer.c`

Powody wypisane są w nagłówku skryptu vendorującego. Krótko: to interfejsy
do systemu operacyjnego, a urządzenie dostaje dostęp do świata przez bindingi
Hydry.

## Łatka Hydry — budżet wykonania

wasm3 nie ma licznika instrukcji ani sposobu przerwania wykonania, a
`ScriptModule` obiecuje, że skrypt nie zawiesi urządzenia. Łatka dokłada
licznik w `M3Runtime` i pobiera z niego w czterech miejscach:

| Miejsce | Po co |
|---|---|
| `op_ContinueLoop` | krawędź wsteczna pętli — główna droga do wykonania bez końca |
| `op_ContinueLoopIf` | jw., wariant warunkowy |
| `op_Call` | rekurencja |
| `op_CallIndirect` | rekurencja przez tablicę funkcji |

Licznika **nie ma** w dyspozycji każdej operacji, bo to najgorętsza ścieżka
interpretera. Miejsce wskazali autorzy wasm3 — komentarz w `op_ContinueLoop`
mówi wprost, że to jest punkt, w którym wykonanie może „uciec" do klienta.

Konsekwencja: budżet liczy krawędzie pętli i wywołania, a nie instrukcje. Kod
prostoliniowy jest skończony z definicji, więc obietnica jest dotrzymana —
zmienia się jednostka budżetu. Opisane w `WasmEngine.hpp`.

API: `m3_SetFuel(runtime, n)`, `m3_GetFuel(runtime)`. Zero znosi ograniczenie.
Wyczerpanie kończy wykonanie błędem `m3Err_trapOutOfFuel`.

## Aktualizacja

    tools/vendor_wasm3.sh            # pobierz nowe wydanie i załataj
    tools/vendor_wasm3.sh --check    # sprawdź spójność osadzonego drzewa

Skrypt przerywa, gdy któraś łatka nie znajdzie swojej kotwicy — wtedy trzeba
poprawić kotwicę w skrypcie, a nie plik tutaj.
