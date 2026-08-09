#pragma once
/**
 * Hydra — profil pamięciowy podsystemu skryptów.
 *
 * Wyłącznie dyrektywy preprocesora, bez ani jednej konstrukcji C++. To celowe:
 * nagłówek wciąga zarówno kod C++ Hydry, jak i `hydra_lua_conf.h` kompilowany
 * jako C razem ze źródłami Lua. Gdyby profil był zdefiniowany w dwóch miejscach,
 * prędzej czy później rozjechałby się i biblioteka zbudowałaby się z jednym
 * układem struktur, a jej użytkownik z drugim.
 */

#include "hydra/core/Config.hpp"

/**
 * Duży profil włącza się tam, gdzie RAM liczy się w setkach kilobajtów.
 *
 * Na RP2040/RP2350 i STM32 obowiązuje profil mały — nie dlatego, że interpreter
 * tam nie działa (działa, i to jest cały sens osadzenia oficjalnego Lua), tylko
 * dlatego, że skrypt nie ma prawa zabrać pamięci pętli sterowania. Kto ma
 * urządzenie z zapasem RAM-u i chce większych budżetów, podnosi je jedną flagą.
 */
#ifndef HYDRA_SCRIPT_LARGE_PROFILE
#  if HYDRA_PLAT_HOST || HYDRA_PLAT_ESP32
#    define HYDRA_SCRIPT_LARGE_PROFILE 1
#  else
#    define HYDRA_SCRIPT_LARGE_PROFILE 0
#  endif
#endif
