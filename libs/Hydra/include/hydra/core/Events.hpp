#pragma once
/**
 * Hydra — standardowe zdarzenia systemowe rdzenia (rozdz. 4.3, 13).
 *
 * Wszystkie są trywialnie kopiowalnymi POD-ami mieszczącymi się w budżecie
 * HYDRA_EVENT_MAX_SIZE. Zdarzenia modułów (sense/net/motion) definiowane są
 * w ich własnych nagłówkach.
 */

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {

/**
 * Stabilny identyfikator liczbowy z nazwy (FNV-1a, 16 bit).
 * Używany dla tasków i modułów, żeby zdarzenia nie nosiły wskaźników na napisy.
 */
constexpr u16 nameId(const char* s) {
    u32 h = 2166136261u;
    for (; *s; ++s) {
        h ^= static_cast<u8>(*s);
        h *= 16777619u;
    }
    return static_cast<u16>((h >> 16) ^ (h & 0xFFFF));
}

/** Stan modułu w cyklu życia (rozdz. 4.1). */
enum class ModuleState : u8 {
    Created = 0,
    Initialized,
    Running,
    Stopped,
    Failed,
};

constexpr const char* toString(ModuleState s) {
    switch (s) {
        case ModuleState::Created:     return "created";
        case ModuleState::Initialized: return "initialized";
        case ModuleState::Running:     return "running";
        case ModuleState::Stopped:     return "stopped";
        case ModuleState::Failed:      return "failed";
    }
    return "unknown";
}

// ResetReason mieszka w Types.hpp — dostarcza go warstwa HAL.

// ---------------------------------------------------------------------------
// Zdarzenia
// ---------------------------------------------------------------------------

/** System wystartował: wszystkie moduły są w stanie Running. */
struct SysStarted {
    u32         bootMs;       ///< czas od resetu do zakończenia App::begin()
    ResetReason resetReason;
    u8          moduleCount;
};

/** Puls z taska core.house — podstawa telemetrii i wykrywania zawieszeń. */
struct SysHeartbeat {
    u32 uptimeMs;
    u32 freeHeapBytes;
    u32 minStackFreeBytes;  ///< najgorszy high-water-mark spośród tasków Hydry
    u16 taskCount;
};

/** Zmiana stanu modułu — miękki restart podsystemu jest widoczny na magistrali. */
struct ModuleStateChanged {
    u16         moduleId;  ///< nameId(module->name())
    ModuleState state;
    Err         error;     ///< Err::None, gdy zmiana przebiegła poprawnie
};

/** Task nie zmieścił się w swoim okresie (rozdz. 9). */
struct TaskDeadlineMissed {
    u16 taskId;       ///< nameId(task name)
    u16 overrunMs;    ///< o ile spóźniona iteracja
    u32 totalMisses;  ///< licznik narastający od startu
};

/** Zgłoszenie degradacji: podsystem działa, ale poza założonymi parametrami. */
struct SysDegraded {
    u16 sourceId;
    u16 code;
};

/** Utracone linie logu — bufor pierścieniowy nadpisał niewysłane dane. */
struct LogOverflow {
    u32 dropped;
};

/** Żądanie restartu urządzenia; obsługiwane przez warstwę aplikacji lub HAL. */
struct RebootRequest {
    u16 sourceId;
    u8  delaySec;
};

}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::SysStarted,         "sys/started")
HYDRA_DECLARE_EVENT(hydra::SysHeartbeat,       "sys/heartbeat")
HYDRA_DECLARE_EVENT(hydra::ModuleStateChanged, "sys/module")
HYDRA_DECLARE_EVENT(hydra::TaskDeadlineMissed, "sys/deadline")
HYDRA_DECLARE_EVENT(hydra::SysDegraded,        "sys/degraded")
HYDRA_DECLARE_EVENT(hydra::LogOverflow,        "sys/log-overflow")
HYDRA_DECLARE_EVENT(hydra::RebootRequest,      "sys/reboot")
