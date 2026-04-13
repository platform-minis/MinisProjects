#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="mycastle-pico:local"
DEPS_VOLUME="pico-deps-cache"

case "${1:-build}" in
  build)
    mkdir -p "${SCRIPT_DIR}/build"
    docker run --rm \
      -v "${SCRIPT_DIR}:/workspace/project" \
      -v "${DEPS_VOLUME}:/workspace/project/build/_deps" \
      "${IMAGE}" \
      bash -c "
        cd /workspace/project/build &&
        cmake .. -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s &&
        make -j\$(nproc) &&
        /workspace/project/build/_deps/picotool-build/picotool uf2 convert \
          src/rpi_pico2_display_dvi.elf \
          src/rpi_pico2_display_dvi.uf2 \
          --family rp2350-arm-s &&
        chown -R $(id -u):$(id -g) /workspace/project/build
      "
    echo "Output: ${SCRIPT_DIR}/build/src/rpi_pico2_display_dvi.uf2"
    ;;

  clean)
    # Remove build artifacts but keep downloaded deps
    docker run --rm \
      -v "${SCRIPT_DIR}:/workspace/project" \
      -v "${DEPS_VOLUME}:/workspace/project/build/_deps" \
      "${IMAGE}" \
      bash -c "find /workspace/project/build -mindepth 1 -not -path '*/build/_deps*' -delete 2>/dev/null || true"
    echo "Cleaned (deps cache preserved)"
    ;;

  clean-all)
    sudo rm -rf "${SCRIPT_DIR}/build"
    docker volume rm "${DEPS_VOLUME}" 2>/dev/null || true
    echo "Fully cleaned"
    ;;

  *)
    echo "Usage: $0 [build|clean|clean-all]"
    exit 1
    ;;
esac
