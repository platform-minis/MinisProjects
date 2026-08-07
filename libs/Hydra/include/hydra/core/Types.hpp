#pragma once
/**
 * Hydra — podstawowe typy współdzielone przez wszystkie warstwy.
 */

#include <stdint.h>
#include <stddef.h>

#include "hydra/core/Config.hpp"

namespace hydra {

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

/** Czas monotoniczny w milisekundach od startu. */
using Millis = u32;
/** Czas monotoniczny w mikrosekundach — znaczniki próbek czujników. */
using Micros = u64;

/** Identyfikator tematu na magistrali zdarzeń (rozdz. 4.3). */
using TopicId = u16;
constexpr TopicId kInvalidTopic = 0;

/** Priorytety tasków — mapowane na zakres FreeRTOS danej platformy (rozdz. 4.2). */
enum class Prio : u8 {
    Idle    = 0,  ///< core.house — watchdog, statystyki
    Low     = 1,  ///< ui.render
    Normal  = 2,  ///< net.worker
    High    = 3,  ///< sense.poll
    Realtime = 4, ///< motion.control
};

/** Wybór rdzenia na platformach SMP. Ignorowany tam, gdzie rdzeń jest jeden. */
enum class Core : i8 {
    Any  = -1,
    Core0 = 0,  ///< sieć, UI (na ESP32 rdzeń 0 obsługuje stos Wi-Fi)
    Core1 = 1,  ///< taski czasu rzeczywistego
};

/** Kody błędów frameworka — zamiast wyjątków (rozdz. 11). */
enum class Err : u8 {
    None = 0,
    NotFound,
    NotSupported,
    Timeout,
    Busy,
    IoError,
    BadArgument,
    OutOfMemory,
    OutOfRange,
    NotInitialized,
    AlreadyExists,
    WouldBlock,
    Protocol,
    Internal,
};

/** Nazwa kodu błędu — do logów i shella diagnostycznego. */
constexpr const char* toString(Err e) {
    switch (e) {
        case Err::None:           return "none";
        case Err::NotFound:       return "not-found";
        case Err::NotSupported:   return "not-supported";
        case Err::Timeout:        return "timeout";
        case Err::Busy:           return "busy";
        case Err::IoError:        return "io-error";
        case Err::BadArgument:    return "bad-argument";
        case Err::OutOfMemory:    return "out-of-memory";
        case Err::OutOfRange:     return "out-of-range";
        case Err::NotInitialized: return "not-initialized";
        case Err::AlreadyExists:  return "already-exists";
        case Err::WouldBlock:     return "would-block";
        case Err::Protocol:       return "protocol";
        case Err::Internal:       return "internal";
    }
    return "unknown";
}

/**
 * Przyczyna ostatniego resetu (rozdz. 13, crash handling).
 * Źródłem jest HAL, konsumentem telemetria — dlatego typ mieszka tutaj,
 * a nie w nagłówku zdarzeń, do którego warstwa HAL nie ma prawa sięgać.
 */
enum class ResetReason : u8 {
    Unknown = 0,
    PowerOn,
    Software,
    Watchdog,
    Panic,
    Brownout,
    DeepSleep,
};

constexpr const char* toString(ResetReason r) {
    switch (r) {
        case ResetReason::Unknown:  return "unknown";
        case ResetReason::PowerOn:  return "power-on";
        case ResetReason::Software: return "software";
        case ResetReason::Watchdog: return "watchdog";
        case ResetReason::Panic:    return "panic";
        case ResetReason::Brownout: return "brownout";
        case ResetReason::DeepSleep: return "deep-sleep";
    }
    return "unknown";
}

/** Jakość próbki czujnika (rozdz. 8). */
enum class Quality : u8 {
    Good = 0,
    Stale,      ///< wartość zamrożona — czujnik nie odświeża danych
    Suspect,    ///< skok poza spodziewany zakres
    Bad,        ///< odczyt nieudany
};

/** Klasa bazowa blokująca kopiowanie — dla obiektów będących właścicielami zasobów. */
class NonCopyable {
protected:
    constexpr NonCopyable() = default;
    ~NonCopyable() = default;
public:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

/** Widok na ciągły obszar pamięci — zamiennik std::span (C++20) dla C++17. */
template <typename T>
class Span {
public:
    constexpr Span() = default;
    constexpr Span(T* data, size_t size) : data_(data), size_(size) {}
    template <size_t N>
    constexpr Span(T (&arr)[N]) : data_(arr), size_(N) {}

    constexpr T*     data()  const { return data_; }
    constexpr size_t size()  const { return size_; }
    constexpr bool   empty() const { return size_ == 0; }
    constexpr T*     begin() const { return data_; }
    constexpr T*     end()   const { return data_ + size_; }
    constexpr T&     operator[](size_t i) const { return data_[i]; }

private:
    T*     data_ = nullptr;
    size_t size_ = 0;
};

using ByteSpan  = Span<u8>;
using CByteSpan = Span<const u8>;

}  // namespace hydra
