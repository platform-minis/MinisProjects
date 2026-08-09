#pragma once
/**
 * Hydra — jedyne miejsce w C++, przez które wchodzą nagłówki Lua.
 *
 * Reguła jest ta sama, co dla `lvgl.h` w warstwie UI (rozdz. 3, reguła 4):
 * biblioteka zewnętrzna ma dokładnie jeden punkt wejścia, więc „gdzie to jest
 * używane" jest pytaniem z odpowiedzią z `grep`, a podmiana wersji nie oznacza
 * przeczesywania drzewa. Nagłówek leży w `src/`, a nie w `include/`, bo nie
 * należy do API Hydry — kod aplikacji nie ma prawa go zobaczyć.
 *
 * Ścieżki są względne celowo. Źródła Lua leżą w `src/lua/` razem ze swoimi
 * nagłówkami i włączają się nawzajem cudzysłowem, więc kompilują się bez ani
 * jednej dodatkowej ścieżki `-I`. Zapis względny utrzymuje tę własność także
 * po naszej stronie: build nie wymaga niczego poza tym, co PlatformIO i tak
 * ustawia dla biblioteki.
 */

extern "C" {
#include "../lua/lauxlib.h"
#include "../lua/lua.h"
#include "../lua/lualib.h"
}

#include "hydra/core/Types.hpp"

namespace hydra {
namespace script {

/** Tłumaczy kod stanu Lua na kod błędu Hydry. */
inline Err mapLuaStatus(int status) {
    switch (status) {
        case LUA_OK:        return Err::None;
        case LUA_ERRSYNTAX: return Err::BadArgument;
        case LUA_ERRMEM:    return Err::OutOfMemory;
        case LUA_ERRRUN:    return Err::Internal;
        case LUA_ERRERR:    return Err::Internal;
        default:            return Err::Internal;
    }
}

}  // namespace script
}  // namespace hydra
