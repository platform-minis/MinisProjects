#pragma once
/**
 * Hydra — druga warstwa konfiguracji: plik płytki (rozdz. 13).
 *
 * Plik płytki opisuje, co jest gdzie podłączone. Wskazuje się go flagą
 * kompilacji, a nie edycją kodu:
 *
 *     build_flags = -I boards -D HYDRA_BOARD_HEADER='"esp32s3_pico.hpp"'
 *
 * Każdy parametr ma sensowną wartość domyślną, więc płytka definiuje tylko to,
 * co ją wyróżnia. kNoPin oznacza „użyj domyślnego pinu core'a Arduino", a nie
 * „brak" — to rozróżnienie jest istotne przy I2C i SPI, gdzie cory mają własne
 * mapowania.
 */

#include "hydra/hal/Pin.hpp"

#if defined(HYDRA_BOARD_HEADER)
#  include HYDRA_BOARD_HEADER
#endif

// --- nazwa płytki ----------------------------------------------------------
#ifndef HYDRA_BOARD_NAME
#  define HYDRA_BOARD_NAME "generic"
#endif

// --- dioda statusu ---------------------------------------------------------
#ifndef HYDRA_BOARD_LED
#  ifdef LED_BUILTIN
#    define HYDRA_BOARD_LED LED_BUILTIN
#  else
#    define HYDRA_BOARD_LED ::hydra::hal::kNoPin
#  endif
#endif

/** Dioda świeci przy stanie niskim (typowe dla płytek z LED do VCC). */
#ifndef HYDRA_BOARD_LED_ACTIVE_LOW
#  define HYDRA_BOARD_LED_ACTIVE_LOW 0
#endif

// --- I2C -------------------------------------------------------------------
#ifndef HYDRA_BOARD_I2C0_ENABLE
#  define HYDRA_BOARD_I2C0_ENABLE 1
#endif
#ifndef HYDRA_BOARD_I2C0_SDA
#  define HYDRA_BOARD_I2C0_SDA ::hydra::hal::kNoPin
#endif
#ifndef HYDRA_BOARD_I2C0_SCL
#  define HYDRA_BOARD_I2C0_SCL ::hydra::hal::kNoPin
#endif
#ifndef HYDRA_BOARD_I2C0_HZ
#  define HYDRA_BOARD_I2C0_HZ 400000
#endif

// --- SPI -------------------------------------------------------------------
#ifndef HYDRA_BOARD_SPI0_ENABLE
#  define HYDRA_BOARD_SPI0_ENABLE 0
#endif
#ifndef HYDRA_BOARD_SPI0_SCK
#  define HYDRA_BOARD_SPI0_SCK ::hydra::hal::kNoPin
#endif
#ifndef HYDRA_BOARD_SPI0_MISO
#  define HYDRA_BOARD_SPI0_MISO ::hydra::hal::kNoPin
#endif
#ifndef HYDRA_BOARD_SPI0_MOSI
#  define HYDRA_BOARD_SPI0_MOSI ::hydra::hal::kNoPin
#endif

// --- UART ------------------------------------------------------------------
#ifndef HYDRA_BOARD_UART0_ENABLE
#  define HYDRA_BOARD_UART0_ENABLE 1
#endif
#ifndef HYDRA_BOARD_UART0_BAUD
#  define HYDRA_BOARD_UART0_BAUD 115200
#endif
#ifndef HYDRA_BOARD_UART0_RX
#  define HYDRA_BOARD_UART0_RX ::hydra::hal::kNoPin
#endif
#ifndef HYDRA_BOARD_UART0_TX
#  define HYDRA_BOARD_UART0_TX ::hydra::hal::kNoPin
#endif

// --- pamięć trwała ---------------------------------------------------------
#ifndef HYDRA_BOARD_STORAGE_NS
#  define HYDRA_BOARD_STORAGE_NS "hydra"
#endif
/** Rozmiar emulowanego EEPROM-u na platformach bez NVS. */
#ifndef HYDRA_BOARD_EEPROM_SIZE
#  define HYDRA_BOARD_EEPROM_SIZE 2048
#endif

namespace hydra {
namespace hal {
namespace board {

constexpr const char* name = HYDRA_BOARD_NAME;
constexpr PinNum      led  = HYDRA_BOARD_LED;
constexpr bool        ledActiveLow = HYDRA_BOARD_LED_ACTIVE_LOW != 0;

}  // namespace board
}  // namespace hal
}  // namespace hydra
