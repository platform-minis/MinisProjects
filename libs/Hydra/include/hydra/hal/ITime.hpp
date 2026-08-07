#pragma once
/**
 * Hydra — czas monotoniczny i kalendarzowy (rozdz. 5).
 *
 * Dwa źródła czasu są rozdzielone celowo. Czas monotoniczny (ms/µs od resetu)
 * jest zawsze dostępny i nigdy się nie cofa — to na nim opierają się okresy
 * pętli i znaczniki próbek. Czas kalendarzowy pochodzi z RTC albo z NTP,
 * może być nieustawiony i może skoczyć po synchronizacji; nadaje się do
 * telemetrii i logów, ale nie do mierzenia odstępów.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace hal {

/** Rozłożona data i czas UTC. */
struct DateTime {
    u16 year   = 1970;
    u8  month  = 1;   ///< 1–12
    u8  day    = 1;   ///< 1–31
    u8  hour   = 0;
    u8  minute = 0;
    u8  second = 0;
};

class ITime {
public:
    virtual ~ITime() = default;

    /** Czas monotoniczny od resetu. Zawsze dostępny, nigdy się nie cofa. */
    Millis monotonicMs() const { return rtos::nowMs(); }
    Micros monotonicUs() const { return rtos::nowUs(); }

    /** Czy zegar kalendarzowy został ustawiony (RTC z podtrzymaniem albo NTP). */
    virtual bool synchronized() const = 0;

    /** Czas uniksowy UTC. Err::NotInitialized, gdy zegar nieustawiony. */
    virtual Result<u64> epochSec() const = 0;
    virtual Status setEpochSec(u64 epoch) = 0;

    /** Rozłożona data UTC — nakładka nad epochSec(). */
    Result<DateTime> utc() const;

    /**
     * Znacznik czasu próbki w mikrosekundach — monotoniczny, więc nadaje się
     * do liczenia odstępów między odczytami czujników (rozdz. 8).
     */
    Micros sampleStamp() const { return monotonicUs(); }
};

/** Konwersja czasu uniksowego na datę UTC (algorytm dni od epoki). */
DateTime toDateTime(u64 epochSec);
/** Konwersja odwrotna. */
u64 toEpochSec(const DateTime& dt);

}  // namespace hal
}  // namespace hydra
