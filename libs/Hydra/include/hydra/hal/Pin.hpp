#pragma once
/**
 * Hydra — numeracja i tryby pinów (rozdz. 5).
 *
 * Aplikacja nigdy nie mówi „GPIO 17". Nazwy logiczne definiuje plik płytki
 * (katalog boards/), który mapuje je na fizyczne piny Arduino:
 *
 *     namespace Pin {
 *         constexpr hal::PinNum MotorLeftPwm = 17;
 *     }
 *
 * Dzięki temu ta sama aplikacja przenosi się na inną płytkę przez podmianę
 * jednego nagłówka, a nie przez przeszukiwanie kodu za magicznymi liczbami.
 */

#include "hydra/core/Types.hpp"

namespace hydra {
namespace hal {

/** Fizyczny numer pinu w numeracji Arduino danej płytki. */
using PinNum = i16;

/** Pin niepodłączony — poprawna wartość konfiguracyjna, nie błąd. */
constexpr PinNum kNoPin = -1;

enum class PinMode : u8 {
    Input = 0,
    InputPullUp,
    InputPullDown,
    Output,
    OutputOpenDrain,
    Analog,
};

/** Zbocze wyzwalające przerwanie. */
enum class Edge : u8 {
    None = 0,
    Rising,
    Falling,
    Both,
    LevelLow,
    LevelHigh,
};

constexpr const char* toString(PinMode m) {
    switch (m) {
        case PinMode::Input:           return "input";
        case PinMode::InputPullUp:     return "input-pullup";
        case PinMode::InputPullDown:   return "input-pulldown";
        case PinMode::Output:          return "output";
        case PinMode::OutputOpenDrain: return "output-od";
        case PinMode::Analog:          return "analog";
    }
    return "unknown";
}

}  // namespace hal
}  // namespace hydra
