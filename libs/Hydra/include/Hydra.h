#pragma once
/**
 * Hydra — uniwersalny framework robotyczno-IoT dla ESP32, RP2040/RP2350 i STM32.
 *
 * Jedyny nagłówek, jaki musi włączyć aplikacja. Moduły opcjonalne (ui, net,
 * sense, motion) dołączają się same, gdy są włączone flagą kompilacji —
 * nieużywany moduł nie kosztuje ani bajta (rozdz. 1).
 *
 * Warstwy widoczne dla aplikacji:
 *   hydra::App       — cykl życia, punkt wejścia
 *   hydra::EventBus  — komunikacja między modułami
 *   hydra::Task      — pętle okresowe z egzekwowanym deadlinem
 *   hydra::Log       — logowanie z poziomami per moduł
 *   hydra::hal::*    — dostęp do sprzętu przez interfejsy (etap 1b)
 */

#include "hydra/core/App.hpp"
#include "hydra/core/Config.hpp"
#include "hydra/core/Delegate.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Fixed.hpp"
#include "hydra/core/IModule.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/core/Version.hpp"
