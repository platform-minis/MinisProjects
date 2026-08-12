# Hydra — testy jednostkowe rdzenia na hoście (rozdz. 12).
#
# Rdzeń kompiluje się natywnie na PC z backendem RTOS opartym o pthread,
# bez FreeRTOS-a i bez Arduino. To główny poligon testów jednostkowych.
#
#   make              — kompilacja i uruchomienie
#   make FILTER=Event — tylko przypadki zawierające podany napis
#   make asan         — sanitizer adresów i zachowań niezdefiniowanych
#   make tsan         — sanitizer wyścigów (patrz uwaga niżej)
#   make clean
#
# Warianty z sanitizerami budują się do osobnych katalogów — mieszanie obiektów
# z różnymi flagami kończy się błędem linkowania albo, gorzej, cichą korupcją.

CXX      ?= c++
CC       ?= cc
# Katalog budowy rozdzielony po systemie i architekturze. Drzewo źródeł bywa
# montowane do kontenera, więc ten sam `build/` widzą naraz macOS i Linux —
# bez rozdzielenia kontener uznawał binarkę Mach-O za aktualną i próbował ją
# uruchomić („Syntax error: word unexpected").
HOST_TAG := $(shell uname -s)-$(shell uname -m)
BUILD    ?= build/$(HOST_TAG)
EXTRA    ?=
BIN       = $(BUILD)/hydra_tests

INCLUDES  = -I../include -I../src/pocketmod -I../src/wasm3 -I../../HydraWamr/src/core/iwasm/include -I. \
            -I../../HydraTflm/src -I../../HydraTflm/src/third_party/flatbuffers/include \
            -I../../HydraTflm/src/third_party/gemmlowp -I../../HydraTflm/src/third_party/ruy
# Wyjątki i RTTI wyłączone tak samo jak w buildzie na MCU (rozdz. 11) —
# inaczej testy nie sprawdzałyby tego samego kodu, co urządzenie.
#
# -MMD -MP jest tu obowiązkowe, nie kosmetyczne: bez śledzenia zależności od
# nagłówków zmiana pola w klasie przebudowuje tylko jedną jednostkę translacji,
# a reszta zostaje ze starym układem obiektu — objawem jest losowy SIGABRT,
# nie błąd kompilacji.
CXXFLAGS  = -std=c++17 -O1 -g -Wall -Wextra -Wshadow -Wno-unused-parameter \
            -fno-exceptions -fno-rtti -DHYDRA_FORCE_HOST -DHYDRA_ENABLE_SENSE=1 -DHYDRA_ENABLE_NET=1 -DHYDRA_ENABLE_UI=1 -DHYDRA_ENABLE_MOTION=1 -DHYDRA_ENABLE_OTA=1 -DHYDRA_ENABLE_SCRIPT=1 -DHYDRA_SCRIPT_ENGINE_WASM=1 -DHYDRA_SCRIPT_ENGINE_WAMR=1 -DHYDRA_ENABLE_MINIS=1 -DHYDRA_ENABLE_MEDIA=1 -DHYDRA_ENABLE_ARDUBOY=1 -DHYDRA_ENABLE_INFER_TFLM=1 -DTF_LITE_STATIC_MEMORY \
            -MMD -MP $(INCLUDES) $(EXTRA)
LDFLAGS   = -lpthread $(EXTRA)

CORE_SRC  = $(wildcard ../src/core/*.cpp)
HAL_SRC   = $(wildcard ../src/hal/*.cpp) $(wildcard ../src/hal/mock/*.cpp) \
            $(wildcard ../src/hal/host/*.cpp)
SENSE_SRC = $(wildcard ../src/sense/*.cpp) $(wildcard ../src/drivers/sense/*.cpp)
NET_SRC   = $(wildcard ../src/net/*.cpp) $(wildcard ../src/net/mock/*.cpp)
# gfx/sdl wchodzi do testów w wariancie bez SDL — ten sam, który powstaje
# w CI i na maszynie bez serwera X. Dzięki temu kompiluje się przy każdej
# zmianie, a nie dopiero u kogoś, kto ma zainstalowane libsdl2-dev.
GFX_SRC   = $(wildcard ../src/gfx/*.cpp) $(wildcard ../src/gfx/sdl/*.cpp)
MOTION_SRC = $(wildcard ../src/motion/*.cpp)
SHELL_SRC = $(wildcard ../src/shell/*.cpp) $(wildcard ../src/diag/*.cpp)
UTIL_SRC  = $(wildcard ../src/util/*.cpp) $(wildcard ../src/ota/*.cpp)
UI_SRC    = $(wildcard ../src/ui/*.cpp) $(wildcard ../src/ui/mock/*.cpp) \
            $(wildcard ../src/ui/lvgl/*.cpp)
SCRIPT_SRC = $(wildcard ../src/script/*.cpp)
# Moduł minis: rdzeń, łącza i JSON. Łącze MQTT wchodzi razem z modułem
# sieciowym, szeregowe — z atrapą UART-u z backendu hostowego.
MINIS_SRC = $(wildcard ../src/minis/*.cpp) $(wildcard ../src/minis/links/*.cpp)
# Potok multimedialny: rdzeń i elementy programowe. Elementy sprzętowe
# (I2S, PWM, ADC) i natywne (SDL) dochodzą w kolejnych etapach.
MEDIA_SRC = $(wildcard ../src/media/*.cpp) $(wildcard ../src/media/elements/*.cpp) \
            $(wildcard ../src/media/sdl/*.cpp)
# Bodziec: warstwa wywołująca zjawiska w świecie wokół urządzenia. Wyłącznie
# hostowa — implementacja na atrapach, obok której stanie kiedyś stanowisko.
# Zgodność z Arduboy2. `Runtime.cpp` jest wyłączony świadomie: na hoście
# definiuje `main()` dla niezmienionych gier, a tu `main()` należy do
# frameworka testowego. Odwołuje się też do `setup()` i `loop()`, których
# w testach nikt nie dostarcza.
ARDUBOY_SRC = $(filter-out ../src/arduboy/Runtime.cpp,$(wildcard ../src/arduboy/*.cpp))
STIM_SRC  = $(wildcard ../src/stim/*.cpp)
# Inferencja: na razie sam szew i silnik bez modelu. TFLM dojdzie osobną
# biblioteką, tak jak WAMR — patrz docs/plan-tflm.md.
INFER_SRC = $(wildcard ../src/infer/*.cpp)
# Model do testów: tablica bajtów wygenerowana z `.tflite`. Testy nie mogą
# wymagać konwertera, tak samo jak nie wymagają toolchaina Arduino.
MODEL_SRC = $(wildcard tflm_model_*.cpp)

# TensorFlow Lite Micro — osobna biblioteka, tak jak WAMR i z tego samego
# powodu: PlatformIO kompiluje całe `src/`, a 200 plików TFLM nie ma czego
# szukać w projekcie bez modelu. Drzewo odtwarza `tools/vendor_tflm.sh`.
TFLM_ROOT = ../../HydraTflm/src
TFLM_SRC  = $(shell find $(TFLM_ROOT) -name '*.cpp' -not -name '*_test.cpp' 2>/dev/null)
TFLM_OBJ  = $(patsubst $(TFLM_ROOT)/%.cpp,$(BUILD)/tflm/%.o,$(TFLM_SRC))
TFLM_INC  = -I$(TFLM_ROOT) -I$(TFLM_ROOT)/third_party/flatbuffers/include \
            -I$(TFLM_ROOT)/third_party/gemmlowp -I$(TFLM_ROOT)/third_party/ruy \
            -I$(TFLM_ROOT)/third_party/kissfft
# `TF_LITE_STATIC_MEMORY` przestawia TFLM na arenę zamiast malloca — to ta sama
# reguła, co w całej Hydrze. `STRIP_ERROR_STRINGS` zdejmuje komunikaty, które
# na MCU zajmują kilkanaście kB flasha; tu zostawiamy je włączone, bo w testach
# to one mówią, dlaczego model się nie wczytał.
TFLM_DEF  = -DTF_LITE_STATIC_MEMORY -DTF_LITE_DISABLE_X86_NEON
TEST_SRC  = $(wildcard test_*.cpp)
SRC       = $(CORE_SRC) $(HAL_SRC) $(SENSE_SRC) $(NET_SRC) $(GFX_SRC) $(UI_SRC) $(MOTION_SRC) $(SHELL_SRC) $(UTIL_SRC) $(SCRIPT_SRC) $(MINIS_SRC) $(MEDIA_SRC) $(ARDUBOY_SRC) $(STIM_SRC) $(INFER_SRC) $(MODEL_SRC) $(TEST_SRC)
OBJ       = $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(SRC)))

# Osadzone źródła Lua kompilują się jako C, nie jako C++. Kompilacja ich jako
# C++ zmieniłaby obsługę błędów interpretera z setjmp/longjmp na wyjątki, które
# w Hydrze są wyłączone — czyli wywróciłaby cały mechanizm pcall.
# pocketmod: jeden plik C z ciałem biblioteki nagłówkowej.
POCKETMOD_SRC = $(wildcard ../src/pocketmod/*.c)
POCKETMOD_OBJ = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(POCKETMOD_SRC)))

LUA_SRC   = $(wildcard ../src/lua/*.c)
LUA_OBJ   = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(LUA_SRC)))

# wasm3 też jest w C. `d_m3HydraHeap` przekierowuje jego alokacje na script::Heap
# — bez tego wasm3 wołałby calloc przy każdym `reload()`, czyli w czasie pracy
# urządzenia. -I../src/wasm3 jest potrzebne, bo jego pliki włączają się nawzajem
# po samej nazwie.
WASM3_SRC = $(wildcard ../src/wasm3/*.c)
WASM3_OBJ = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(WASM3_SRC)))

# WAMR mieszka w osobnej bibliotece (libs/HydraWamr), nie w src/ Hydry — patrz
# jej VENDOR.md. Testy budują go wprost ze źródeł, żeby dało się sprawdzić oba
# runtime'y w jednej binarce; to jedyny sposób, żeby zauważyć, że zaczęły się
# różnić na tym samym bajtkodzie.
WAMR_ROOT = ../../HydraWamr/src
WAMR_SRC  = $(shell find $(WAMR_ROOT) -name '*.c' -not -path '*/esp-idf/*' \
              -not -name 'invokeNative_general.c' 2>/dev/null)
# macOS: wariant uniwersalny (Mach-O). Linux: plik właściwy dla architektury.
WAMR_ASM  = $(WAMR_ROOT)/core/iwasm/common/arch/invokeNative_$(if $(filter Darwin,$(shell uname -s)),osx_universal,$(if $(filter aarch64,$(shell uname -m)),aarch64,em64)).s
WAMR_ASM_OBJ = $(BUILD)/wamr_invokeNative.o
WAMR_OBJ  = $(patsubst %.c,$(BUILD)/wamr_%.o,$(notdir $(WAMR_SRC)))

WAMR_INC  = -I$(WAMR_ROOT)/core -I$(WAMR_ROOT)/core/iwasm/include \
            -I$(WAMR_ROOT)/core/iwasm/common -I$(WAMR_ROOT)/core/iwasm/interpreter \
            -I$(WAMR_ROOT)/core/shared/platform/include \
            -I$(WAMR_ROOT)/core/shared/platform/darwin \
            -I$(WAMR_ROOT)/core/shared/mem-alloc -I$(WAMR_ROOT)/core/shared/utils

# BH_MALLOC/BH_FREE muszą wskazywać na alokator WAMR-a — runtime sprawdza to
# makrem i przerywa budowę komunikatem, który nie mówi, czego brakuje.
# BUILD_TARGET musi zgadzać się z architekturą — od niego zależy między innymi
# wybór ścieżki wywołania funkcji natywnej.
WAMR_ARCH = $(if $(filter arm64 aarch64,$(shell uname -m)),AARCH64,X86_64)
WAMR_DEF  = -DBH_PLATFORM_DARWIN -DBUILD_TARGET_$(WAMR_ARCH) \
            -DWASM_ENABLE_INTERP=1 -DWASM_ENABLE_FAST_INTERP=0 \
            -DWASM_ENABLE_AOT=0 -DWASM_ENABLE_JIT=0 -DWASM_ENABLE_GC=0 \
            -DWASM_ENABLE_INSTRUCTION_METERING=1 \
            -DWASM_ENABLE_LIBC_BUILTIN=0 -DWASM_ENABLE_LIBC_WASI=0 \
            -DWASM_ENABLE_MULTI_MODULE=0 -DWASM_ENABLE_SHARED_MEMORY=0 \
            -DWASM_ENABLE_THREAD_MGR=0 \
            -DWASM_DISABLE_HW_BOUND_CHECK=1 -DWASM_DISABLE_STACK_HW_BOUND_CHECK=1 \
            -DBH_MALLOC=wasm_runtime_malloc -DBH_FREE=wasm_runtime_free

CFLAGS    = -std=c99 -O1 -g -Wall -Wextra -MMD -MP $(INCLUDES) -I../src/wasm3 \
            -Dd_m3HydraHeap=1 $(EXTRA)

DEP       = $(OBJ:.o=.d) $(LUA_OBJ:.o=.d) $(WASM3_OBJ:.o=.d)

VPATH     = ../src/core:../src/hal:../src/hal/mock:../src/hal/host:../src/sense:../src/drivers/sense:\
            ../src/net:../src/net/mock:../src/gfx:../src/gfx/sdl:../src/ui:../src/ui/mock:../src/ui/lvgl:../src/motion:../src/shell:../src/diag:../src/util:../src/ota:\
            ../src/script:../src/minis:../src/minis/links:../src/media:../src/media/elements:../src/media/sdl:../src/arduboy:../src/stim:../src/infer:../src/pocketmod:../src/lua:../src/wasm3:$(subst $() $(),:,$(patsubst %/,%,$(sort $(dir $(WAMR_SRC))))):.

.PHONY: docs all run asan tsan examples stub clean

all: run

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# WAMR ma własne flagi i własny prefiks obiektu, żeby nazwy nie zderzyły się
# z jednostkami Hydry ani Lua.
$(BUILD)/wamr_%.o: %.c | $(BUILD)
	$(CC) -std=gnu99 -O1 -g -MMD -MP $(WAMR_INC) $(WAMR_DEF) $(EXTRA) -c $< -o $@

$(BUILD)/tflm/%.o: $(TFLM_ROOT)/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O1 -g -fno-exceptions -fno-rtti -MMD -MP \
	       $(TFLM_INC) $(TFLM_DEF) $(EXTRA) \
	       -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	       -Wno-unused-private-field -Wno-deprecated-declarations \
	       -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Definicje są tu konieczne, nie kosmetyczne: plik wybiera wariant symbolu
# po BH_PLATFORM_DARWIN (macOS chce podkreślenia i nie zna dyrektyw ELF-owych),
# a wariant uniwersalny przełącza się po architekturze.
$(WAMR_ASM_OBJ): $(WAMR_ASM) | $(BUILD)
	$(CC) -x assembler-with-cpp $(WAMR_DEF) -c $< -o $@

$(BIN): $(OBJ) $(LUA_OBJ) $(WASM3_OBJ) $(WAMR_OBJ) $(WAMR_ASM_OBJ) $(TFLM_OBJ) $(POCKETMOD_OBJ)
	$(CXX) $(OBJ) $(LUA_OBJ) $(WASM3_OBJ) $(WAMR_OBJ) $(WAMR_ASM_OBJ) $(TFLM_OBJ) $(POCKETMOD_OBJ) $(LDFLAGS) -o $@

run: $(BIN)
	@./$(BIN) $(FILTER)

asan:
	@$(MAKE) BUILD=build-asan/$(HOST_TAG) \
	         EXTRA="-fsanitize=address,undefined -fno-omit-frame-pointer" run

# TSan zgłasza też wyścigi z założenia nieszkodliwe: pola Task::Stats pisze
# wyłącznie sam task, a diagnostyka je tylko czyta (patrz komentarz przy
# strukturze Stats). Ostrzeżenia spoza tej kategorii traktujemy jak błędy.
tsan:
	@$(MAKE) BUILD=build-tsan/$(HOST_TAG) EXTRA="-fsanitize=thread" run

# Przykłady kompilujemy na backendzie hostowym. To sprawdza dwie rzeczy naraz:
# że API biblioteki się nie rozjechało i że przykład nie sięga po Arduino —
# gdyby sięgał, na hoście nie byłoby czego dołączyć.
EXAMPLES = $(wildcard ../examples/*/main.cpp)
# Bez -MMD/-MP: przy -fsyntax-only generowałyby osierocone pliki .d.
SYNTAX_FLAGS = $(filter-out -MMD -MP,$(CXXFLAGS))

# Aplikacja urządzenia wymaga pliku płytki tego urządzenia — rover odwołuje się
# do nazw takich jak Pin::MotorLeftPwm, których ogólny plik modułu nie zna.
# To nie niedogodność, tylko sedno warstwy boards/.
examples:
	@for f in $(EXAMPLES); do \
		case "$$f" in \
			*rover*)  board=rover_s3 ;; \
			*native*) board=native ;; \
			*)        board=esp32s3_pico ;; \
		esac; \
		printf '  %-40s (%s)\n' "$$f" "$$board"; \
		$(CXX) $(SYNTAX_FLAGS) -fsyntax-only \
			-D HYDRA_BOARD_HEADER="\"hydra/boards/$$board.hpp\"" "$$f" || exit 1; \
	done
	@printf '  %-40s (%s)\n' "../templates/starter/src/main.cpp" "esp32s3_pico"
	@$(CXX) $(SYNTAX_FLAGS) -fsyntax-only \
		-D HYDRA_BOARD_HEADER='"hydra/boards/esp32s3_pico.hpp"' \
		../templates/starter/src/main.cpp
	@printf 'Przykłady i szablon kompilują się na backendzie hostowym.\n'

# Sprawdzenie składni backendu Arduino na atrapach nagłówków. Nie zastępuje
# buildu `pio ci` — łapie literówki i błędy typów w gałęziach platformowych,
# których na tej maszynie nie da się skompilować inaczej.
# ESP32 występuje dwukrotnie, bo rdzenie 2.x i 3.x mają różne API LEDC,
# a kod obsługuje oba. Bez drugiego przejścia jedna z gałęzi nigdy nie
# przechodziłaby przez kompilator.
STUB_PLATFORMS = CONFIG_IDF_TARGET_ESP32S3:esp32s3_pico \
                 CONFIG_IDF_TARGET_ESP32S3,ESP_ARDUINO_VERSION_MAJOR=3:esp32s3_pico \
                 ARDUINO_ARCH_RP2040:rp2040_pico \
                 ARDUINO_ARCH_STM32:stm32_nucleo_g474
ARDUINO_SRC = $(wildcard ../src/hal/arduino/*.cpp) $(wildcard ../src/net/arduino/*.cpp) \
              $(wildcard ../src/ota/arduino/*.cpp)

stub:
	@for spec in $(STUB_PLATFORMS); do \
		def=$${spec%%:*}; board=$${spec##*:}; \
		defs=""; \
		for d in $$(printf '%s' "$$def" | tr ',' ' '); do defs="$$defs -D$$d"; done; \
		for f in $(ARDUINO_SRC); do \
			$(CXX) -std=c++17 -fsyntax-only -fno-exceptions -fno-rtti \
				-Wall -Wextra -Wno-unused-parameter \
				$$defs -D HYDRA_ENABLE_NET=1 -D HYDRA_ENABLE_OTA=1 \
				-D HYDRA_BOARD_HEADER="\"hydra/boards/$$board.hpp\"" \
				-I arduino_stub -I ../include "$$f" || exit 1; \
		done; \
		printf '  %s\n' "$$def"; \
	done
	@printf 'Backend Arduino przechodzi kontrolę składni dla wszystkich platform.\n'

# Fragmenty z docs/api.md w postaci kompilowalnej. Dokumentacja opisująca
# nieistniejące funkcje wygląda wiarygodnie i kosztuje czytelnika godzinę,
# zanim zorientuje się, że problem jest w tekście.
docs:
	@echo "  SPRAWDZAM  docs/api.md"
	@$(CXX) -std=c++17 -fsyntax-only -fno-exceptions -fno-rtti -Wall -Wextra \
	  -DHYDRA_FORCE_HOST -DHYDRA_ENABLE_SENSE=1 -DHYDRA_ENABLE_NET=1 \
	  -DHYDRA_ENABLE_UI=1 -DHYDRA_ENABLE_MOTION=1 -DHYDRA_ENABLE_OTA=1 -DHYDRA_ENABLE_SCRIPT=1 -DHYDRA_ENABLE_MINIS=1 -DHYDRA_ENABLE_MEDIA=1 -DHYDRA_ENABLE_ARDUBOY=1 \
	  -DHYDRA_BOARD_HEADER='"hydra/boards/esp32s3_pico.hpp"' \
	  -I../include ../docs/snippets.cpp
	@echo "  Fragmenty dokumentacji zgodne z API."
	@echo "  SPRAWDZAM  powierzchnia importow WASM"
	@python3 ../tools/gen_bindings.py --check

clean:
	@rm -rf build build-asan build-tsan

-include $(DEP)
