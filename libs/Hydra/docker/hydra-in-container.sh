#!/usr/bin/env bash
#
# Pełna weryfikacja uruchamiana **wewnątrz** kontenera.
#
# Wydzielone z hydra.sh, bo tamten skrypt wywołuje `docker run` — a wołanie
# Dockera z wnętrza kontenera wymagałoby podania mu gniazda demona. Tutaj
# jesteśmy już w środku i po prostu budujemy.

set -euo pipefail

ENVS="${HYDRA_ENVS:-esp32s3 esp32c3 pico pico2 stm32g4}"

echo "=== testy hostowe ==="
make -C test
make -C test asan
make -C test tsan
make -C test examples
make -C test stub
make -C test docs
./tools/check_includes.sh

echo
echo "=== wsady ==="
failed=""
for env in $ENVS; do
    for dir in examples/*/; do
        example="${dir%/}"
        printf '\n--- %s / %s\n' "$env" "$(basename "$example")"
        if ! pio ci --project-conf platformio.ini --lib . -e "$env" "$example"; then
            failed="$failed $env/$(basename "$example")"
        fi
    done
done

if [ -n "$failed" ]; then
    printf '\n\033[31mNiepowodzenia:%s\033[0m\n' "$failed"
    exit 1
fi
printf '\n\033[32mTesty hostowe i wszystkie wsady przeszły.\033[0m\n'
