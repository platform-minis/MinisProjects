# Hydra — przykład media-native: potok audio na karcie dźwiękowej hosta.
#
# Ten plik jest zarazem wzorcem dla generatora Studia: to, co Studio wypisuje
# dla celu `mcu: native`, ma wyglądać tak samo. Jeśli coś tu poprawiasz,
# popraw też packages/hydra-studio/src/model/emit/cmake.ts — inaczej przykład
# i wygenerowany projekt zaczną się różnić w sposób, którego nikt nie zauważy
# do pierwszego błędu linkowania.
#
#   cmake -B build -D HYDRA_ROOT=../..
#   cmake --build build
#   ./build/media-native
#
# Windows (SDL2 z vcpkg):
#   cmake -B build -D HYDRA_ROOT=../.. -D CMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# macOS (SDL2 z Homebrew):
#   brew install sdl2 && cmake -B build -D HYDRA_ROOT=../..

cmake_minimum_required(VERSION 3.20)
project(media-native VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(HYDRA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Katalog biblioteki Hydra")

# --- źródła frameworka -----------------------------------------------------
# Backend hostowy: atrapy HAL i scheduler na pthreadach. Modułów opcjonalnych
# ten przykład nie potrzebuje — rysuje przez gfx, a nie przez moduł ui.
file(GLOB HYDRA_SOURCES
    "${HYDRA_ROOT}/src/core/*.cpp"
    "${HYDRA_ROOT}/src/hal/*.cpp"
    "${HYDRA_ROOT}/src/hal/mock/*.cpp"
    "${HYDRA_ROOT}/src/hal/host/*.cpp"
    "${HYDRA_ROOT}/src/util/*.cpp"
    "${HYDRA_ROOT}/src/gfx/*.cpp"
    "${HYDRA_ROOT}/src/gfx/sdl/*.cpp"
    # Potok multimedialny: rdzeń, elementy programowe i backend SDL.
    "${HYDRA_ROOT}/src/media/*.cpp"
    "${HYDRA_ROOT}/src/media/elements/*.cpp"
    "${HYDRA_ROOT}/src/media/sdl/*.cpp")

if(NOT HYDRA_SOURCES)
    message(FATAL_ERROR
        "Nie znaleziono źródeł Hydry w \"${HYDRA_ROOT}\". "
        "Wskaż katalog biblioteki: cmake -B build -D HYDRA_ROOT=<ścieżka>")
endif()

add_library(hydra STATIC ${HYDRA_SOURCES})
target_include_directories(hydra PUBLIC "${HYDRA_ROOT}/include")
target_compile_options(hydra PUBLIC -fno-exceptions -fno-rtti -Wall -Wextra)
target_compile_definitions(hydra PUBLIC
    HYDRA_FORCE_HOST=1
    HYDRA_ENABLE_MEDIA=1
    HYDRA_BOARD_HEADER="hydra/boards/native.hpp")

find_package(Threads REQUIRED)
target_link_libraries(hydra PUBLIC Threads::Threads)

# --- SDL -------------------------------------------------------------------
#
# Trzy drogi, bo trzy systemy pakują SDL inaczej: vcpkg i Homebrew dają plik
# konfiguracyjny CMake, dystrybucje Linuksa — pkg-config. Brak SDL nie jest
# błędem konfiguracji: Hydra zbuduje się bez okna, a SdlDisplay::begin() zwróci
# NotSupported. Tak wygląda ta sama budowa w CI bez serwera X.
find_package(SDL2 CONFIG QUIET)

if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 IMPORTED_TARGET QUIET sdl2)
    endif()
endif()

if(TARGET SDL2::SDL2)
    target_link_libraries(hydra PUBLIC SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
        # Windows: SDL2main dostarcza WinMain, które woła nasze main().
        target_link_libraries(hydra PUBLIC SDL2::SDL2main)
    endif()
    set(HYDRA_SDL_FOUND ON)
elseif(TARGET PkgConfig::SDL2)
    target_link_libraries(hydra PUBLIC PkgConfig::SDL2)
    set(HYDRA_SDL_FOUND ON)
else()
    set(HYDRA_SDL_FOUND OFF)
endif()

if(HYDRA_SDL_FOUND)
    target_compile_definitions(hydra PUBLIC HYDRA_WITH_SDL=1)
    message(STATUS "Hydra: SDL znaleziony — okno będzie dostępne")
else()
    message(WARNING
        "Hydra: nie znaleziono SDL2 — build powstanie bez okna.\n"
        "  Debian/Ubuntu:  sudo apt install libsdl2-dev\n"
        "  macOS:          brew install sdl2\n"
        "  Windows:        vcpkg install sdl2  (+ CMAKE_TOOLCHAIN_FILE)")
endif()

# --- aplikacja -------------------------------------------------------------
add_executable(media-native main.cpp)
target_link_libraries(media-native PRIVATE hydra)

# Bez tego binarka na Windows nie startuje: SDL2.dll leży w katalogu pakietu,
# a nie obok pliku wykonywalnego. Kopiujemy ją po budowie, żeby katalog
# z wynikiem dało się spakować i przenieść na inną maszynę w całości.
if(WIN32 AND HYDRA_SDL_FOUND AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    add_custom_command(TARGET media-native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:media-native> $<TARGET_FILE_DIR:media-native>
        COMMAND_EXPAND_LISTS)
endif()
