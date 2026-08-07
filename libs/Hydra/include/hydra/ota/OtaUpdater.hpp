#pragma once
/**
 * Hydra — aktualizacja oprogramowania przez sieć (rozdz. 7.2).
 *
 * Pobiera obraz po HTTP, liczy jego skrót w locie, weryfikuje i zapisuje.
 * Trzy właściwości decydują o tym, czy ten mechanizm nadaje się do urządzenia,
 * którego nikt nie odwiedzi po wgraniu wadliwej wersji:
 *
 * **Weryfikacja przed przełączeniem.** Skrót liczony jest w trakcie pobierania,
 * a porównanie następuje przed commit(). Obraz uszkodzony w drodze nigdy nie
 * staje się aktywny.
 *
 * **Autentyczność osobno od spójności.** Sam skrót mówi, że plik nie uległ
 * uszkodzeniu — nie chroni przed podstawieniem cudzego obrazu, bo napastnik
 * policzy go równie dobrze. HMAC z kluczem współdzielonym to zmienia.
 *
 * **Tryb próbny po restarcie.** Nowy obraz startuje warunkowo i musi
 * potwierdzić, że działa. Brak potwierdzenia w zadanym czasie cofa
 * aktualizację przy kolejnym rozruchu. Bez tego pierwsza wersja, która
 * wstaje i natychmiast się wywraca, kończy wizytą z programatorem.
 *
 * Pobieranie nie blokuje: step() wykonuje ograniczoną porcję pracy i wraca,
 * więc mieści się w tasku sieciowym obok pozostałych zadań.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_OTA && HYDRA_ENABLE_NET

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Secret.hpp"
#include "hydra/net/ITransport.hpp"
#include "hydra/ota/IFirmwareStore.hpp"
#include "hydra/util/Sha256.hpp"

namespace hydra {
namespace ota {

/** Rozmiar porcji czytanej z gniazda w jednym kroku. */
#ifndef HYDRA_OTA_CHUNK
#  define HYDRA_OTA_CHUNK 512
#endif

enum class OtaState : u8 {
    Idle = 0,
    Connecting,
    Headers,        ///< odbiór nagłówków odpowiedzi
    Downloading,
    Verifying,
    Ready,          ///< obraz zapisany i zweryfikowany, czeka na restart
    Failed,
    PendingConfirm, ///< działający obraz jest w trybie próbnym
};

constexpr const char* toString(OtaState s) {
    switch (s) {
        case OtaState::Idle:           return "idle";
        case OtaState::Connecting:     return "connecting";
        case OtaState::Headers:        return "headers";
        case OtaState::Downloading:    return "downloading";
        case OtaState::Verifying:      return "verifying";
        case OtaState::Ready:          return "ready";
        case OtaState::Failed:         return "failed";
        case OtaState::PendingConfirm: return "pending-confirm";
    }
    return "unknown";
}

/** Postęp pobierania — do podglądu na ekranie i w telemetrii. */
struct OtaProgress {
    u32 received;
    u32 total;
    u8  percent;
    u8  state;
};

/** Zakończenie aktualizacji, pomyślne albo nie. */
struct OtaFinished {
    bool success;
    Err  error;
    u32  bytes;
};

class OtaUpdater {
public:
    struct Config {
        const char* host = nullptr;
        u16         port = 80;
        const char* path = "/firmware.bin";

        /** Oczekiwany skrót obrazu w zapisie szesnastkowym. */
        const char* expectedSha256 = nullptr;

        /**
         * Klucz do potwierdzenia autentyczności. Pusty oznacza sprawdzanie
         * wyłącznie spójności — dopuszczalne w sieci zamkniętej, nie w Internecie.
         */
        SecretString<64> hmacKey;

        u32 connectTimeoutMs = 10000;
        /** Maksymalny czas bez nowych danych, po którym pobieranie przerywamy. */
        u32 stallTimeoutMs   = 15000;
        /** Ile czasu po restarcie nowy obraz ma na potwierdzenie sprawności. */
        u32 confirmTimeoutMs = 60000;
    };

    struct Stats {
        u32 attempts   = 0;
        u32 successes  = 0;
        u32 failures   = 0;
        u32 rollbacks  = 0;
    };

    Status begin(net::IClient& client, IFirmwareStore& store, Millis now);

    /** Rozpoczyna aktualizację. Nie blokuje — postęp robi step(). */
    Status start(const Config& cfg, Millis now);
    /** Przerywa trwające pobieranie i porzuca zapisany fragment. */
    void   abort();

    /** Krok maszyny stanów. Wołany cyklicznie, np. z taska sieciowego. */
    void step(Millis now);

    OtaState state() const { return state_; }
    u8       percent() const;
    u32      received() const { return received_; }
    u32      total() const { return total_; }
    Err      lastError() const { return error_; }
    Stats    stats() const { return stats_; }

    /**
     * Potwierdza, że działający obraz jest sprawny. Aplikacja woła to
     * po zainicjalizowaniu wszystkiego, co uznaje za warunek poprawności —
     * na przykład po odzyskaniu łączności z brokerem.
     */
    Status confirmRunningImage();

    /** Wymusza powrót do poprzedniego obrazu. */
    Status rollback();

private:
    void   transition(OtaState next);
    void   finishWithError(Err error);
    Status sendRequest();
    void   consumeHeaders(Millis now);
    void   consumeBody(Millis now);
    void   verify();

    net::IClient*   client_ = nullptr;
    IFirmwareStore* store_  = nullptr;
    Config          cfg_{};

    OtaState state_ = OtaState::Idle;
    Err      error_ = Err::None;

    u32    received_    = 0;
    u32    total_       = 0;
    Millis lastData_    = 0;
    Millis confirmDeadline_ = 0;

    util::Sha256     sha_;
    util::HmacSha256 hmac_;
    bool             useHmac_ = false;

    /** Bufor nagłówków odpowiedzi; wystarcza na typową odpowiedź serwera. */
    char   headerBuffer_[256] = {};
    size_t headerLength_      = 0;

    Stats stats_{};
};

}  // namespace ota
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::ota::OtaProgress, "ota/progress")
HYDRA_DECLARE_EVENT(hydra::ota::OtaFinished, "ota/finished")

#endif  // HYDRA_ENABLE_OTA && HYDRA_ENABLE_NET
