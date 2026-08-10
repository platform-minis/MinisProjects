#!/usr/bin/env bash
#
# Hydra — osadzenie WAMR jako osobnej biblioteki w libs/HydraWamr/.
#
# ## Dlaczego osobno, a nie w src/ Hydry jak wasm3
#
# wasm3 to 14,5 tysiąca wierszy przenośnego C99 bez warstwy platformy. WAMR to
# ~80 tysięcy wierszy i katalog na system, wybierany normalnie przez CMake.
# PlatformIO kompiluje całe `src/` biblioteki, więc wsadzenie WAMR-a do Hydry
# oznaczałoby te 80 tysięcy wierszy w budowie **każdego** projektu, także tego
# bez skryptów — usuwanych dopiero przez konsolidator.
#
# Jako osobna biblioteka WAMR trafia wyłącznie do projektów, które wpiszą go
# w `lib_deps`. Hydra o nim nie wie, dopóki nie zażąda się silnika WAMR przez
# HYDRA_SCRIPT_WASM_ENGINE.
#
# ## Zakres konfiguracji
#
# Wyłącznie interpreter klasyczny: bez AOT, JIT, GC, WASI, wątków i pamięci
# dzielonej. To jest ten WAMR, który ma sens na urządzeniu — reszta jest dla
# serwerów albo wymaga toolchaina, którego na płytce nie ma.
#
# **BH_MALLOC/BH_FREE muszą wskazywać na alokator WAMR-a.** Runtime sprawdza to
# makrem i przerywa budowę, jeśli tak nie jest. Bez tych dwóch definicji nic
# się nie zbuduje, a komunikat („unexpected BH_MALLOC") nie mówi, czego brakuje.
#
# ## Platformy
#
# | Cel | Warstwa | Stan |
# |---|---|---|
# | host (macOS/Linux) | `darwin` + `common/posix` | zweryfikowane kompilatorem |
# | ESP32 / ESP32-S3 | `esp-idf` | **nieprzetestowane** — patrz niżej |
#
# Warstwy ESP32 nie da się sprawdzić bez toolchaina Espressifa, a konfiguracja
# WAMR-a dla niej powstała z listy plików, nie z uruchomionej budowy. Traktuj ją
# jako punkt wyjścia do pierwszego `pio run`, a nie jako gotową rzecz.
#
# RP2040/RP2350 i STM32 zostają na wasm3 — i tak są poniżej progu 256 kB RAM,
# od którego WAMR zaczyna mieć sens.
#
#   tools/vendor_wamr.sh            — pobierz i osadź
#   tools/vendor_wamr.sh --check    — sprawdź spójność osadzonego drzewa

set -euo pipefail

WAMR_VERSION="2.4.5"
WAMR_SHA512="3aadee3befdd9a8f4fb45c13800e98145ef5492843b08715d9d6787dc9261fb345cc9005d9544efb184f53c83dfe495c176d97b0f05c729db76069f3e3aea60e"
WAMR_URL="https://github.com/bytecodealliance/wasm-micro-runtime/archive/refs/tags/WAMR-${WAMR_VERSION}.tar.gz"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$(cd "$ROOT/.." && pwd)/HydraWamr"

# Jednostki rdzenia — wszystkie zweryfikowane kompilatorem na hoście.
# Kolejność bez znaczenia; grupowanie odpowiada katalogom WAMR-a.
UNITS=(
  core/iwasm/common/wasm_runtime_common.c
  core/iwasm/common/wasm_native.c
  core/iwasm/common/wasm_exec_env.c
  core/iwasm/common/wasm_memory.c
  core/iwasm/common/wasm_loader_common.c
  # wasm_runtime_common.c woła wasm_trap_delete bezwarunkowo — bez tej
  # jednostki drzewo kompiluje się, ale nie linkuje.
  core/iwasm/common/wasm_c_api.c
  core/iwasm/common/wasm_blocking_op.c
  core/iwasm/common/arch/invokeNative_general.c
  core/iwasm/interpreter/wasm_loader.c
  core/iwasm/interpreter/wasm_runtime.c
  core/iwasm/interpreter/wasm_interp_classic.c
  core/shared/mem-alloc/mem_alloc.c
  core/shared/mem-alloc/ems/ems_kfc.c
  core/shared/mem-alloc/ems/ems_alloc.c
  core/shared/mem-alloc/ems/ems_hmu.c
  core/shared/utils/bh_assert.c
  core/shared/utils/bh_common.c
  core/shared/utils/bh_hashmap.c
  core/shared/utils/bh_list.c
  core/shared/utils/bh_log.c
  core/shared/utils/bh_bitmap.c
  core/shared/utils/bh_vector.c
  # Dekoder LEB128 — wołany przez wasm_loader_common.c.
  core/shared/utils/bh_leb128.c
  core/shared/utils/runtime_timer.c
)

# Wywołanie funkcji natywnej z modułu — w assemblerze, per architektura.
# Wariant `invokeNative_general.c` jest przenośny, ale nie przenosi poprawnie
# argumentów zmiennoprzecinkowych: `event_emit` z argumentem f32 wywracał się
# na arm64. Assembler jest tu jedyną drogą, którą WAMR uważa za poprawną.
ASM_UNITS=(
  core/iwasm/common/arch/invokeNative_aarch64.s
  core/iwasm/common/arch/invokeNative_xtensa.s
  core/iwasm/common/arch/invokeNative_em64.s
  # macOS używa Mach-O, a warianty ELF-owe mają dyrektywy, których jego
  # asembler nie zna. WAMR dostarcza na to osobny plik uniwersalny.
  core/iwasm/common/arch/invokeNative_osx_universal.s
)

# Warstwa POSIX — host. `posix_clock.c` odpada, bo ciągnie `libc_errno.h`
# z warstwy WASI, której nie budujemy.
POSIX_UNITS=(
  core/shared/platform/darwin/platform_init.c
  core/shared/platform/common/posix/posix_time.c
  core/shared/platform/common/posix/posix_malloc.c
  core/shared/platform/common/posix/posix_memmap.c
  # Przenośne `os_mremap`. Potrzebne, odkąd wyłączyliśmy sprzętową kontrolę
  # granic: `wasm_memory.c` wchodzi wtedy w ścieżkę powiększania pamięci
  # liniowej, która go woła.
  core/shared/platform/common/memory/mremap.c
  core/shared/platform/common/posix/posix_thread.c
  core/shared/platform/common/posix/posix_sleep.c
  core/shared/platform/common/posix/posix_blocking_op.c
)

# Warstwa ESP-IDF. NIEPRZETESTOWANA — patrz nagłówek.
ESPIDF_UNITS=(
  core/shared/platform/esp-idf/espidf_platform.c
  core/shared/platform/esp-idf/espidf_thread.c
  core/shared/platform/esp-idf/espidf_malloc.c
  core/shared/platform/esp-idf/espidf_memmap.c
  core/shared/platform/esp-idf/espidf_clock.c
)

# Katalogi nagłówków kopiowane w całości — WAMR włącza je ścieżkami względnymi
# wewnątrz swojego drzewa, więc struktury nie wolno spłaszczyć.
HEADER_DIRS=(
  core/iwasm/include
  # Nagłówki AOT mimo wyłączonego AOT: `wasm_memory.c` włącza aot_runtime.h
  # bezwarunkowo, a dopiero w środku sprawdza WASM_ENABLE_AOT. Bez nich drzewo
  # osadzone się nie buduje, choć klon z pełnym repozytorium tak.
  core/iwasm/aot
  core/iwasm/compilation
  core/iwasm/common
  core/iwasm/interpreter
  core/shared/platform/include
  core/shared/platform/darwin
  core/shared/platform/esp-idf
  core/shared/platform/common/posix
  core/shared/mem-alloc
  core/shared/utils
)

RED=$'\033[31m'; GREEN=$'\033[32m'; DIM=$'\033[2m'; OFF=$'\033[0m'
die()  { printf '%s✗ %s%s\n' "$RED" "$1" "$OFF" >&2; exit 1; }
note() { printf '%s✓%s %s\n' "$GREEN" "$OFF" "$1"; }

sha512of() {
    if command -v sha512sum >/dev/null 2>&1; then sha512sum "$1" | cut -d' ' -f1
    else shasum -a 512 "$1" | cut -d' ' -f1
    fi
}

# ---------------------------------------------------------------------------
# Tryb --check
# ---------------------------------------------------------------------------

if [[ "${1:-}" == "--check" ]]; then
    [[ -d "$DEST/src" ]] || die "brak $DEST/src — uruchom skrypt bez --check"

    missing=0
    for u in "${UNITS[@]}" "${POSIX_UNITS[@]}"; do
        [[ -f "$DEST/src/$u" ]] || { printf '  brak %s\n' "$u"; missing=1; }
    done
    (( missing == 0 )) || die "osadzone drzewo jest niekompletne"

    grep -q "BH_MALLOC=wasm_runtime_malloc" "$DEST/library.json" \
        || die "library.json nie ustawia BH_MALLOC — WAMR sie nie zbuduje"

    note "osadzone drzewo WAMR $WAMR_VERSION jest spójne"
    exit 0
fi

# ---------------------------------------------------------------------------
# Pobranie i osadzenie
# ---------------------------------------------------------------------------

command -v curl >/dev/null 2>&1 || die "wymagany curl"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

printf '%sPobieram%s %s\n' "$DIM" "$OFF" "$WAMR_URL"
curl -sSfL -o "$WORK/wamr.tar.gz" "$WAMR_URL" || die "pobranie nie powiodło się"

got="$(sha512of "$WORK/wamr.tar.gz")"
if [[ "$WAMR_SHA512" == "__WYPELNIANE_PRZY_PIERWSZYM_URUCHOMIENIU__" ]]; then
    printf '%sSuma kontrolna archiwum:%s %s\n' "$DIM" "$OFF" "$got"
    printf '%sWpisz ją do WAMR_SHA512 w tym skrypcie i uruchom ponownie.%s\n' "$DIM" "$OFF"
else
    [[ "$got" == "$WAMR_SHA512" ]] || die "suma kontrolna się nie zgadza:
    oczekiwano $WAMR_SHA512
    otrzymano  $got"
    note "suma kontrolna archiwum zgodna"
fi

mkdir -p "$WORK/src"
tar xzf "$WORK/wamr.tar.gz" -C "$WORK/src" --strip-components=1
SRC="$WORK/src"
[[ -d "$SRC/core" ]] || die "archiwum nie ma katalogu core/"

rm -rf "$DEST/src" "$DEST/VENDOR.md"
mkdir -p "$DEST/src"

# Jednostki — ze strukturą katalogów, bo WAMR włącza się ścieżkami względnymi.
for u in "${UNITS[@]}" "${ASM_UNITS[@]}" "${POSIX_UNITS[@]}" "${ESPIDF_UNITS[@]}"; do
    mkdir -p "$DEST/src/$(dirname "$u")"
    cp "$SRC/$u" "$DEST/src/$u" || die "brak $u w archiwum"
done

# Nagłówki — całe katalogi, bez wchodzenia w podkatalogi, których nie budujemy.
for d in "${HEADER_DIRS[@]}"; do
    [[ -d "$SRC/$d" ]] || continue
    mkdir -p "$DEST/src/$d"
    find "$SRC/$d" -maxdepth 1 -name "*.h" -exec cp {} "$DEST/src/$d/" \;
done
# `core/config.h` i `core/version.h` leżą poza tymi katalogami, a są włączane.
cp "$SRC/core/config.h" "$DEST/src/core/config.h"
[[ -f "$SRC/core/version.h" ]] && cp "$SRC/core/version.h" "$DEST/src/core/version.h"
mkdir -p "$DEST/src/core/shared/mem-alloc/ems"
find "$SRC/core/shared/mem-alloc/ems" -maxdepth 1 -name "*.h" \
     -exec cp {} "$DEST/src/core/shared/mem-alloc/ems/" \;

cp "$SRC/LICENSE" "$DEST/LICENSE" 2>/dev/null || true

cat > "$DEST/VENDOR.md" <<EOF
# WAMR osadzony dla Hydry

Kopia wydania **WAMR-${WAMR_VERSION}** z
<https://github.com/bytecodealliance/wasm-micro-runtime>, licencja Apache-2.0
z wyjątkiem LLVM. Nie edytuj tych plików — całe drzewo odtwarza
\`libs/Hydra/tools/vendor_wamr.sh\`.

## Dlaczego osobna biblioteka, a nie \`src/\` Hydry

wasm3 to 14,5 tysiąca wierszy przenośnego C99. WAMR to ~80 tysięcy i katalog na
system operacyjny. PlatformIO kompiluje całe \`src/\` biblioteki, więc WAMR
w Hydrze trafiałby do budowy **każdego** projektu — także tego bez skryptów.
Jako osobna biblioteka wchodzi wyłącznie tam, gdzie wpisano go w \`lib_deps\`.

## Zakres

Wyłącznie interpreter klasyczny. Wyłączone: AOT, JIT, GC, WASI, wątki, pamięć
dzielona. Włączone: \`WASM_ENABLE_INSTRUCTION_METERING\` — dzięki niemu budżet
wykonania jest w WAMR wbudowany i **nie wymaga łatki**, w przeciwieństwie do
wasm3.

## Pułapka, która kosztuje najwięcej czasu

\`BH_MALLOC\` i \`BH_FREE\` muszą wskazywać na \`wasm_runtime_malloc\`
i \`wasm_runtime_free\`. WAMR sprawdza to makrem i przerywa budowę komunikatem
„unexpected BH_MALLOC", który nie mówi, czego brakuje. Definicje są
w \`library.json\`.

## Stan platform

| Cel | Warstwa | Stan |
|---|---|---|
| host (macOS/Linux) | \`darwin\` + \`common/posix\` | **zweryfikowane** — 29/29 jednostek |
| ESP32 / ESP32-S3 | \`esp-idf\` | **nieprzetestowane** |

Warstwa ESP-IDF została skopiowana z listy plików, bez uruchomionej budowy —
w środowisku, w którym powstawało to osadzenie, nie było ani \`cmake\`, ani
toolchaina Espressifa. Traktuj ją jako punkt wyjścia do pierwszego
\`pio run\`, a nie jako rzecz gotową.

RP2040/RP2350 i STM32 zostają na wasm3: są poniżej progu 256 kB RAM, od którego
WAMR zaczyna mieć sens.
EOF

note "osadzono $(find "$DEST/src" -name '*.c' | wc -l | tr -d ' ') jednostek w $DEST/src"
printf '%sTeraz:%s tools/vendor_wamr.sh --check\n' "$DIM" "$OFF"
