#!/usr/bin/env bash
#
# Hydra — budowa jednego przykładu dla jednej platformy.
#
#   tools/build_example.sh <środowisko> <przykład>
#   tools/build_example.sh esp32s3 blink-task
#
# Jedno miejsce, w którym opisane jest, czego przykład potrzebuje: modułów
# opcjonalnych, własnego pliku płytki, ewentualnego pominięcia. Wywołują to
# zarówno `docker/hydra.sh fw`, jak i CI — wcześniej obie ścieżki miały własne
# kopie tej logiki i zdążyły się rozjechać.
#
# Uruchamiać z katalogu głównego Hydry; wymaga `pio` w ścieżce.

set -euo pipefail

env_name="${1:?podaj środowisko, np. esp32s3}"
example="${2:?podaj przykład, np. blink-task}"
dir="examples/$example"

[ -d "$dir" ] || { printf 'Nie ma przykładu: %s\n' "$example" >&2; exit 1; }

# --- pominięcie ------------------------------------------------------------
# Przykład bywa bez sensu na danej płytce (sieć na module bez radia). Powód
# leży w pliku `skip` i zawsze jest wypisywany — ciche pominięcie czyta się
# później jak „przeszło".
if [ -f "$dir/skip" ]; then
    reason=$(awk -v e="$env_name" '$1 == e { $1=""; sub(/^ +/,""); print }' "$dir/skip")
    if [ -n "$reason" ]; then
        printf '\033[33m∅ %s / %s — pominięty: %s\033[0m\n' "$env_name" "$example" "$reason"
        exit 0
    fi
fi

# --- moduły opcjonalne -----------------------------------------------------
# Wyprowadzane z włączeń w kodzie przykładu, a nie z osobnej listy, która
# mogłaby się z nim rozminąć.
flags=""
for pair in SENSE:sense NET:net UI:ui MOTION:motion OTA:ota; do
    name="${pair%%:*}"; path="${pair##*:}"
    if grep -qr "hydra/${path}/" "$dir" 2>/dev/null; then
        flags="$flags -DHYDRA_ENABLE_${name}=1"
    fi
done

# --- płytka ----------------------------------------------------------------
# Aplikacja konkretnego urządzenia potrzebuje pliku pinów tego urządzenia.
if [ -f "$dir/board" ]; then
    board=$(tr -d '[:space:]' < "$dir/board")
    flags="$flags -DHYDRA_BOARD_HEADER=\\\"hydra/boards/${board}.hpp\\\""
fi

printf '\033[36m→ %s / %s%s\033[0m\n' "$env_name" "$example" \
       "$( [ -n "$flags" ] && printf ' (%s)' "$(printf '%s' "$flags" | tr -s ' ')" )"

# PLATFORMIO_BUILD_FLAGS dokłada się do build_flags ze środowiska,
# w przeciwieństwie do --project-option, które by je zastąpiło.
PLATFORMIO_BUILD_FLAGS="$flags" exec pio ci \
    --project-conf platformio.ini \
    --lib . \
    -e "$env_name" \
    "$dir"
