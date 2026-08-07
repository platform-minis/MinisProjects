#pragma once
/**
 * Hydra — zapis okoliczności awarii i raport po restarcie (rozdz. 13).
 *
 * Urządzenie wbudowane, które się zrestartowało, zwykle nie ma komu o tym
 * powiedzieć: nikt nie patrzył na port szeregowy, a po resecie pamięć RAM
 * jest już wyczyszczona. Ten moduł przenosi przez reset trzy rzeczy:
 * przyczynę resetu z rejestrów procesora, kontekst zapisany przez
 * oprogramowanie tuż przed awarią oraz ogon bufora logów.
 *
 * Zapis idzie do IStorage, a nie do pamięci podtrzymywanej — ta ostatnia
 * jest szybsza i nie zużywa Flasha, ale nie przeżywa zaniku zasilania,
 * czyli akurat tego przypadku, który najtrudniej odtworzyć przy biurku.
 *
 * Ograniczenie warte odnotowania: framework nie zbiera śladu stosu.
 * Wymagałoby to przechwycenia procedury obsługi wyjątku procesora, która
 * na każdej platformie wygląda inaczej i bywa już zajęta przez SDK vendora.
 * Zapisywany jest natomiast kontekst podany jawnie — nazwa taska, kod błędu,
 * krótki opis — co w praktyce lokalizuje problem równie skutecznie.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace diag {

/** Długość opisu zapisywanego razem z awarią. */
constexpr size_t kCrashDetailMax = 48;
/** Ile bajtów ogona logu przenosi się przez reset. */
#ifndef HYDRA_CRASH_LOG_BYTES
#  define HYDRA_CRASH_LOG_BYTES 256
#endif

/** Zapis okoliczności ostatniej awarii. */
struct CrashRecord {
    ResetReason reason   = ResetReason::Unknown;
    u32         uptimeMs = 0;   ///< czas pracy przed awarią
    u16         sourceId = 0;   ///< nameId taska albo modułu
    u16         code     = 0;   ///< kod błędu specyficzny dla źródła
    u32         bootCount = 0;  ///< który to rozruch od pierwszego uruchomienia
    char        detail[kCrashDetailMax] = {};

    bool valid() const { return reason != ResetReason::Unknown || detail[0] != '\0'; }
};

/** Raport o poprzednim rozruchu, publikowany po starcie. */
struct CrashReported {
    ResetReason reason;
    u32         uptimeMs;
    u16         sourceId;
    u16         code;
};

class CrashRecorder {
public:
    /**
     * Wczytuje zapis z poprzedniego rozruchu i zwiększa licznik rozruchów.
     * Wołane wcześnie, zanim moduły zdążą cokolwiek nadpisać.
     */
    Status begin();

    /** Czy poprzedni rozruch zakończył się czymś wartym odnotowania. */
    bool hasPrevious() const { return hasPrevious_; }
    CrashRecord previous() const { return previous_; }
    u32 bootCount() const { return bootCount_; }

    /**
     * Zapisuje kontekst awarii. Wołane tuż przed zamierzonym restartem
     * albo z procedury obsługi błędu krytycznego.
     */
    Status record(ResetReason reason, u16 sourceId, u16 code, const char* detail);

    /** Dokłada do zapisu ogon bufora logów. */
    Status saveLogTail();
    /** Odczytuje zachowany ogon logu z poprzedniego rozruchu. */
    Result<size_t> loadLogTail(char* out, size_t capacity);

    /** Publikuje raport na magistrali i kasuje zapis. */
    Status publishAndClear();
    Status clear();

private:
    CrashRecord previous_{};
    bool        hasPrevious_ = false;
    u32         bootCount_   = 0;
};

}  // namespace diag
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::diag::CrashReported, "diag/crash")
