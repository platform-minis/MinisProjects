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
#    wprost. Reguła pilnuje warstw Hydry, a nie tego, co pisze się na niej.
hits=$(grep -rnE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"]($ARDUINO_HEADERS)[>\"]" \
        include src examples templates test 2>/dev/null \
    | grep -vE '^src/[a-z]+/arduino/' \
    | grep -vE '^(examples|templates)/' \
    | grep -v '^test/arduino_stub/' || true)

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

# --- Prywatne nagłówki backendu nie wyciekają do API -----------------------

hits=$(grep -rn 'ArduinoBackend\.hpp' include examples 2>/dev/null || true)

if [ -n "$hits" ]; then
    report "prywatny nagłówek backendu użyty poza src/hal/arduino/" "$hits"
else
    printf '%s✓%s prywatne nagłówki backendu nie wyciekają do API\n' "$GREEN" "$OFF"
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
