#!/usr/bin/env bash
#
# Hydra — pobranie i osadzenie źródeł wasm3 w src/wasm3/.
#
# Ten sam zamysł, co `vendor_lua.sh`: „aktualizacja wasm3" ma być poleceniem,
# a nie wieczorem z diffem. Osadzone drzewo jest kopią wydania z GitHuba,
# zmienioną w dokładnie czterech miejscach opisanych niżej. Wszystkie zmiany
# nakłada ten skrypt i każda ma kotwicę sprawdzaną co do liczby trafień, więc
# podniesienie wersji sprowadza się do zmiany WASM3_VERSION i sprawdzenia,
# czy łatki nadal się nakładają. Jeśli nie — skrypt przerywa z nazwą łatki,
# zamiast po cichu wydać drzewo bez budżetu wykonania.
#
# Świadomie NIE jest to submoduł gitowy — z tego samego powodu, co przy Lua:
# build musi działać po zwykłym `git clone`, a PlatformIO kompiluje wszystko,
# co leży pod src/.
#
#   tools/vendor_wasm3.sh            — pobierz, zweryfikuj, osadź, załataj
#   tools/vendor_wasm3.sh --check    — sprawdź, czy osadzone drzewo jest spójne
#
# Wymaga: curl, tar, python3, sha512sum (lub shasum -a 512).
#
# ---------------------------------------------------------------------------
# Łatka Hydry: budżet wykonania
# ---------------------------------------------------------------------------
#
# wasm3 nie ma licznika instrukcji ani żadnego sposobu przerwania wykonania —
# a `ScriptModule` obiecuje, że skrypt nie zawiesi urządzenia. Bez tej łatki
# `while (1) {}` w module kładzie task na zawsze.
#
# Licznik NIE siedzi w dyspozycji każdej operacji, bo to najgorętsza ścieżka
# interpretera. Siedzi w miejscu, które wskazali sami autorzy wasm3 — komentarz
# w `op_ContinueLoop` mówi wprost: „this is where execution can escape the M3
# code and callback to the client / fiber switch". Do tego dochodzą `Call`
# i `CallIndirect`, bo rekurencja jest drugą drogą do wykonania bez końca.
#
# Konsekwencja dla semantyki: budżet WASM liczy **krawędzie wsteczne pętli
# i wywołania**, a nie instrukcje. Kod prostoliniowy jest skończony z definicji
# — ogranicza go rozmiar modułu — więc obietnica „skrypt nie zawiesi
# urządzenia" jest dotrzymana; zmienia się tylko jednostka budżetu. Jest to
# opisane w `WasmEngine.hpp` i widoczne w `IScriptEngine::JobState`.

set -euo pipefail

WASM3_VERSION="0.5.0"
# Suma kontrolna archiwum wydania z GitHuba. GitHub generuje tarballe
# deterministycznie dla tagów, więc ta wartość jest stabilna.
WASM3_SHA512="87d29f942ef9a93faeb4085f1fc7ae8c274a7bd528ccaaf56a273815c524e43c96635acd6b59ee607d5d0c383c1d1358d09080a0d1a23cf1dcaedcab6c56d39e"
WASM3_URL="https://github.com/wasm3/wasm3/archive/refs/tags/v${WASM3_VERSION}.tar.gz"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/src/wasm3"

# Rdzeń: parser modułu, kompilator do kodu wątkowanego, interpreter, środowisko.
CORE_UNITS="m3_bind m3_code m3_compile m3_core m3_emit m3_env m3_exec
            m3_function m3_info m3_module m3_optimize m3_parse"

# Nagłówki potrzebne rdzeniowi.
HEADERS="m3_api_defs.h m3_api_libc.h m3_api_tracer.h m3_api_wasi.h m3_bind.h
         m3_code.h m3_compile.h m3_config.h m3_config_platforms.h m3_core.h
         m3_emit.h m3_env.h m3_exception.h m3_exec.h m3_exec_defs.h
         m3_function.h m3_info.h m3_math_utils.h wasm3.h wasm3_defs.h"

# Czego nie osadzamy i dlaczego:
#   m3_api_wasi.c      — WASI to system operacyjny: pliki, zegar, argv.
#                        Urządzenie dostaje dostęp do świata przez bindingi
#                        Hydry, a nie przez interfejs napisany dla serwerów.
#   m3_api_uvwasi.c    — jw., dodatkowo z zależnością od libuv.
#   m3_api_libc.c      — printf i malloc modułu; obie rzeczy Hydra ma własne.
#   m3_api_meta_wasi.c — jw.
#   m3_api_tracer.c    — zapis śladu wykonania do pliku.
#   extra/, extensions/ — narzędzia poboczne i eksperymenty.
EXCLUDED="m3_api_wasi.c m3_api_uvwasi.c m3_api_libc.c m3_api_meta_wasi.c m3_api_tracer.c"

RED=$'\033[31m'; GREEN=$'\033[32m'; DIM=$'\033[2m'; OFF=$'\033[0m'

die()  { printf '%s✗ %s%s\n' "$RED" "$1" "$OFF" >&2; exit 1; }
note() { printf '%s✓%s %s\n' "$GREEN" "$OFF" "$1"; }

sha512of() {
    if command -v sha512sum >/dev/null 2>&1; then sha512sum "$1" | cut -d' ' -f1
    else shasum -a 512 "$1" | cut -d' ' -f1
    fi
}

# ---------------------------------------------------------------------------
# Łatki
# ---------------------------------------------------------------------------

apply_patches() {
    local dir="$1"
    python3 - "$dir" <<'PYEOF'
import sys, pathlib

root = pathlib.Path(sys.argv[1])
applied = []

def patch(relpath, anchor, replacement, label):
    """Podmienia dokładnie jedno wystąpienie kotwicy. Inaczej przerywa."""
    path = root / relpath
    text = path.read_text()
    count = text.count(anchor)
    if count != 1:
        sys.exit(f"lataka '{label}': kotwica w {relpath} wystapila {count} razy, oczekiwano 1")
    path.write_text(text.replace(anchor, replacement, 1))
    applied.append(label)

# --- 1. Stała błędu i publiczne API budżetu -------------------------------
patch(
    "wasm3.h",
    'd_m3ErrorConst  (trapStackOverflow,             "[trap] stack overflow")',
    'd_m3ErrorConst  (trapStackOverflow,             "[trap] stack overflow")\n'
    '// Hydra: budzet wykonania wyczerpany. Patrz tools/vendor_wasm3.sh.\n'
    'd_m3ErrorConst  (trapOutOfFuel,                 "[trap] out of fuel")',
    "stala trapOutOfFuel",
)

patch(
    "wasm3.h",
    "    void *              m3_GetUserData              (IM3Runtime             i_runtime);",
    "    void *              m3_GetUserData              (IM3Runtime             i_runtime);\n"
    "\n"
    "    // Hydra: budzet wykonania. Zero znosi ograniczenie. Liczone sa krawedzie\n"
    "    // wsteczne petli oraz wywolania funkcji — patrz tools/vendor_wasm3.sh.\n"
    "    void                m3_SetFuel                  (IM3Runtime             i_runtime,\n"
    "                                                     uint32_t               i_fuel);\n"
    "    uint32_t            m3_GetFuel                  (IM3Runtime             i_runtime);",
    "deklaracje m3_SetFuel/m3_GetFuel",
)

# --- 2. Pole licznika w M3Runtime -----------------------------------------
patch(
    "m3_env.h",
    "    IM3Function             lastCalled;     // last function that successfully executed",
    "    IM3Function             lastCalled;     // last function that successfully executed\n"
    "\n"
    "    // Hydra: pozostaly budzet wykonania; 0 = bez ograniczenia.\n"
    "    u32                     fuel;",
    "pole fuel w M3Runtime",
)

# --- 3. Definicje API ------------------------------------------------------
patch(
    "m3_env.c",
    "void *  m3_GetUserData  (IM3Runtime i_runtime)\n"
    "{\n"
    "    return i_runtime ? i_runtime->userdata : NULL;\n"
    "}",
    "void *  m3_GetUserData  (IM3Runtime i_runtime)\n"
    "{\n"
    "    return i_runtime ? i_runtime->userdata : NULL;\n"
    "}\n"
    "\n"
    "\n"
    "// Hydra: budzet wykonania.\n"
    "void  m3_SetFuel  (IM3Runtime i_runtime, u32 i_fuel)\n"
    "{\n"
    "    if (i_runtime) i_runtime->fuel = i_fuel;\n"
    "}\n"
    "\n"
    "u32  m3_GetFuel  (IM3Runtime i_runtime)\n"
    "{\n"
    "    return i_runtime ? i_runtime->fuel : 0;\n"
    "}",
    "definicje m3_SetFuel/m3_GetFuel",
)

# --- 4. Pobór budżetu w interpreterze --------------------------------------
# Makro stawiamy po definicji newTrap, bo z niego korzysta.
patch(
    "m3_exec.h",
    "#if d_m3EnableStrace == 1",
    "// Hydra: pobor budzetu wykonania. Zero w polu `fuel` znosi ograniczenie.\n"
    "// Wywolywane wylacznie tam, gdzie moze powstac wykonanie bez konca:\n"
    "// na krawedzi wstecznej petli i przy wywolaniu funkcji.\n"
    "#define d_m3HydraSpendFuel()                                                \\\n"
    "    do {                                                                    \\\n"
    "        IM3Runtime _hydraRt = m3MemRuntime (_mem);                          \\\n"
    "        if (_hydraRt->fuel) {                                               \\\n"
    "            if (M3_UNLIKELY (--_hydraRt->fuel == 0))                        \\\n"
    "                newTrap (m3Err_trapOutOfFuel);                              \\\n"
    "        }                                                                   \\\n"
    "    } while (0)\n"
    "\n"
    "#if d_m3EnableStrace == 1",
    "makro d_m3HydraSpendFuel",
)

patch(
    "m3_exec.h",
    '    // TODO: this is where execution can "escape" the M3 code and callback to the client / fiber switch',
    "    d_m3HydraSpendFuel ();\n"
    "\n"
    '    // TODO: this is where execution can "escape" the M3 code and callback to the client / fiber switch',
    "pobor budzetu w ContinueLoop",
)

patch(
    "m3_exec.h",
    "    i32 condition = (i32) _r0;\n"
    "    void * loopId = immediate (void *);\n"
    "\n"
    "    if (condition)\n"
    "    {\n"
    "        return loopId;",
    "    i32 condition = (i32) _r0;\n"
    "    void * loopId = immediate (void *);\n"
    "\n"
    "    if (condition)\n"
    "    {\n"
    "        d_m3HydraSpendFuel ();\n"
    "        return loopId;",
    "pobor budzetu w ContinueLoopIf",
)

patch(
    "m3_exec.h",
    "    m3ret_t r = Call (callPC, sp, _mem, d_m3OpDefaultArgs);",
    "    d_m3HydraSpendFuel ();\n"
    "\n"
    "    m3ret_t r = Call (callPC, sp, _mem, d_m3OpDefaultArgs);",
    "pobor budzetu w Call",
)

patch(
    "m3_exec.h",
    "    u32 tableIndex              = slot (u32);\n"
    "    IM3Module module            = immediate (IM3Module);",
    "    d_m3HydraSpendFuel ();\n"
    "\n"
    "    u32 tableIndex              = slot (u32);\n"
    "    IM3Module module            = immediate (IM3Module);",
    "pobor budzetu w CallIndirect",
)

# --- 5. Alokacja przez pulę Hydry -----------------------------------------
# wasm3 alokuje przy wczytywaniu modułu, a `ScriptModule::reload()` robi to
# w czasie pracy urządzenia — czyli dokładnie tam, gdzie regułą Hydry jest
# „po App::begin() nie sięgamy po stertę systemową". Własny tryb wasm3
# (d_m3FixedHeap) nie wystarcza: zwalnia wyłącznie ostatni przydział, więc
# pula topniałaby z każdą aktualizacją skryptu.
patch(
    "m3_core.c",
    "#if d_m3FixedHeap\n"
    "\n"
    "static u8 fixedHeap[d_m3FixedHeap];",
    "#if d_m3HydraHeap\n"
    "\n"
    "// Hydra: alokacja przez script::Heap — statyczna pula z realnym scalaniem\n"
    "// wolnych bloków i licznikami zużycia. Patrz include/hydra/script/WasmAlloc.h.\n"
    "void *  m3_Malloc  (size_t i_size)\n"
    "{\n"
    "    return hydraWasm3Alloc (i_size);\n"
    "}\n"
    "\n"
    "void  m3_FreeImpl  (void * i_ptr)\n"
    "{\n"
    "    hydraWasm3Free (i_ptr);\n"
    "}\n"
    "\n"
    "void *  m3_Realloc  (void * i_ptr, size_t i_newSize, size_t i_oldSize)\n"
    "{\n"
    "    return hydraWasm3Realloc (i_ptr, i_oldSize, i_newSize);\n"
    "}\n"
    "\n"
    "#elif d_m3FixedHeap\n"
    "\n"
    "static u8 fixedHeap[d_m3FixedHeap];",
    "alokacja przez pule Hydry",
)

patch(
    "m3_core.c",
    '#include "m3_env.h"',
    '#include "m3_env.h"\n'
    '#include "hydra/script/WasmAlloc.h"',
    "wlaczenie WasmAlloc.h",
)

patch(
    "m3_config.h",
    "# ifndef d_m3FixedHeap\n"
    "#   define d_m3FixedHeap                        false",
    "// Hydra: alokacja przez script::Heap zamiast systemowego calloc.\n"
    "# ifndef d_m3HydraHeap\n"
    "#   define d_m3HydraHeap                        false\n"
    "# endif\n"
    "\n"
    "# ifndef d_m3FixedHeap\n"
    "#   define d_m3FixedHeap                        false",
    "flaga d_m3HydraHeap",
)

for label in applied:
    print(f"    {label}")
PYEOF
}

# ---------------------------------------------------------------------------
# Tryb --check
# ---------------------------------------------------------------------------

if [[ "${1:-}" == "--check" ]]; then
    [[ -d "$DEST" ]] || die "brak katalogu $DEST — uruchom skrypt bez --check"

    missing=0
    for unit in $CORE_UNITS; do
        [[ -f "$DEST/$unit.c" ]] || { printf '  brak %s.c\n' "$unit"; missing=1; }
    done
    for header in $HEADERS; do
        [[ -f "$DEST/$header" ]] || { printf '  brak %s\n' "$header"; missing=1; }
    done
    (( missing == 0 )) || die "osadzone drzewo jest niekompletne"

    for marker in "m3Err_trapOutOfFuel" "d_m3HydraSpendFuel" "m3_SetFuel" \
                  "hydraWasm3Alloc" "d_m3HydraHeap"; do
        grep -rq "$marker" "$DEST" || die "brak łatki Hydry: $marker"
    done
    for f in "$DEST"/*.c; do
        base="$(basename "$f")"
        for skip in $EXCLUDED; do
            [[ "$base" == "$skip" ]] && die "osadzono wykluczoną jednostkę: $base"
        done
    done

    note "osadzone drzewo wasm3 $WASM3_VERSION jest spójne i załatane"
    exit 0
fi

# ---------------------------------------------------------------------------
# Pobranie i osadzenie
# ---------------------------------------------------------------------------

command -v curl >/dev/null 2>&1 || die "wymagany curl"
command -v python3 >/dev/null 2>&1 || die "wymagany python3"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

printf '%sPobieram%s %s\n' "$DIM" "$OFF" "$WASM3_URL"
curl -sSfL -o "$WORK/wasm3.tar.gz" "$WASM3_URL" || die "pobranie nie powiodło się"

got="$(sha512of "$WORK/wasm3.tar.gz")"
[[ "$got" == "$WASM3_SHA512" ]] || die "suma kontrolna się nie zgadza:
    oczekiwano $WASM3_SHA512
    otrzymano  $got"
note "suma kontrolna archiwum zgodna"

mkdir -p "$WORK/src"
tar xzf "$WORK/wasm3.tar.gz" -C "$WORK/src" --strip-components=1
SRC="$WORK/src/source"
[[ -d "$SRC" ]] || die "archiwum nie ma katalogu source/"

apply_patches "$SRC"
note "łatki Hydry nałożone"

rm -rf "$DEST"
mkdir -p "$DEST"
for unit in $CORE_UNITS; do
    cp "$SRC/$unit.c" "$DEST/" || die "brak $unit.c w archiwum"
done
for header in $HEADERS; do
    cp "$SRC/$header" "$DEST/" || die "brak $header w archiwum"
done

cat > "$DEST/VENDOR.md" <<EOF
# wasm3 osadzony w Hydrze

Kopia wydania **v${WASM3_VERSION}** z <https://github.com/wasm3/wasm3>, licencja MIT.
Nie edytuj tych plików ręcznie — całe drzewo odtwarza \`tools/vendor_wasm3.sh\`,
a zmiany nanoszone poza skryptem zniknie przy najbliższej aktualizacji.

## Czego tu nie ma

$(for e in $EXCLUDED; do printf -- "- \`%s\`\n" "$e"; done)

Powody wypisane są w nagłówku skryptu vendorującego. Krótko: to interfejsy
do systemu operacyjnego, a urządzenie dostaje dostęp do świata przez bindingi
Hydry.

## Łatka Hydry — budżet wykonania

wasm3 nie ma licznika instrukcji ani sposobu przerwania wykonania, a
\`ScriptModule\` obiecuje, że skrypt nie zawiesi urządzenia. Łatka dokłada
licznik w \`M3Runtime\` i pobiera z niego w czterech miejscach:

| Miejsce | Po co |
|---|---|
| \`op_ContinueLoop\` | krawędź wsteczna pętli — główna droga do wykonania bez końca |
| \`op_ContinueLoopIf\` | jw., wariant warunkowy |
| \`op_Call\` | rekurencja |
| \`op_CallIndirect\` | rekurencja przez tablicę funkcji |

Licznika **nie ma** w dyspozycji każdej operacji, bo to najgorętsza ścieżka
interpretera. Miejsce wskazali autorzy wasm3 — komentarz w \`op_ContinueLoop\`
mówi wprost, że to jest punkt, w którym wykonanie może „uciec" do klienta.

Konsekwencja: budżet liczy krawędzie pętli i wywołania, a nie instrukcje. Kod
prostoliniowy jest skończony z definicji, więc obietnica jest dotrzymana —
zmienia się jednostka budżetu. Opisane w \`WasmEngine.hpp\`.

API: \`m3_SetFuel(runtime, n)\`, \`m3_GetFuel(runtime)\`. Zero znosi ograniczenie.
Wyczerpanie kończy wykonanie błędem \`m3Err_trapOutOfFuel\`.

## Aktualizacja

    tools/vendor_wasm3.sh            # pobierz nowe wydanie i załataj
    tools/vendor_wasm3.sh --check    # sprawdź spójność osadzonego drzewa

Skrypt przerywa, gdy któraś łatka nie znajdzie swojej kotwicy — wtedy trzeba
poprawić kotwicę w skrypcie, a nie plik tutaj.
EOF

cp "$WORK/src/LICENSE" "$DEST/LICENSE" 2>/dev/null || true

note "osadzono $(ls "$DEST"/*.c | wc -l | tr -d ' ') jednostek i $(ls "$DEST"/*.h | wc -l | tr -d ' ') nagłówków w src/wasm3/"
printf '%sTeraz:%s tools/vendor_wasm3.sh --check\n' "$DIM" "$OFF"
