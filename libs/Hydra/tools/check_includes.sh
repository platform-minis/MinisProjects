#!/usr/bin/env bash
#
# Hydra — test reguł zależności między warstwami (rozdz. 3).
#
# Reguła 1: zależności biegną wyłącznie w dół. Warstwa HAL nie ma prawa sięgać
#           po rdzeń aplikacyjny (App, EventBus, IModule, Log) — inaczej
#           przestałaby dać się użyć w izolacji i w testach.
# Reguła 2: nagłówki Arduino wolno włączać wyłącznie w katalogach backendów,
#           czyli src/*/arduino/. Specyfikacja wymienia src/hal/arduino/, bo
#           w chwili jej spisania był to jedyny backend; zasada mówi jednak
#           o plikach backendu, a Wi-Fi jest peryferiem jak każde inne.
#
# Dodatkowo pilnujemy, żeby rdzeń nie zaglądał wprost do FreeRTOS-a — cała
# wiedza o jądrze ma siedzieć w backendzie RTOS.
#
# Uruchomienie:  tools/check_includes.sh   (z katalogu biblioteki albo dowolnego)

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 2

RED=$'\033[31m'; GREEN=$'\033[32m'; DIM=$'\033[2m'; OFF=$'\033[0m'
violations=0

report() {
    printf '%s✗ %s%s\n' "$RED" "$1" "$OFF"
    printf '%s\n' "$2" | sed 's/^/    /'
    violations=$((violations + 1))
}

# --- Reguła 2: nagłówki Arduino tylko w katalogach backendów ---------------

ARDUINO_HEADERS='Arduino\.h|Wire\.h|SPI\.h|EEPROM\.h|Preferences\.h|HardwareSerial\.h|WiFi\.h'

# Wyjątki:
#  - src/*/arduino/       — właściwe miejsce backendu;
#  - test/arduino_stub/   — atrapy tych nagłówków, a nie ich użycie;
#  - examples/, templates/ — kod użytkownika, któremu wolno sięgać po Arduino
#    wprost. Reguła pilnuje warstw Hydry, a nie tego, co pisze się na niej;
#  - include/compat/     — powierzchnia zgodności z cudzym API (dziś Arduboy2).
#    Jej zadaniem jest właśnie stanie okrakiem na Arduino: na układzie oddaje
#    sprawę prawdziwemu `Arduino.h`, na hoście podstawia atrapę. Katalog jest
#    osobnym korzeniem włączeń, dodawanym do ścieżki tylko dla projektów, które
#    o dany moduł poprosiły — kod przenośny nie ma jak stamtąd niczego złapać.
hits=$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"]($ARDUINO_HEADERS)[>\"]" \
        include src examples templates test 2>/dev/null \
    | grep -vE '^src/[a-z]+/arduino/' \
    | grep -vE '^(examples|templates)/' \
    | grep -v '^test/arduino_stub/' \
    | grep -v '^include/compat/' || true)

if [ -n "$hits" ]; then
    report "nagłówki Arduino poza katalogami backendów (src/*/arduino/)" "$hits"
else
    printf '%s✓%s nagłówki Arduino tylko w katalogach backendów\n' "$GREEN" "$OFF"
fi

# --- Reguła 1: HAL nie sięga po rdzeń aplikacyjny --------------------------

CORE_UPPER='hydra/core/(App|EventBus|Events|IModule|Log|Task|LogSinks)\.hpp'

hits=$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"]$CORE_UPPER[>\"]" \
        include/hydra/hal src/hal 2>/dev/null || true)

if [ -n "$hits" ]; then
    report "warstwa HAL sięga po rdzeń aplikacyjny" "$hits"
else
    printf '%s✓%s HAL używa wyłącznie fundamentów rdzenia\n' "$GREEN" "$OFF"
fi

# --- Rdzeń nie zna FreeRTOS-a poza backendem RTOS --------------------------

hits=$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"](freertos/)?(FreeRTOS|task|queue|semphr)\.h[>\"]" \
        include src 2>/dev/null \
    | grep -v '^src/core/rtos_freertos\.cpp' || true)

if [ -n "$hits" ]; then
    report "FreeRTOS włączany poza backendem RTOS" "$hits"
else
    printf '%s✓%s FreeRTOS widoczny tylko w src/core/rtos_freertos.cpp\n' "$GREEN" "$OFF"
fi

# --- Biblioteki interfejsu tylko w plikach wiążących -----------------------

# LVGL jest biblioteką w C z globalnym API, więc nie da się jej owinąć
# szablonem jak bibliotek graficznych. Zamiast tego cała reszta rozmawia
# z typem cech, a lvgl.h pojawia się dokładnie w jednym pliku.
hits=$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"](lvgl|lv_conf)\.h[>\"]" \
        include src examples test 2>/dev/null \
    | grep -v '^include/hydra/ui/lvgl/LvglApi\.hpp' || true)

if [ -n "$hits" ]; then
    report "nagłówki LVGL poza plikiem wiążącym (LvglApi.hpp)" "$hits"
else
    printf '%s✓%s LVGL widoczne tylko w LvglApi.hpp\n' "$GREEN" "$OFF"
fi

# --- SDL tylko w backendzie okna -------------------------------------------

# SDL jest dla celu `native` tym, czym Arduino dla ESP32: biblioteką backendu.
# Ma więc tę samą regułę — nagłówki publiczne operują na void*, a SDL.h wchodzi
# tylko w katalogach backendów. Bez tego cel `native` zacząłby przeciekać do API
# i przestałby być wymienny z celami sprzętowymi.
#
# Katalogi są dwa i lista ma się nie wydłużać: okno (src/gfx/sdl/) oraz
# dźwięk i podgląd potoku (src/media/sdl/). Trzeci oznaczałby, że SDL rozlewa
# się po bibliotece zamiast siedzieć za interfejsem.
hits=$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"]SDL[0-9]*(_[a-z]+)?\.h[>\"]" \
        include src examples templates test 2>/dev/null \
    | grep -vE '^src/(gfx|media)/sdl/' || true)

if [ -n "$hits" ]; then
    report "nagłówki SDL poza katalogami backendów (src/gfx/sdl/, src/media/sdl/)" "$hits"
else
    printf '%s✓%s SDL widoczne tylko w src/gfx/sdl/ i src/media/sdl/\n' "$GREEN" "$OFF"
fi

# --- Osadzone Lua tylko przez plik wiążący ---------------------------------

# Ta sama reguła co dla LVGL i z tego samego powodu: Lua jest biblioteką w C
# z globalnym API, a jej nagłówki wciągają luaconf.h, który ustala reprezentację
# liczb i układ struktur. Gdyby wchodziły w wielu miejscach, podniesienie wersji
# interpretera albo zmiana profilu pamięci oznaczałyby przeczesywanie drzewa.
#
# Wyjątki: src/lua/ to same osadzone źródła, które włączają się nawzajem,
# a src/script/LuaInternal.hpp jest jedynym plikiem wiążącym po stronie C++.
hits=$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"].*(lua|lauxlib|lualib)\.h[>\"]" \
        include src examples templates test 2>/dev/null \
    | grep -vE '^src/lua/' \
    | grep -v '^src/script/LuaInternal\.hpp' || true)

if [ -n "$hits" ]; then
    report "nagłówki Lua poza plikiem wiążącym (src/script/LuaInternal.hpp)" "$hits"
else
    printf '%s✓%s Lua widoczne tylko w LuaInternal.hpp\n' "$GREEN" "$OFF"
fi

# Kod aplikacji nie ma prawa zobaczyć typu lua_State — API skryptów wystawia
# `Interp` i `Ctx`, żeby zmiana interpretera nie przechodziła przez nagłówki.
#
# Komentarze są z tego wyłączone: nagłówek ma prawo wyjaśnić, co trzyma pod
# `void*`, i właśnie to robi. Sprawdzenie dotyczy kodu, więc przed dopasowaniem
# wygaszamy komentarze wierszowe, jednowierszowe blokowe i wiersze ciągu bloku.
# Wygaszamy, a nie usuwamy — inaczej rozjechałaby się numeracja w raporcie.
hits=$(for f in $(grep -rl 'lua_State' include examples templates 2>/dev/null || true); do
    sed -e 's|//.*||' -e 's|/\*.*\*/||' -e 's|^[[:space:]]*\*.*||' "$f" \
        | grep -n 'lua_State' | sed "s|^|$f:|"
done)

if [ -n "$hits" ]; then
    report "typ lua_State wyciekł do API albo do kodu użytkownika" "$hits"
else
    printf '%s✓%s lua_State nie wycieka poza src/\n' "$GREEN" "$OFF"
fi

# --- Prywatne nagłówki backendu nie wyciekają do API -----------------------

hits=$(grep -rn 'ArduinoBackend\.hpp' include examples 2>/dev/null || true)

if [ -n "$hits" ]; then
    report "prywatny nagłówek backendu użyty poza src/hal/arduino/" "$hits"
else
    printf '%s✓%s prywatne nagłówki backendu nie wyciekają do API\n' "$GREEN" "$OFF"
fi

# Ta sama reguła co dla Lua i z tego samego powodu: wasm3 jest biblioteką w C,
# a `m3_config.h` ustala rozmiary stron kodu i tryb alokacji. Gdyby jej nagłówki
# wchodziły w wielu miejscach, podniesienie wersji runtime'u albo zmiana profilu
# pamięci oznaczałyby przeczesywanie drzewa.
#
# Wyjątki: src/wasm3/ to osadzone źródła włączające się nawzajem, a
# src/script/WasmEngine.cpp jest jedynym plikiem wiążącym po stronie C++.
hits=$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"](wasm3|m3_)[^>\"]*\.h[>\"]" \
        include src examples templates test 2>/dev/null \
    | grep -vE '^src/wasm3/' \
    | grep -v '^src/script/WasmEngine\.cpp' || true)

if [ -n "$hits" ]; then
    report "nagłówki wasm3 poza plikiem wiążącym (src/script/WasmEngine.cpp)" "$hits"
else
    printf '%s✓%s wasm3 widoczne tylko w WasmEngine.cpp\n' "$GREEN" "$OFF"
fi

# Kod aplikacji nie ma prawa zobaczyć typów wasm3 — API skryptów wystawia
# `IScriptEngine`, żeby zmiana runtime'u nie przechodziła przez nagłówki.
# Komentarze wygaszamy z tego samego powodu, co przy lua_State.
hits=$(for f in $(grep -rlE 'IM3Runtime|IM3Module|IM3Function' include examples templates 2>/dev/null || true); do
    sed -e 's|//.*||' -e 's|/\*.*\*/||' -e 's|^[[:space:]]*\*.*||' "$f" \
        | grep -nE 'IM3Runtime|IM3Module|IM3Function' | sed "s|^|$f:|"
done)

if [ -n "$hits" ]; then
    report "typy wasm3 wyciekły do API albo do kodu użytkownika" "$hits"
else
    printf '%s✓%s typy wasm3 nie wyciekają poza src/\n' "$GREEN" "$OFF"
fi

# --- Podsumowanie ----------------------------------------------------------

echo
if [ "$violations" -eq 0 ]; then
    printf '%sReguły zależności spełnione.%s\n' "$GREEN" "$OFF"
    exit 0
fi
printf '%sNaruszeń reguł zależności: %d%s\n' "$RED" "$violations" "$OFF"
printf '%sSzczegóły reguł: rozdz. 3 specyfikacji i komentarz w hydra/hal/Hal.hpp%s\n' "$DIM" "$OFF"
exit 1
