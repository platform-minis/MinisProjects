#!/usr/bin/env bash
#
# Hydra — pobranie i osadzenie oficjalnych źródeł Lua w src/lua/.
#
# Skrypt istnieje po to, żeby „aktualizacja Lua" była poleceniem, a nie
# wieczorem z diffem. Osadzone źródła są kopią wydania z lua.org, zmienioną
# w dokładnie czterech miejscach opisanych niżej — wszystkie zmiany nakłada
# ten skrypt, więc podniesienie wersji sprowadza się do zmiany LUA_VERSION
# i sprawdzenia, czy łatki nadal się nakładają.
#
# Świadomie NIE jest to submoduł gitowy: build musi działać po zwykłym
# `git clone` bez `--recursive`, a PlatformIO i tak kompiluje wszystko,
# co leży pod src/.
#
#   tools/vendor_lua.sh            — pobierz, zweryfikuj, osadź
#   tools/vendor_lua.sh --check    — tylko sprawdź, czy osadzone drzewo jest spójne
#
# Wymaga: curl, tar, sha512sum (lub shasum -a 512).

set -euo pipefail

LUA_VERSION="5.4.8"
# Suma kontrolna wydania. Zweryfikowana wobec niezależnego źródła — pakietu
# main/lua5.4 w Alpine Linux (aports), które publikuje sha512 tego samego
# archiwum. lua.org publikuje sumę wyłącznie dla wydania bieżącego, więc dla
# starszych gałęzi porównanie z pakietem dystrybucji jest jedyną dostępną
# niezależną kontrolą.
LUA_SHA512="875ad1f6df3ba63722b5069564c9d3a4057b4c3564c691061bb49cf6cdf5d2e303f05762bd46797b444aaf992c03021f423df142123eebf86751fd77edaf8060"
LUA_URL="https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/src/lua"

# Rdzeń języka — parser, maszyna wirtualna, odśmiecacz, tablice, napisy.
CORE_UNITS="lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject
            lopcodes lparser lstate lstring ltable ltm lundump lvm lzio"

# Biblioteki standardowe, które mają sens na urządzeniu bez systemu plików
# i bez systemu operacyjnego.
LIB_UNITS="lauxlib lbaselib lcorolib ldblib lmathlib lstrlib ltablib lutf8lib"

# Czego nie osadzamy i dlaczego:
#   lua.c      — main() samodzielnego interpretera,
#   luac.c     — main() kompilatora,
#   linit.c    — otwiera komplet bibliotek łącznie z io/os/package; Hydra ma
#                własny opener, który przepuszcza tylko to, co wybrał moduł,
#   liolib.c   — pliki i FILE*; urządzenie dostaje dostęp do danych przez HAL,
#   loslib.c   — os.execute, os.tmpname, zegar systemowy; namiastkę os.clock
#                i os.time dostarcza binding Hydry, oparty o hal::ITime,
#   loadlib.c  — require i ładowanie bibliotek dynamicznych; na MCU nie ma
#                czego dowiązywać w czasie wykonania.
EXCLUDED="lua.c luac.c linit.c liolib.c loslib.c loadlib.c"

RED=$'\033[31m'; GREEN=$'\033[32m'; DIM=$'\033[2m'; OFF=$'\033[0m'

die() { printf '%s✗ %s%s\n' "$RED" "$1" "$OFF" >&2; exit 1; }
note() { printf '%s✓%s %s\n' "$GREEN" "$OFF" "$1"; }

sha512of() {
    if command -v sha512sum >/dev/null 2>&1; then sha512sum "$1" | cut -d' ' -f1
    else shasum -a 512 "$1" | cut -d' ' -f1; fi
}

# --- tryb kontrolny --------------------------------------------------------

if [ "${1:-}" = "--check" ]; then
    [ -f "$DEST/VENDOR.md" ] || die "brak $DEST/VENDOR.md — źródła nie są osadzone"
    grep -q "lua-${LUA_VERSION}" "$DEST/VENDOR.md" \
        || die "osadzona wersja nie zgadza się z LUA_VERSION=${LUA_VERSION}"
    for u in $CORE_UNITS $LIB_UNITS; do
        [ -f "$DEST/$u.c" ] || die "brak jednostki $u.c"
    done
    for f in $EXCLUDED; do
        [ -f "$DEST/$f" ] && die "plik $f nie powinien być osadzony"
    done
    grep -q 'hydra_lua_conf.h' "$DEST/luaconf.h" || die "łatka konfiguracyjna nie jest nałożona"
    [ -f "$DEST/hydra_lua_conf.h" ] || die "brak hydra_lua_conf.h"
    note "osadzone źródła Lua ${LUA_VERSION} są spójne"
    exit 0
fi

# --- pobranie i weryfikacja ------------------------------------------------

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

printf '%s  POBIERAM   %s%s\n' "$DIM" "$LUA_URL" "$OFF"
curl -fsSL --retry 3 -o "$TMP/lua.tar.gz" "$LUA_URL" || die "pobieranie nie powiodło się"

got="$(sha512of "$TMP/lua.tar.gz")"
[ "$got" = "$LUA_SHA512" ] || die "suma kontrolna się nie zgadza
    oczekiwano: $LUA_SHA512
    otrzymano:  $got"
note "suma kontrolna sha512 zgodna"

tar xzf "$TMP/lua.tar.gz" -C "$TMP"
SRC="$TMP/lua-${LUA_VERSION}/src"
[ -d "$SRC" ] || die "archiwum nie zawiera lua-${LUA_VERSION}/src"

# --- kopiowanie ------------------------------------------------------------

# hydra_lua_conf.h jest nasz i przeżywa ponowne osadzenie.
mkdir -p "$DEST"
find "$DEST" -maxdepth 1 -name '*.c' -delete
find "$DEST" -maxdepth 1 -name '*.h' ! -name 'hydra_lua_conf.h' -delete

for u in $CORE_UNITS $LIB_UNITS; do
    cp "$SRC/$u.c" "$DEST/$u.c"
done
# Nagłówki kopiujemy w komplecie — są małe, a brak jednego objawia się
# dopiero w połowie kompilacji. lua.hpp (opakowanie extern "C" dla C++)
# pomijamy: Hydra ma własne, w include/hydra/script/LuaApi.hpp.
for h in "$SRC"/*.h; do
    base="$(basename "$h")"
    [ "$base" = "lua.hpp" ] && continue
    cp "$h" "$DEST/$base"
done

for f in $EXCLUDED; do rm -f "$DEST/$f"; done
note "skopiowano $(ls "$DEST"/*.c | wc -l | tr -d ' ') jednostek i $(ls "$DEST"/*.h | wc -l | tr -d ' ') nagłówków"

# --- łatki -----------------------------------------------------------------
#
# Wszystkie cztery sprowadzają się do jednego: pozwolić Hydrze ustawić
# konfigurację, której upstream nie osłonił strażnikiem preprocesora.
# Żadna nie zmienia semantyki języka.

patch_fail() { die "łatka '$1' nie znalazła oczekiwanego wzorca — sprawdź, co zmieniło się w luaconf.h wydania ${LUA_VERSION}"; }

# 1. Wciągnięcie konfiguracji Hydry na samą górę luaconf.h.
#
#    Dlaczego tutaj, a nie w lprefix.h: luaconf.h wchodzi przez lua.h do
#    każdego pliku — również do naszego kodu C++ — więc ustawienia takie jak
#    LUA_32BITS, zmieniające układ struktur i typ lua_Number, widzi tak samo
#    biblioteka i jej użytkownik. Ustawienie ich z build_flags dałoby ciche
#    rozjechanie ABI w chwili, gdy ktoś skompiluje własny plik bez tych flag.
grep -q '#include <stddef.h>' "$DEST/luaconf.h" || patch_fail "include"
awk '
    /^#include <stddef.h>$/ && !done {
        print
        print ""
        print "/* Hydra: konfiguracja osadzenia — patrz tools/vendor_lua.sh */"
        print "#include \"hydra_lua_conf.h\""
        done = 1
        next
    }
    { print }
' "$DEST/luaconf.h" > "$DEST/luaconf.h.new" && mv "$DEST/luaconf.h.new" "$DEST/luaconf.h"

# 2. LUA_32BITS — 32-bitowe liczby całkowite i float zamiast double.
grep -q '^#define LUA_32BITS	0$' "$DEST/luaconf.h" || patch_fail "LUA_32BITS"
perl -0pi -e 's/^#define LUA_32BITS\t0$/#if !defined(LUA_32BITS)\n#define LUA_32BITS\t0\n#endif/m' "$DEST/luaconf.h"

# 3. LUAI_MAXSTACK — górny limit stosu Lua.
grep -q '^#if LUAI_IS32INT$' "$DEST/luaconf.h" || patch_fail "LUAI_MAXSTACK"
perl -0pi -e 's/^#if LUAI_IS32INT\n#define LUAI_MAXSTACK\t\t1000000\n#else\n#define LUAI_MAXSTACK\t\t15000\n#endif$/#if !defined(LUAI_MAXSTACK)\n#if LUAI_IS32INT\n#define LUAI_MAXSTACK\t\t1000000\n#else\n#define LUAI_MAXSTACK\t\t15000\n#endif\n#endif/m' "$DEST/luaconf.h"

# 4. LUAL_BUFFERSIZE — bufor roboczy lauxlib, leżący na stosie C.
grep -q '^#define LUAL_BUFFERSIZE' "$DEST/luaconf.h" || patch_fail "LUAL_BUFFERSIZE"
perl -0pi -e 's/^#define LUAL_BUFFERSIZE   \(\(int\)\(16 \* sizeof\(void\*\) \* sizeof\(lua_Number\)\)\)$/#if !defined(LUAL_BUFFERSIZE)\n#define LUAL_BUFFERSIZE   ((int)(16 * sizeof(void*) * sizeof(lua_Number)))\n#endif/m' "$DEST/luaconf.h"

for marker in 'hydra_lua_conf.h' '#if !defined(LUA_32BITS)' '#if !defined(LUAI_MAXSTACK)' '#if !defined(LUAL_BUFFERSIZE)'; do
    grep -qF "$marker" "$DEST/luaconf.h" || patch_fail "$marker"
done
note "nałożono 4 łatki na luaconf.h"

# --- metryczka -------------------------------------------------------------

cat > "$DEST/VENDOR.md" <<EOF
# Osadzone źródła Lua

Nie edytować ręcznie. Ten katalog odtwarza \`tools/vendor_lua.sh\`.

| | |
|---|---|
| Wydanie | lua-${LUA_VERSION} |
| Źródło | ${LUA_URL} |
| sha512 | \`${LUA_SHA512}\` |
| Licencja | MIT (patrz nagłówek \`lua.h\`) |

## Czego nie osadzono

\`${EXCLUDED}\`

Powody wypisane są przy zmiennej \`EXCLUDED\` w skrypcie: dwa pliki zawierają
\`main()\`, \`linit.c\` zastępuje opener Hydry, a \`liolib\`/\`loslib\`/\`loadlib\`
wymagają systemu plików i systemu operacyjnego, których na MCU nie ma.

## Zmiany wobec wydania

Wyłącznie w \`luaconf.h\`, wszystkie nakładane przez skrypt:

1. \`#include "hydra_lua_conf.h"\` tuż po \`#include <stddef.h>\`,
2. strażnik \`#if !defined\` wokół \`LUA_32BITS\`,
3. strażnik \`#if !defined\` wokół bloku \`LUAI_MAXSTACK\`,
4. strażnik \`#if !defined\` wokół \`LUAL_BUFFERSIZE\`.

Żadna nie zmienia semantyki języka — wszystkie tylko odsłaniają ustawienia,
których upstream nie osłonił preprocesorem. Konfiguracja właściwa siedzi
w \`hydra_lua_conf.h\`, który jest plikiem Hydry i skrypt go nie nadpisuje.

Sprawdzenie spójności: \`tools/vendor_lua.sh --check\`.
EOF

note "osadzono Lua ${LUA_VERSION} w src/lua/"
