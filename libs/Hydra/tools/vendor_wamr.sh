#!/usr/bin/env bash
#
# Hydra — osadzenie rdzenia WAMR (WebAssembly Micro Runtime) w src/wamr/.
#
# Trzeci skrypt osadzający, po Lua i wasm3, i ten sam zamysł: aktualizacja ma
# być poleceniem, a nie wieczorem z diffem. Różnica jest taka, że WAMR nie ma
# jednego „rdzenia" — ma macierz konfiguracji rozstrzyganą w ich CMake
# (interpreter klasyczny albo szybki, AOT, JIT, wielomodułowość, WASI, pamięć
# dzielona). Ten skrypt zamraża **jeden** punkt tej macierzy: klasyczny
# interpreter, bez WASI, bez wątków, bez AOT.
#
# Lista plików i lista definicji pochodzą z budowy, która przechodzi, a nie
# z dokumentacji. Powód w hydra_wamr_conf.h — przełączniki WAMR wiążą się ze
# sobą w sposób, którego z dokumentacji nie widać.
#
#   tools/vendor_wamr.sh           — pobierz i osadź
#   tools/vendor_wamr.sh --check   — sprawdź spójność osadzonego drzewa
#
# Wymaga: git.

set -euo pipefail

WAMR_COMMIT="97c7b8fd30b309abfe3a60b86bc5abb112fedbfa"   # 2026-08-07
WAMR_REPO="https://github.com/bytecodealliance/wasm-micro-runtime"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/src/wamr"
CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

# Pliki rdzenia; ścieżki względem korzenia repozytorium WAMR.
CORE=(
    core/iwasm/interpreter/wasm_runtime.c
    core/iwasm/interpreter/wasm_loader.c
    core/iwasm/interpreter/wasm_interp_classic.c
    core/iwasm/common/wasm_runtime_common.c
    core/iwasm/common/wasm_exec_env.c
    core/iwasm/common/wasm_memory.c
    core/iwasm/common/wasm_native.c
    core/iwasm/common/wasm_loader_common.c
    core/iwasm/common/wasm_blocking_op.c
    core/iwasm/common/wasm_application.c
    core/shared/utils/bh_assert.c
    core/shared/utils/bh_common.c
    core/shared/utils/bh_hashmap.c
    core/shared/utils/bh_list.c
    core/shared/utils/bh_log.c
    core/shared/utils/bh_queue.c
    core/shared/utils/bh_vector.c
    core/shared/utils/bh_bitmap.c
    core/shared/utils/bh_leb128.c
    # `bh_get_tick_ms` z runtime_timer.c woła bh_log.c przy każdym wpisie.
    core/shared/utils/runtime_timer.c
    # wasm_runtime_common.c woła `wasm_trap_delete` bezwarunkowo, mimo że
    # samego C API nie używamy — symbol musi skądś być.
    core/iwasm/common/wasm_c_api.c
    core/shared/mem-alloc/mem_alloc.c
    core/shared/mem-alloc/ems/ems_alloc.c
    core/shared/mem-alloc/ems/ems_gc.c
    core/shared/mem-alloc/ems/ems_hmu.c
    core/shared/mem-alloc/ems/ems_kfc.c
    # Port platformowy. Tylko te pliki — `posix_clock.c` jest pominięty
    # świadomie, bo wciąga libc_errno.h z warstwy WASI, której tu nie ma.
    core/shared/platform/linux/platform_init.c
    core/shared/platform/common/posix/posix_thread.c
    core/shared/platform/common/posix/posix_time.c
    core/shared/platform/common/posix/posix_malloc.c
    core/shared/platform/common/posix/posix_memmap.c
    core/shared/platform/common/posix/posix_sleep.c
    core/shared/platform/common/posix/posix_blocking_op.c
)

# Wywołanie funkcji natywnych jest w asemblerze i zależy od architektury.
# Kopiujemy wszystkie warianty; wybiera je system budowania.
ARCH_SRC=(
    core/iwasm/common/arch/invokeNative_em64.s
    core/iwasm/common/arch/invokeNative_aarch64.s
    core/iwasm/common/arch/invokeNative_thumb.s
    core/iwasm/common/arch/invokeNative_arm.s
    core/iwasm/common/arch/invokeNative_xtensa.s
    core/iwasm/common/arch/invokeNative_riscv.S
)

# Porty platformowe. Zweryfikowany jest wyłącznie `linux`. `esp-idf`
# i `freertos` idą do drzewa, bo bez nich nie da się nawet spróbować budowy
# na układzie — ale nikt ich jeszcze nie uruchomił.
COPY_DIRS=(
    core/shared/platform/include
    core/shared/platform/linux
    core/shared/platform/esp-idf
    core/shared/platform/freertos
    core/shared/platform/common/posix
    core/iwasm/include
    core/iwasm/common
    core/iwasm/interpreter
    # Nagłówki AOT mimo WASM_ENABLE_AOT=0: wasm_memory.c włącza aot_runtime.h
    # bezwarunkowo i dopiero w środku sprawdza przełącznik.
    core/iwasm/aot
    # `aot_runtime.h` włącza `../compilation/aot.h` — łańcuch nagłówków AOT
    # ciągnie się dalej, mimo że żaden plik AOT nie jest kompilowany.
    core/iwasm/compilation
    core/shared/utils
    core/shared/mem-alloc
    core/shared/mem-alloc/ems
)

say() { printf '  %s\n' "$*"; }

if [ "$CHECK_ONLY" = 1 ]; then
    missing=0
    for f in "${CORE[@]}"; do
        [ -f "$DEST/${f#core/}" ] || { echo "BRAK: src/wamr/${f#core/}"; missing=1; }
    done
    [ -f "$DEST/hydra_wamr_conf.h" ] || { echo "BRAK: src/wamr/hydra_wamr_conf.h"; missing=1; }
    [ -f "$DEST/config.h" ] || { echo "BRAK: src/wamr/config.h"; missing=1; }
    if [ "$missing" = 0 ]; then echo "osadzone drzewo WAMR jest spójne"; exit 0; fi
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say "pobieram WAMR @ ${WAMR_COMMIT:0:12}"
git clone --quiet --depth 1 "$WAMR_REPO" "$TMP/wamr"
git -C "$TMP/wamr" checkout --quiet "$WAMR_COMMIT" 2>/dev/null || \
    say "UWAGA: nie udało się przypiąć commita; biorę HEAD gałęzi"

rm -rf "$DEST"
mkdir -p "$DEST"

# Układ zachowany bez przedrostka `core/` — inaczej każda ścieżka włączeń
# w osadzonym drzewie miałaby o jeden poziom za dużo.
copy_rel() {
    local rel="${1#core/}"
    mkdir -p "$DEST/$(dirname "$rel")"
    cp "$TMP/wamr/$1" "$DEST/$rel"
}

# `core/config.h` musi trafić do korzenia osadzonego drzewa: platform_common.h
# sięga po niego ścieżką `../../../config.h`, która po zdjęciu przedrostka
# `core/` wypada dokładnie tutaj. Bez niego nie kompiluje się nic poza
# pojedynczym plikiem bez włączeń — i objaw nie wskazuje na przyczynę.
cp "$TMP/wamr/core/config.h" "$DEST/config.h"
# `version.h` leży o dwa poziomy wyżej niż iwasm/common i jest tam włączany
# ścieżką względną — po zdjęciu przedrostka `core/` wypada w korzeniu.
cp "$TMP/wamr/core/version.h" "$DEST/version.h"

for f in "${CORE[@]}"; do copy_rel "$f"; done
for f in "${ARCH_SRC[@]}"; do [ -f "$TMP/wamr/$f" ] && copy_rel "$f" || true; done

for d in "${COPY_DIRS[@]}"; do
    [ -d "$TMP/wamr/$d" ] || continue
    rel="${d#core/}"
    mkdir -p "$DEST/$rel"
    find "$TMP/wamr/$d" -maxdepth 1 -type f \
        \( -name '*.h' -o -name '*.inl' \) -exec cp {} "$DEST/$rel/" \;
done

say "skopiowano $(find "$DEST" -type f | wc -l) plików"

cat > "$DEST/hydra_wamr_conf.h" <<'CONF'
#pragma once
/*
 * Hydra — punkt macierzy konfiguracji WAMR, który osadzamy.
 *
 * Te wartości nie są preferencją, tylko **jedyną kombinacją sprawdzoną
 * kompilacją**. WAMR wiąże przełączniki ze sobą w sposób niewidoczny
 * z dokumentacji, a objawem jest błąd w pliku, którego się nie tykało:
 *
 *   WASM_ENABLE_BULK_MEMORY=1  odsłania w wasm_interp_classic.c odwołania do
 *                              `linear_mem_size`, deklarowanego dopiero przy
 *                              WASM_ENABLE_SHARED_HEAP — objaw to „undeclared”
 *                              i nieosiągalna etykieta `out_of_bounds`;
 *   WASM_ENABLE_LIBC_WASI=1    wciąga posix_clock.c, a ten libc_errno.h
 *                              z warstwy, której w tym zestawie nie ma.
 *
 * Zmiana któregokolwiek wymaga powtórzenia budowy i prawdopodobnie dołożenia
 * plików do listy w tools/vendor_wamr.sh.
 */

#define WASM_ENABLE_INTERP        1
#define WASM_ENABLE_FAST_INTERP   0   /* szybszy, ale większy — osobna decyzja */
#define WASM_ENABLE_AOT           0
#define WASM_ENABLE_JIT           0
#define WASM_ENABLE_LIBC_BUILTIN  0
#define WASM_ENABLE_LIBC_WASI     0
#define WASM_ENABLE_MULTI_MODULE  0
#define WASM_ENABLE_SHARED_MEMORY 0
#define WASM_ENABLE_THREAD_MGR    0
#define WASM_ENABLE_BULK_MEMORY   0
#define WASM_ENABLE_REF_TYPES     0
#define WASM_ENABLE_GC            0
#define WASM_ENABLE_SHARED_HEAP   0
#define WASM_ENABLE_MEMORY64      0

/*
 * Pamięć.
 *
 * WAMR ma **własny** menedżer puli (mem-alloc/ems), więc podpięcie go pod
 * pulę Hydry wygląda inaczej niż w wasm3: nie podmienia się trzech funkcji
 * alokatora, tylko podaje bufor przy `wasm_runtime_full_init()` z trybem
 * `Alloc_With_Pool`. Prostsze, ale zupełnie inne — i dlatego nie ma tu łatki
 * odpowiadającej tej z vendor_wasm3.sh.
 */
#define BH_MALLOC wasm_runtime_malloc
#define BH_FREE   wasm_runtime_free
CONF

cat > "$DEST/VENDOR.md" <<EOF
# WAMR — źródła osadzone

Rdzeń [WebAssembly Micro Runtime](https://github.com/bytecodealliance/wasm-micro-runtime),
commit \`$WAMR_COMMIT\`, licencja Apache-2.0 WITH LLVM-exception.

Odtwarza to drzewo \`tools/vendor_wamr.sh\`. Osadzony jest **jeden** punkt
macierzy konfiguracji: klasyczny interpreter, bez WASI, wątków i AOT.
Wartości w \`hydra_wamr_conf.h\` są sprawdzone kompilacją.

## Stan weryfikacji

| Element | Stan |
|---|---|
| Kompilacja rdzenia na Linuksie (aarch64) | sprawdzona: 32/32 plików, ~220 KB kodu |
| Port \`esp-idf\` / \`freertos\` | skopiowany, **nieuruchomiony** |
| \`WamrEngine\` za \`IScriptEngine\` | **nie istnieje** |
| Podpięcie pod pulę \`script::Heap\` | **nie zrobione** |

Drzewo nie jest jeszcze wciągane przez żaden system budowania —
\`HYDRA_SCRIPT_HAS_WAMR\` zostaje zerem, a \`EngineSelector\` cofa się do wasm3
z zaznaczonym ustępstwem.

## Budowa rdzenia — polecenie sprawdzone

    ARCH=\$(uname -m)
    case \$ARCH in aarch64) T=-DBUILD_TARGET_AARCH64; N=aarch64;;
                   *)       T=-DBUILD_TARGET_X86_64;  N=em64;; esac

    gcc -std=gnu99 -w -c \$T -DBH_PLATFORM_LINUX -include hydra_wamr_conf.h \\
        -Iinclude -Iiwasm/include -Iiwasm/common -Iiwasm/interpreter \\
        -Ishared/utils -Ishared/platform/include -Ishared/platform/linux \\
        -Ishared/mem-alloc -Ishared/mem-alloc/ems \\
        iwasm/interpreter/*.c iwasm/common/wasm_*.c \\
        iwasm/common/arch/invokeNative_\$N.s \\
        shared/utils/*.c shared/mem-alloc/*.c shared/mem-alloc/ems/*.c \\
        shared/platform/linux/platform_init.c \\
        shared/platform/common/posix/posix_{thread,time,malloc,memmap,sleep,blocking_op}.c

Uwaga: \`posix_clock.c\` jest **pominięty** świadomie — wciąga \`libc_errno.h\`
z warstwy WASI, której tu nie ma.
EOF

say "gotowe — src/wamr/ ($(du -sh "$DEST" | cut -f1))"
