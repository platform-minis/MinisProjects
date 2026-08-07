#pragma once
/**
 * Hydra — typy i zdarzenia modułu ruchu (rozdz. 9).
 *
 * Jednostki są ustalone raz i obowiązują w całym module: prędkość liniowa
 * w metrach na sekundę, kątowa w radianach na sekundę, moc silnika w promilach
 * od -1000 do 1000. Promile nie są przypadkiem — to ta sama jednostka, w której
 * HAL przyjmuje wypełnienie PWM i w której joystick zwraca wychylenie, więc
 * droga od dotknięcia ekranu do napięcia na mostku nie wymaga po drodze ani
 * jednego przeliczenia.
 *
 * Wszystkie wielkości w ścieżce sterowania mają typ real_t: float tam, gdzie
 * jest FPU, i Q16.16 na RP2040 (rozdz. 15). Kod regulatorów i kinematyki jest
 * wspólny.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Fixed.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace motion {

/** Skrajna wartość mocy silnika. */
constexpr i16 kMaxPower = 1000;

/** Zadana prędkość pojazdu: liniowa i kątowa. */
struct Twist {
    real_t linear  = real(0.0f);  ///< m/s, dodatnia do przodu
    real_t angular = real(0.0f);  ///< rad/s, dodatnia w lewo
};

/** Prędkości obu kół w metrach na sekundę na styku z podłożem. */
struct WheelSpeeds {
    real_t left  = real(0.0f);
    real_t right = real(0.0f);
};

/** Położenie i orientacja w układzie odometrii. */
struct Pose {
    real_t x     = real(0.0f);  ///< m
    real_t y     = real(0.0f);  ///< m
    real_t theta = real(0.0f);  ///< rad, w zakresie [-π, π]
};

/** Stan łańcucha bezpieczeństwa (rozdz. 9). */
enum class SafetyState : u8 {
    Ready = 0,        ///< wolno jechać
    EmergencyStop,    ///< zatrzymanie awaryjne, wymaga jawnego skasowania
    CommandTimeout,   ///< brak zadania prędkości przez zadany czas
    OverCurrent,      ///< przekroczony limit prądu
    NotEnabled,       ///< napęd wyłączony programowo
};

constexpr const char* toString(SafetyState s) {
    switch (s) {
        case SafetyState::Ready:          return "ready";
        case SafetyState::EmergencyStop:  return "e-stop";
        case SafetyState::CommandTimeout: return "command-timeout";
        case SafetyState::OverCurrent:    return "over-current";
        case SafetyState::NotEnabled:     return "not-enabled";
    }
    return "unknown";
}

/** Powód zatrzymania awaryjnego — do diagnostyki po zdarzeniu. */
enum class StopReason : u8 {
    Unknown = 0,
    Operator,       ///< przycisk albo komenda
    CommandTimeout,
    OverCurrent,
    EncoderFault,
    DeadlineMissed,
};

// ---------------------------------------------------------------------------
// Zdarzenia
// ---------------------------------------------------------------------------

/** Zmiana stanu bezpieczeństwa napędu. */
struct SafetyChanged {
    SafetyState from;
    SafetyState to;
    StopReason  reason;
};

/** Okresowa migawka stanu napędu — podstawa telemetrii i podglądu. */
struct MotionState {
    float x        = 0.0f;  ///< m
    float y        = 0.0f;  ///< m
    float theta    = 0.0f;  ///< rad
    float linear   = 0.0f;  ///< m/s, wartość zmierzona
    float angular  = 0.0f;  ///< rad/s, wartość zmierzona
    u8    safety   = 0;     ///< SafetyState
};

/** Przekroczony limit prądu na którymś z kanałów. */
struct CurrentLimitTripped {
    u32 measuredMa;
    u32 limitMa;
    u8  channel;
};

}  // namespace motion
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::motion::SafetyChanged,       "motion/safety")
HYDRA_DECLARE_EVENT(hydra::motion::MotionState,         "motion/state")
HYDRA_DECLARE_EVENT(hydra::motion::CurrentLimitTripped, "motion/current")

#endif  // HYDRA_ENABLE_MOTION
