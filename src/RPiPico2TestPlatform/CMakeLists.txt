cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Pico SDK import (must happen BEFORE project())
# Set PICO_SDK_PATH in your environment or pass -DPICO_SDK_PATH=...
# ---------------------------------------------------------------------------
include(pico_sdk_import.cmake OPTIONAL RESULT_VARIABLE _SDK_FOUND)

if(_SDK_FOUND)
    # Building for Pico 2 (RP2350)
    set(PICO_BOARD pico2 CACHE STRING "Target board")
    project(RPiPico2TestPlatform C CXX ASM)
    pico_sdk_init()
else()
    # Building for host (x86/arm64) - unit tests only
    project(RPiPico2TestPlatform C CXX)
    message(STATUS "pico_sdk_import.cmake not found - building HOST unit tests only")
    set(HOST_BUILD TRUE)
endif()

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Strict warnings for all targets
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror=return-type
)

# ---------------------------------------------------------------------------
# Firmware target (Pico only)
# ---------------------------------------------------------------------------
if(NOT HOST_BUILD)
    add_subdirectory(src)
endif()

# ---------------------------------------------------------------------------
# Tests (always built - host unit tests + optional integration tests)
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
