#pragma once
/**
 * Hydra — maszyna stanów połączenia (rozdz. 7.1).
 *
 *   Idle → Connecting → Online ⇄ Degraded
 *            ↑              ↓
 *          Reconnecting ←───┘
 *
 * Trzy decyzje projektowe warte odnotowania:
 *
 * 1. **Czas wchodzi argumentem, nie jest odczytywany w środku.** tick(now)
 *    dostaje bieżącą chwilę od wołającego. Dzięki temu całą maszynę — łącznie
 *    z wykładniczym backoffem i przełączaniem sieci zapasowych — da się
 *    przetestować deterministycznie, bez czekania w testach na realne minuty.
 *
 * 2. **Łączenie nie blokuje.** connect() na interfejsie tylko rozpoczyna
 *    procedurę; postęp sprawdzany jest w kolejnych tyknięciach. Blokujące
 *    oczekiwanie kilkanaście sekund zatrzymałoby task sieciowy razem z całą
 *    obsługą protokołów.
 *
 * 3. **Degraded to osobny stan.** Łącze działa, ale poniżej założonych
 *    parametrów — słaby sygnał albo warstwa wyżej zgłasza utratę usługi.
 *    Aplikacja może wtedy ograniczyć ruch, zamiast czekać na twarde zerwanie.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/Expected.hpp"
#include "hydra/net/ITransport.hpp"
#include "hydra/net/NetTypes.hpp"

namespace hydra {
namespace net {

class ConnectionManager {
public:
    struct Config {
        /** Ile czekamy na uzyskanie łącza, zanim uznamy próbę za nieudaną. */
        u32 connectTimeoutMs = 15000;
        /** Pierwsze odczekanie po nieudanej próbie. */
        u32 backoffBaseMs    = 1000;
        /** Górna granica odczekiwania — backoff rośnie wykładniczo do niej. */
        u32 backoffMaxMs     = 60000;
        /** Po tylu nieudanych próbach przechodzimy do kolejnej sieci z listy. */
        u8  attemptsPerNetwork = 3;
        /** Poniżej tej siły sygnału łącze uznajemy za osłabione. */
        i8  degradedRssiDbm  = -80;
    };

    struct Stats {
        u32 connects      = 0;  ///< udane połączenia
        u32 failures      = 0;  ///< nieudane próby
        u32 disconnects   = 0;  ///< zerwania już nawiązanego łącza
        u32 totalOnlineSec = 0;
    };

    explicit ConnectionManager(INetworkInterface& iface) : iface_(iface) {}

    Status init(const Config& cfg);

    /** Dodaje sieć do listy. Kolejność dodania wyznacza priorytet. */
    Status addNetwork(const NetworkCredentials& creds);
    u8     networkCount() const { return count_; }
    const NetworkCredentials* network(u8 index) const;

    /** Odczyt i zapis listy sieci w pamięci trwałej (rozdz. 7.1). */
    Status loadNetworks();
    Status saveNetworks() const;
    Status forgetNetworks();

    /** Rozpoczyna łączenie. Bez sieci na liście zwraca Err::NotFound. */
    Status start(Millis now);
    /** Rozłącza i wraca do stanu Idle. */
    void   stop();

    /** Jeden krok maszyny stanów. Wołane cyklicznie przez task net.worker. */
    void tick(Millis now);

    ConnState state() const { return state_; }
    u8        currentNetwork() const { return current_; }
    u32       attempt() const { return attempt_; }
    /** Chwila, w której nastąpi kolejna próba (stan Reconnecting). */
    Millis    retryAt() const { return retryAt_; }
    Stats     stats() const { return stats_; }

    /**
     * Zgłoszenie z warstwy protokołów: czy usługa działa. Utrata brokera przy
     * sprawnym łączu przenosi maszynę w Degraded, a nie w Reconnecting —
     * zrywanie Wi-Fi z powodu problemów brokera tylko wydłużałoby przestój.
     */
    void reportServiceHealth(bool healthy);

    /** Odczekanie przed próbą numer attempt — wyliczane, nie zapamiętane. */
    u32 backoffFor(u32 attempt) const;

private:
    void transition(ConnState next, Millis now);
    void beginAttempt(Millis now);
    void scheduleRetry(Millis now);

    INetworkInterface& iface_;
    Config             cfg_{};

    NetworkCredentials networks_[HYDRA_NET_MAX_NETWORKS];
    u8                 count_   = 0;
    u8                 current_ = 0;

    ConnState state_       = ConnState::Idle;
    u32       attempt_     = 0;
    u32       networkTries_ = 0;
    Millis    stateSince_  = 0;
    Millis    retryAt_     = 0;
    Millis    onlineSince_ = 0;
    bool      serviceHealthy_ = true;
    bool      initialized_    = false;

    Stats stats_{};
};

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
