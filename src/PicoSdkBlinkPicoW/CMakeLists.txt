cmake_minimum_required(VERSION 3.13)

if(DEFINED ENV{PICO_SDK_PATH})
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR "PICO_SDK_PATH environment variable not set")
endif()

set(PICO_BOARD pico_w)
set(PICO_PLATFORM rp2040)

project(PicoSdkBlinkPicoW C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(src)
