cmake_minimum_required(VERSION 3.13)

if(DEFINED ENV{PICO_SDK_PATH})
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR "PICO_SDK_PATH environment variable not set")
endif()

project(RPiPico2DisplayVGA C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Target: Raspberry Pi Pico 2 (RP2350)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

pico_sdk_init()

# ---------------------------------------------------------------------------
# PicoVGA — codaris/picovga-cmake v1.2.2 (supports RP2040 + RP2350)
# Pinned to exact commit — brak śledzenia zmian upstream.
# Repo:    https://github.com/codaris/picovga-cmake
# Release: https://github.com/codaris/picovga-cmake/releases/tag/v1.2.2
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    picovga
    GIT_REPOSITORY https://github.com/codaris/picovga-cmake.git
    GIT_TAG        f2dc01f6990d81b08e9c31494325aced66f2e20d  # v1.2.2
)
FetchContent_MakeAvailable(picovga)

# Makro add_picovga() pochodzi z picovga.cmake i zajmuje się:
#   - generowaniem nagłówków PIO (vga.pio → vga.pio.h)
#   - dołączaniem plików renderujących (asm, C)
#   - linkowaniem wszystkich zależności pico-sdk
include(${picovga_SOURCE_DIR}/picovga.cmake)

# ---------------------------------------------------------------------------
# PS/2 keyboard — lurk101/ps2kbd-lib
# Pinned commit: 9ef20df0bfded1d8fdddbb8b864202224c7faf50
# Repo: https://github.com/lurk101/ps2kbd-lib
# GPIO: GP20 (DATA), GP21 (CLK)  — używa PIO1 (PIO0 zajęty przez PicoVGA)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    ps2kbd_lib
    GIT_REPOSITORY https://github.com/lurk101/ps2kbd-lib.git
    GIT_TAG        9ef20df0bfded1d8fdddbb8b864202224c7faf50
)
FetchContent_MakeAvailable(ps2kbd_lib)

add_subdirectory(src)
