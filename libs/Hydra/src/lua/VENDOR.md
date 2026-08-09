# Osadzone źródła Lua

Nie edytować ręcznie. Ten katalog odtwarza `tools/vendor_lua.sh`.

| | |
|---|---|
| Wydanie | lua-5.4.8 |
| Źródło | https://www.lua.org/ftp/lua-5.4.8.tar.gz |
| sha512 | `875ad1f6df3ba63722b5069564c9d3a4057b4c3564c691061bb49cf6cdf5d2e303f05762bd46797b444aaf992c03021f423df142123eebf86751fd77edaf8060` |
| Licencja | MIT (patrz nagłówek `lua.h`) |

## Czego nie osadzono

`lua.c luac.c linit.c liolib.c loslib.c loadlib.c`

Powody wypisane są przy zmiennej `EXCLUDED` w skrypcie: dwa pliki zawierają
`main()`, `linit.c` zastępuje opener Hydry, a `liolib`/`loslib`/`loadlib`
wymagają systemu plików i systemu operacyjnego, których na MCU nie ma.

## Zmiany wobec wydania

Wyłącznie w `luaconf.h`, wszystkie nakładane przez skrypt:

1. `#include "hydra_lua_conf.h"` tuż po `#include <stddef.h>`,
2. strażnik `#if !defined` wokół `LUA_32BITS`,
3. strażnik `#if !defined` wokół bloku `LUAI_MAXSTACK`,
4. strażnik `#if !defined` wokół `LUAL_BUFFERSIZE`.

Żadna nie zmienia semantyki języka — wszystkie tylko odsłaniają ustawienia,
których upstream nie osłonił preprocesorem. Konfiguracja właściwa siedzi
w `hydra_lua_conf.h`, który jest plikiem Hydry i skrypt go nie nadpisuje.

Sprawdzenie spójności: `tools/vendor_lua.sh --check`.
