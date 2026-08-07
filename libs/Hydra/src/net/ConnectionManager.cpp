/** Hydra — implementacja maszyny stanów połączenia (rozdz. 7.1). */

#include "hydra/net/ConnectionManager.hpp"

#if HYDRA_ENABLE_NET

#include <stdio.h>
#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("net.conn")

namespace hydra {
namespace net {
namespace {

/** Przestrzeń nazw w IStorage z listą sieci. */
constexpr const char* kStorageNamespace = "hydra-net";

/**
 * Klucze budujemy osobno dla nazwy i klucza sieci. Zapis obu w jednym rekordzie
 * przekroczyłby limit wartości emulowanego EEPROM-u na RP2 i STM32, a to
 * właśnie tam pamięć trwała jest najciaśniejsza.
 */
void ssidKey(u8 index, char* out, size_t cap) { snprintf(out, cap, "n%us", index); }
void pskKey(u8 index, char* out, size_t cap)  { snprintf(out, cap, "n%up", index); }

}  // namespace

// ---------------------------------------------------------------------------
// Konfiguracja i lista sieci
// ---------------------------------------------------------------------------

Status ConnectionManager::init(const Config& cfg) {
    if (cfg.backoffBaseMs == 0 || cfg.backoffMaxMs < cfg.backoffBaseMs) {
        return fail(Err::BadArgument);
    }
    if (cfg.attemptsPerNetwork == 0) return fail(Err::BadArgument);

    cfg_ = cfg;
    HYDRA_CHECK(iface_.begin());
    initialized_ = true;
    return ok();
}

Status ConnectionManager::addNetwork(const NetworkCredentials& creds) {
    if (!creds.valid()) return fail(Err::BadArgument);
    if (count_ >= HYDRA_NET_MAX_NETWORKS) return fail(Err::OutOfMemory);

    networks_[count_]          = creds;
    networks_[count_].priority = count_;
    ++count_;
    return ok();
}

const NetworkCredentials* ConnectionManager::network(u8 index) const {
    return index < count_ ? &networks_[index] : nullptr;
}

Status ConnectionManager::loadNetworks() {
    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kStorageNamespace, true));

    // Wczytujemy obok, a nie na miejscu: pusta pamięć nie może skasować sieci
    // podanych programowo przed startem. Inaczej urządzenie z poświadczeniami
    // wkompilowanymi na sztywno traciłoby je przy pierwszym uruchomieniu.
    NetworkCredentials loaded[HYDRA_NET_MAX_NETWORKS];
    u8                 found = 0;

    for (u8 i = 0; i < HYDRA_NET_MAX_NETWORKS; ++i) {
        char key[hal::kStorageKeyMax + 1];
        ssidKey(i, key, sizeof(key));

        NetworkCredentials creds;
        auto ssid = storage.getString(key, creds.ssid, sizeof(creds.ssid));
        if (!ssid || creds.ssid[0] == '\0') continue;

        char pskBuf[kPskMax] = {};
        pskKey(i, key, sizeof(key));
        auto psk = storage.getString(key, pskBuf, sizeof(pskBuf));
        if (psk) creds.psk.set(pskBuf);
        // Kopia jawna znika natychmiast — dalej żyje wyłącznie w SecretString.
        memset(pskBuf, 0, sizeof(pskBuf));

        creds.priority = found;
        loaded[found++] = creds;
    }

    if (found == 0) return fail(Err::NotFound);

    for (u8 i = 0; i < found; ++i) networks_[i] = loaded[i];
    count_ = found;

    HYDRA_LOGI("wczytano sieci: %u", static_cast<unsigned>(count_));
    return ok();
}

Status ConnectionManager::saveNetworks() const {
    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kStorageNamespace, false));

    for (u8 i = 0; i < count_; ++i) {
        char key[hal::kStorageKeyMax + 1];
        ssidKey(i, key, sizeof(key));
        HYDRA_CHECK(storage.setString(key, networks_[i].ssid));

        pskKey(i, key, sizeof(key));
        HYDRA_CHECK(storage.setString(key, networks_[i].psk.reveal()));
    }
    return storage.commit();
}

Status ConnectionManager::forgetNetworks() {
    auto& storage = hal::Hal::storage();
    HYDRA_CHECK(storage.begin(kStorageNamespace, false));
    HYDRA_CHECK(storage.eraseAll());
    count_ = 0;
    return storage.commit();
}

// ---------------------------------------------------------------------------
// Maszyna stanów
// ---------------------------------------------------------------------------

u32 ConnectionManager::backoffFor(u32 attempt) const {
    // Podwajanie z twardym sufitem. Przesunięcie ograniczamy do 16 bitów,
    // żeby długa seria niepowodzeń nie przekręciła licznika.
    const u32 shift = attempt > 16 ? 16 : attempt;
    const u64 delay = static_cast<u64>(cfg_.backoffBaseMs) << shift;
    return delay > cfg_.backoffMaxMs ? cfg_.backoffMaxMs : static_cast<u32>(delay);
}

void ConnectionManager::transition(ConnState next, Millis now) {
    if (next == state_) return;

    const ConnState prev = state_;
    state_      = next;
    stateSince_ = now;

    EventBus::publish(ConnStateChanged{prev, next, current_, attempt_});
    HYDRA_LOGI("%s → %s (sieć %u, próba %lu)", toString(prev), toString(next),
               static_cast<unsigned>(current_), static_cast<unsigned long>(attempt_));
}

void ConnectionManager::beginAttempt(Millis now) {
    ++attempt_;
    ++networkTries_;

    const NetworkCredentials& creds = networks_[current_];
    HYDRA_LOGI("łączenie z '%s' (klucz: %s)", creds.ssid, creds.psk.masked());

    if (auto r = iface_.connect(creds); !r) {
        HYDRA_LOGW("interfejs odrzucił próbę: %s", toString(r.error()));
        ++stats_.failures;
        scheduleRetry(now);
        return;
    }
    transition(ConnState::Connecting, now);
}

void ConnectionManager::scheduleRetry(Millis now) {
    // Po wyczerpaniu prób na jednej sieci przechodzimy do kolejnej. Licznik
    // backoffu celowo nie jest zerowany: gdyby był, urządzenie z dwiema
    // nieosiągalnymi sieciami dobijałoby się do nich co sekundę bez końca.
    if (count_ > 1 && networkTries_ >= cfg_.attemptsPerNetwork) {
        networkTries_ = 0;
        current_      = static_cast<u8>((current_ + 1) % count_);
        HYDRA_LOGI("przełączenie na sieć %u ('%s')", static_cast<unsigned>(current_),
                   networks_[current_].ssid);
    }

    retryAt_ = now + backoffFor(attempt_);
    transition(ConnState::Reconnecting, now);
}

Status ConnectionManager::start(Millis now) {
    if (!initialized_) return fail(Err::NotInitialized);
    if (count_ == 0) return fail(Err::NotFound);
    if (state_ != ConnState::Idle) return ok();

    attempt_      = 0;
    networkTries_ = 0;
    current_      = 0;
    beginAttempt(now);
    return ok();
}

void ConnectionManager::stop() {
    iface_.disconnect();
    state_       = ConnState::Idle;
    attempt_     = 0;
    networkTries_ = 0;
    onlineSince_ = 0;
}

void ConnectionManager::reportServiceHealth(bool healthy) { serviceHealthy_ = healthy; }

void ConnectionManager::tick(Millis now) {
    switch (state_) {
        case ConnState::Idle:
            break;

        case ConnState::Connecting:
            if (iface_.linkUp()) {
                attempt_      = 0;
                networkTries_ = 0;
                onlineSince_  = now;
                ++stats_.connects;
                transition(ConnState::Online, now);
                EventBus::publish(
                    NetGotAddress{iface_.localIpV4(), iface_.rssiDbm(), current_});
                break;
            }
            if (now - stateSince_ >= cfg_.connectTimeoutMs) {
                HYDRA_LOGW("przekroczony czas łączenia z '%s'", networks_[current_].ssid);
                ++stats_.failures;
                iface_.disconnect();
                scheduleRetry(now);
            }
            break;

        case ConnState::Online:
        case ConnState::Degraded: {
            if (!iface_.linkUp()) {
                const u32 uptime = onlineSince_ ? (now - onlineSince_) / 1000u : 0;
                stats_.totalOnlineSec += uptime;
                ++stats_.disconnects;
                EventBus::publish(NetLost{Err::IoError, uptime});
                HYDRA_LOGW("łącze zerwane po %lus", static_cast<unsigned long>(uptime));
                scheduleRetry(now);
                break;
            }

            // Sygnał zerowy zgłaszają łącza przewodowe — nie ma czego oceniać.
            const i8   rssi = iface_.rssiDbm();
            const bool weak = rssi != 0 && rssi < cfg_.degradedRssiDbm;
            const bool degraded = weak || !serviceHealthy_;

            transition(degraded ? ConnState::Degraded : ConnState::Online, now);
            break;
        }

        case ConnState::Reconnecting:
            // Porównanie odporne na przepełnienie licznika milisekund.
            if (static_cast<i32>(now - retryAt_) >= 0) beginAttempt(now);
            break;
    }
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
