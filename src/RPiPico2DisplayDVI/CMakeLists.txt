cmake_minimum_required(VERSION 3.13)

if(DEFINED ENV{PICO_SDK_PATH})
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR "PICO_SDK_PATH environment variable not set")
endif()

project(RPiPico2DisplayDVI C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Target: Raspberry Pi Pico 2 (RP2350)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

include(FetchContent)

# PicoDVI — DVI via PIO, 1bpp monochrome
FetchContent_Declare(
    picodvi
    GIT_REPOSITORY https://github.com/Wren6991/PicoDVI.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(picodvi)
if(NOT picodvi_POPULATED)
    FetchContent_Populate(picodvi)
endif()

pico_sdk_init()

add_subdirectory(${picodvi_SOURCE_DIR}/software/libdvi libdvi_build)

add_subdirectory(src)
