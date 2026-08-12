#!/usr/bin/env bash
#
# Odtwarza osadzone drzewo TensorFlow Lite Micro w libs/HydraTflm.
#
# Nie kopiuje plików ręcznie: TFLM ma własny generator minimalnego drzewa
# (`create_tflm_tree.py`), utrzymywany przez zespół TFLM właśnie po to, żeby
# integracje z zewnętrznymi środowiskami nie musiały śledzić listy plików.
# Ręczna lista rozjechałaby się przy pierwszej zmianie układu katalogów —
# a przy 200 plikach nikt tego nie zauważy, dopóki nie zabraknie symbolu.
#
# ## Dlaczego w kontenerze
#
# Generator wymaga GNU Make >= 3.82, a macOS dostarcza 3.81 (i nie podniesie
# tego z powodów licencyjnych). Do tego chce `numpy`, `pillow`, `curl`, `patch`
# i `unzip`. Zamiast wymagać tego wszystkiego od maszyny dewelopera,
# uruchamiamy go w obrazie, który i tak jest w projekcie.
#
# Użycie:
#     tools/vendor_tflm.sh [--image OBRAZ] [--ref GAŁĄŹ]
#
set -euo pipefail

IMAGE="${HYDRA_TFLM_IMAGE:-mycastle-hydra-wasm:local}"
REF="main"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$HERE/../HydraTflm"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --image) IMAGE="$2"; shift 2 ;;
        --ref)   REF="$2";   shift 2 ;;
        *) echo "Nieznany argument: $1" >&2; exit 1 ;;
    esac
done

RED=$'\033[31m'; GREEN=$'\033[32m'; DIM=$'\033[2m'; OFF=$'\033[0m'
die()  { printf '%s✗ %s%s\n' "$RED" "$1" "$OFF" >&2; exit 1; }
note() { printf '%s✓%s %s\n' "$GREEN" "$OFF" "$1"; }

command -v docker >/dev/null || die "brak dockera — generator TFLM potrzebuje kontenera"
docker image inspect "$IMAGE" >/dev/null 2>&1 \
    || die "brak obrazu $IMAGE — zbuduj go z docker/Dockerfile.cli (target hydra-wasm)"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

printf '%sgeneruję drzewo TFLM (%s) w obrazie %s…%s\n' "$DIM" "$REF" "$IMAGE" "$OFF"

# `-e hello_world` dokłada przykładowy model sinusa. Trafia on do testów jako
# tablica bajtów — testy nie mogą wymagać konwertera modeli, tak samo jak nie
# wymagają toolchaina Arduino.
docker run --rm -v "$STAGE:/out" "$IMAGE" sh -c '
    set -e
    apt-get update -qq >/dev/null 2>&1
    apt-get install -y -qq curl unzip patch >/dev/null 2>&1
    pip3 install --quiet numpy pillow 2>&1 | grep -v WARNING || true
    git clone --depth 1 --branch '"$REF"' -q https://github.com/tensorflow/tflite-micro.git /tflm
    cd /tflm
    echo "commit: $(git rev-parse --short HEAD)" > /out/COMMIT
    python3 tensorflow/lite/micro/tools/project_generation/create_tflm_tree.py \
        /out/tree -e hello_world --rename_cc_to_cpp
' >/dev/null || die "generator TFLM zawiódł"

[[ -d "$STAGE/tree/tensorflow" ]] || die "generator nie wyprodukował drzewa"

rm -rf "$DEST/src"
mkdir -p "$DEST/src"
cp -R "$STAGE/tree/tensorflow" "$STAGE/tree/third_party" "$STAGE/tree/signal" "$DEST/src/"
cp "$STAGE/tree/LICENSE" "$DEST/LICENSE"

# Model przykładowy idzie do testów, nie do biblioteki: to dane testowe,
# a nie część runtime'u.
cp "$STAGE/tree/examples/hello_world/models/hello_world_float_model_data.cpp" \
   "$HERE/test/tflm_model_sine.cpp"
cp "$STAGE/tree/examples/hello_world/models/hello_world_float_model_data.h" \
   "$HERE/test/tflm_model_sine.h"
# Nagłówek leży obok pliku w naszym drzewie, nie w `models/`.
sed -i.bak 's|#include "models/hello_world_float_model_data.h"|#include "tflm_model_sine.h"|' \
    "$HERE/test/tflm_model_sine.cpp"
rm -f "$HERE/test/tflm_model_sine.cpp.bak"

COMMIT="$(cat "$STAGE/COMMIT" 2>/dev/null || echo 'nieznany')"
note "drzewo odtworzone ($COMMIT)"
note "plików .cpp: $(find "$DEST/src" -name '*.cpp' | wc -l | tr -d ' ')"
note "rozmiar: $(du -sh "$DEST/src" | cut -f1)"

printf '%sPamiętaj: przy zmianie wersji sprawdź `make -C test` — TFLM potrafi\n' "$DIM"
printf 'zmienić układ pól TfLiteTensor, a to widać dopiero przy uruchomieniu.%s\n' "$OFF"
