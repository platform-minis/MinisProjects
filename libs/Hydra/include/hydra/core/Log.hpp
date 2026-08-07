#pragma once
/**
 * Hydra — logowanie z poziomami per moduł (rozdz. 13).
 *
 * Trzy mechanizmy w jednym:
 *   1. filtr dwustopniowy — kompilacyjny (HYDRA_LOG_COMPILE_LEVEL) i runtime
 *      (globalny + per moduł), więc log wyłączony nie kosztuje nawet skoku;
 *   2. bufor pierścieniowy w RAM — zrzucany po awarii (Log::dump);
 *   3. tryb deferowany — formatowanie zostaje u wołającego, ale zapis do sinka
 *      (UART/USB) przenosi się do taska core.house, żeby log nie psuł czasów
 *      pętli czasu rzeczywistego.
 *
 * Logowanie z ISR jest zabronione (rozdz. 10) — wywołanie z przerwania jest
 * bezpiecznie ignorowane i liczone w statystykach.
 */

#include <stdarg.h>

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {

enum class LogLevel : u8 {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Off   = 5,
};

constexpr const char* toString(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRC";
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info:  return "INF";
        case LogLevel::Warn:  return "WRN";
        case LogLevel::Error: return "ERR";
        case LogLevel::Off:   return "OFF";
    }
    return "???";
}

/** Poziom wycinany na etapie kompilacji — wszystko poniżej znika z binarki. */
#ifndef HYDRA_LOG_COMPILE_LEVEL
#  if HYDRA_PLAT_HOST
#    define HYDRA_LOG_COMPILE_LEVEL 0  /* Trace */
#  else
#    define HYDRA_LOG_COMPILE_LEVEL 1  /* Debug */
#  endif
#endif

/** Odbiornik sformatowanych linii logu. */
class ILogSink {
public:
    virtual ~ILogSink() = default;
    /** Linia bez znaku nowej linii. Implementacja odpowiada za zakończenie wiersza. */
    virtual void write(LogLevel level, const char* line, size_t len) = 0;
};

class Log {
public:
    enum class Mode : u8 {
        Sync,      ///< zapis do sinków w kontekście wołającego
        Deferred,  ///< zapis do sinków przeniesiony do Log::drain() (core.house)
    };

    struct Stats {
        u32 emitted     = 0;  ///< linie przyjęte
        u32 filtered    = 0;  ///< linie odrzucone przez filtr runtime
        u32 ringDropped = 0;  ///< linie nadpisane w buforze przed wysłaniem
        u32 isrDropped  = 0;  ///< próby logowania z ISR
    };

    static void init(LogLevel level = LogLevel::Info, Mode mode = Mode::Sync);

    /** Rejestruje sink. Sink musi żyć dłużej niż Log (zwykle obiekt statyczny). */
    static Status addSink(ILogSink& sink);
    static void   clearSinks();

    static void     setLevel(LogLevel l);
    static LogLevel level();
    static void     setMode(Mode m);

    /** Poziom dla konkretnego modułu, np. setModuleLevel("net", LogLevel::Trace). */
    static Status   setModuleLevel(const char* module, LogLevel l);
    static LogLevel moduleLevel(const char* module);

    /** Filtr runtime — używany przez makra przed formatowaniem argumentów. */
    static bool enabled(const char* module, LogLevel l);

    static void write(LogLevel l, const char* module, const char* fmt, ...);
    static void vwrite(LogLevel l, const char* module, const char* fmt, va_list ap);

    /**
     * Wypycha zbuforowane linie do sinków (tryb Deferred).
     * Zwraca liczbę wysłanych linii. Wołane przez task core.house.
     */
    static u32 drain(u32 maxLines = 16);

    /**
     * Zrzuca zawartość bufora pierścieniowego do bufora wołającego —
     * do publikacji po awarii (rozdz. 13, crash handling).
     * Zwraca liczbę zapisanych bajtów (bez terminatora).
     */
    static size_t dump(char* out, size_t cap);

    static Stats stats();
    /** Zeruje stan loggera. Wyłącznie do testów jednostkowych. */
    static void reset();
};

#if HYDRA_PLAT_HOST
/** Sink piszący na stdout — dostępny tylko w buildzie hostowym. */
ILogSink& stdoutSink();
#endif

}  // namespace hydra

/**
 * Nazwa modułu logującego dla bieżącego pliku.
 * Wymagana przed użyciem makr HYDRA_LOG*.
 */
#define HYDRA_LOG_MODULE(name) \
    namespace { constexpr const char* kHydraLogModule = name; }

/**
 * Filtr kompilacyjny. Przy progu Trace porównanie jest zawsze prawdziwe
 * i GCC słusznie to zgłasza (-Wtype-limits) — wtedy pomijamy je zupełnie,
 * zamiast zagłuszać ostrzeżenie.
 */
#if HYDRA_LOG_COMPILE_LEVEL <= 0
#  define HYDRA_LOG_COMPILED_IN(lvl) true
#else
#  define HYDRA_LOG_COMPILED_IN(lvl) (static_cast<int>(lvl) >= HYDRA_LOG_COMPILE_LEVEL)
#endif

#define HYDRA_LOG_AT(lvl, mod, ...)                                            \
    do {                                                                       \
        if (HYDRA_LOG_COMPILED_IN(lvl) &&                                      \
            ::hydra::Log::enabled((mod), (lvl))) {                             \
            ::hydra::Log::write((lvl), (mod), __VA_ARGS__);                    \
        }                                                                      \
    } while (0)

#define HYDRA_LOGT(...) HYDRA_LOG_AT(::hydra::LogLevel::Trace, kHydraLogModule, __VA_ARGS__)
#define HYDRA_LOGD(...) HYDRA_LOG_AT(::hydra::LogLevel::Debug, kHydraLogModule, __VA_ARGS__)
#define HYDRA_LOGI(...) HYDRA_LOG_AT(::hydra::LogLevel::Info,  kHydraLogModule, __VA_ARGS__)
#define HYDRA_LOGW(...) HYDRA_LOG_AT(::hydra::LogLevel::Warn,  kHydraLogModule, __VA_ARGS__)
#define HYDRA_LOGE(...) HYDRA_LOG_AT(::hydra::LogLevel::Error, kHydraLogModule, __VA_ARGS__)
