#!/usr/bin/env bash
#
# Hydra — pobranie i osadzenie źródeł wasm3 w src/wasm3/.
#
# Ten sam zamysł co tools/vendor_lua.sh: „aktualizacja wasm3" ma być poleceniem,
# a nie wieczorem z diffem. Osadzone drzewo jest kopią wydania z GitHuba,
# zmienioną w dokładnie trzech miejscach opisanych niżej — wszystkie łatki
# nakłada ten skrypt.
#
# Świadomie NIE jest to submoduł: build musi działać po zwykłym `git clone`
# bez `--recursive`, a PlatformIO kompiluje wszystko, co leży pod src/.
#
#   tools/vendor_wasm3.sh           — pobierz i osadź
#   tools/vendor_wasm3.sh --check   — sprawdź, czy osadzone drzewo jest spójne
#
# Wymaga: git (albo curl + tar).

set -euo pipefail

WASM3_COMMIT="d77cd814aa0bc68cb1df917580a6304d34cfb30b"   # 2026-06-26
WASM3_REPO="https://github.com/wasm3/wasm3"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/src/wasm3"
CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

# ---------------------------------------------------------------------------
# Które pliki bierzemy
# ---------------------------------------------------------------------------
#
# Rdzeń interpretera i nic więcej. Pomijamy:
#
#   m3_api_wasi.c, m3_api_uvwasi.c, m3_api_meta_wasi.c
#       WASI to interfejs systemowy dla programów uniksowych — pliki, gniazda,
#       zegar ścienny. Urządzenie ma HAL i to on jest jego systemem; wpuszczenie
#       WASI dołożyłoby drugi, niespójny z pierwszym.
#   m3_api_libc.c
#       importy printf/memcpy dla modułów kompilowanych „jak na PC". Funkcje
#       gospodarza wnosi etap trzeci — pod nazwami Hydry, nie libc.
#   m3_api_tracer.c
#       narzędzie do śledzenia wykonania; przydatne przy pracy nad samym
#       wasm3, bezużyteczne w produkcie.
#
KEEP=(
    m3_bind.c m3_bind.h
    m3_code.c m3_code.h
    m3_compile.c m3_compile.h
    m3_config.h m3_config_platforms.h
    m3_core.c m3_core.h
    m3_env.c m3_env.h
    m3_exception.h
    m3_exec.c m3_exec.h m3_exec_defs.h
    m3_function.c m3_function.h
    m3_info.c m3_info.h
    m3_math_utils.h
    m3_module.c
    m3_parse.c
    wasm3.h
    wasm3_defs.h
)

say() { printf '  %s\n' "$*"; }

if [ "$CHECK_ONLY" = 1 ]; then
    missing=0
    for f in "${KEEP[@]}"; do
        [ -f "$DEST/$f" ] || { echo "BRAK: src/wasm3/$f"; missing=1; }
    done
    [ -f "$DEST/hydra_m3_conf.h" ] || { echo "BRAK: src/wasm3/hydra_m3_conf.h"; missing=1; }
    grep -q "HYDRA_WASM3_ALLOC" "$DEST/m3_core.c" 2>/dev/null \
        || { echo "BRAK łatki alokatora w m3_core.c"; missing=1; }
    grep -q "hydra_m3_conf.h" "$DEST/m3_config.h" 2>/dev/null \
        || { echo "BRAK łatki konfiguracji w m3_config.h"; missing=1; }
    if [ "$missing" = 0 ]; then
        echo "osadzone drzewo wasm3 jest spójne"
        exit 0
    fi
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say "pobieram wasm3 @ ${WASM3_COMMIT:0:12}"
git clone --quiet --depth 1 "$WASM3_REPO" "$TMP/w3"
git -C "$TMP/w3" fetch --quiet --depth 1 origin "$WASM3_COMMIT" 2>/dev/null || true
git -C "$TMP/w3" checkout --quiet "$WASM3_COMMIT" 2>/dev/null || \
    say "UWAGA: nie udało się przypiąć commita; biorę HEAD gałęzi"

rm -rf "$DEST"
mkdir -p "$DEST"
for f in "${KEEP[@]}"; do
    cp "$TMP/w3/source/$f" "$DEST/$f"
done
say "skopiowano ${#KEEP[@]} plików"

# ---------------------------------------------------------------------------
# Łatka 1: konfiguracja
# ---------------------------------------------------------------------------
cat > "$DEST/hydra_m3_conf.h" <<'CONF'
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
CONF

# `#pragma once` stoi w pierwszym wierszu m3_config.h — wstawiamy się tuż za nim,
# żeby nasze definicje wyprzedziły wszystkie `#ifndef` oryginału.
awk 'NR==1 { print; print "#include \"hydra_m3_conf.h\""; next } { print }' \
    "$DEST/m3_config.h" > "$DEST/m3_config.h.new"
mv "$DEST/m3_config.h.new" "$DEST/m3_config.h"
say "łatka 1/3: hydra_m3_conf.h wpięty w m3_config.h"

# ---------------------------------------------------------------------------
# Łatka 2: alokator
# ---------------------------------------------------------------------------
#
# wasm3 alokuje z malloc() albo ze statycznego bufora, który potrafi zwolnić
# wyłącznie ostatni blok. Żadne z tych dwojga nie nadaje się do urządzenia:
# malloc łamie regułę „brak alokacji po App::begin()", a bufor bez zwalniania
# przecieka przy każdym przeładowaniu modułu.
#
# Kierujemy więc alokacje na pulę Hydry — tę samą klasę `Heap`, z której
# korzysta Lua. Ma `allocate`, `release` i `reallocate(ptr, stary, nowy)`,
# czyli dokładnie trzy operacje, których wasm3 potrzebuje.
python3 - "$DEST/m3_core.c" <<'PATCH'
import sys, re
path = sys.argv[1]
src = open(path, encoding='utf-8').read()

marker = "#if d_m3FixedHeap\n"
assert marker in src, "nie znaleziono gałęzi alokatora — sprawdź, czy wasm3 jej nie przebudował"

hydra_branch = '''#if defined(HYDRA_WASM3_ALLOC)

/*
 * Alokator Hydry — łatka nakładana przez tools/vendor_wasm3.sh.
 *
 * Trzy funkcje niżej dostarcza src/script/Wasm3Engine.cpp i kieruje je na
 * pulę `script::Heap`. Powód w komentarzu przy łatce w skrypcie osadzającym.
 */
extern void * hydra_wasm3_malloc  (size_t size);
extern void   hydra_wasm3_free    (void * ptr);
extern void * hydra_wasm3_realloc (void * ptr, size_t newSize, size_t oldSize);

void *  m3_Malloc_Impl  (size_t i_size)                                  { return hydra_wasm3_malloc(i_size); }
void    m3_Free_Impl    (void * io_ptr)                                  { hydra_wasm3_free(io_ptr); }
void *  m3_Realloc_Impl (void * i_ptr, size_t i_new, size_t i_old)       { return hydra_wasm3_realloc(i_ptr, i_new, i_old); }

#elif d_m3FixedHeap
'''

src = src.replace(marker, hydra_branch, 1)
open(path, 'w', encoding='utf-8').write(src)
print("  łatka 2/3: alokator skierowany na pulę Hydry")
PATCH

# ---------------------------------------------------------------------------
# Łatka 3: opis pochodzenia
# ---------------------------------------------------------------------------
cat > "$DEST/VENDOR.md" <<EOF
# wasm3 — źródła osadzone

Kopia rdzenia interpretera [wasm3](https://github.com/wasm3/wasm3),
commit \`$WASM3_COMMIT\`, licencja MIT.

Drzewo **nie jest** edytowane ręcznie. Odtwarza je \`tools/vendor_wasm3.sh\`,
który pobiera źródła i nakłada trzy łatki:

1. \`hydra_m3_conf.h\` wpięty w \`m3_config.h\` — rozmiar strony kodu, wysokość
   stosu funkcji, wyłączone WASI i tracer.
2. Alokator w \`m3_core.c\` skierowany na pulę \`script::Heap\` pod
   \`HYDRA_WASM3_ALLOC\` — bez tego wasm3 woła \`malloc()\`.
3. Ten plik.

Pominięte wobec oryginału: \`m3_api_wasi.c\`, \`m3_api_uvwasi.c\`,
\`m3_api_meta_wasi.c\`, \`m3_api_libc.c\`, \`m3_api_tracer.c\`. Powody są
w komentarzu przy liście \`KEEP\` w skrypcie osadzającym.

Podniesienie wersji: zmień \`WASM3_COMMIT\` i uruchom skrypt. Jeśli łatka się
nie nałoży, skrypt przerwie z komunikatem wskazującym miejsce.
EOF
say "łatka 3/3: VENDOR.md"

say "gotowe — src/wasm3/ ($(du -sh "$DEST" | cut -f1))"
