/**
 * @file avr/pgmspace.h
 * @brief Przekierowanie dla gier włączających nagłówek AVR wprost.
 *
 * Część gier robi `#include <avr/pgmspace.h>` obok `<Arduboy2.h>`. Na celu
 * natywnym taki plik nie istnieje; makra dostarcza nasza atrapa Arduino.
 * Na układzie nagłówek AVR też nie istnieje, ale i tam odpowiednie makra
 * przychodzą z prawdziwego `Arduino.h`.
 */
#ifndef HYDRA_COMPAT_AVR_PGMSPACE_H
#define HYDRA_COMPAT_AVR_PGMSPACE_H

#include "hydra/core/Config.hpp"

#if HYDRA_PLAT_HOST
#  include "../ArduinoCompat.h"
#else
#  include <Arduino.h>
#endif

#endif  // HYDRA_COMPAT_AVR_PGMSPACE_H
