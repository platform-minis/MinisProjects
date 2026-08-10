#!/usr/bin/env bash
#
# Budowa celu przeglądarkowego.
#
# Osobny skrypt, a nie preset CMake, bo `emcmake` musi ustawić toolchain
# **przed** konfiguracją — preset wskazujący plik toolchaina Emscriptena
# wymagałby ścieżki do emsdk zapisanej na sztywno, a ta jest różna na każdej
# maszynie i w kontenerze.
#
#   ./build-wasm.sh                 — użyje emcc z PATH
#   EMSDK=/opt/emsdk ./build-wasm.sh — albo ze wskazanego emsdk
#
# Wynik: build/wasm/minis-node.{html,js,wasm}. Otworzyć przez serwer
# HTTP, nie przez file:// — przeglądarki blokują pobieranie .wasm z pliku.
#
#   python3 -m http.server -d build/wasm 8000

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HYDRA="${HYDRA_ROOT:-$HERE/../..}"

if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091
    source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
fi
command -v emcc >/dev/null || { echo "brak emcc — zainstaluj emsdk albo ustaw EMSDK"; exit 1; }

# Cel podaje się jawnie: wygenerowany CMakeLists domyśla się `podglad`,
# czyli natywnego, a ten szuka SDL2 w systemie zamiast używać portu.
TARGET="${HYDRA_TARGET:-przegladarka}"

emcmake cmake -S "$HERE" -B "$HERE/build/wasm" \
    -D HYDRA_TARGET="$TARGET" \
    -D HYDRA_ROOT="$HYDRA" \
    -D CMAKE_BUILD_TYPE=Release \
    -D CMAKE_EXECUTABLE_SUFFIX=".html"
cmake --build "$HERE/build/wasm" -j"$(nproc)"

echo
echo "gotowe: $HERE/build/wasm/minis-node.html"
echo "uruchom: python3 -m http.server -d $HERE/build/wasm 8000"
