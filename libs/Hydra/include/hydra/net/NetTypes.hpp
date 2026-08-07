#pragma once
/**
 * Hydra — typy i zdarzenia modułu sieciowego (rozdz. 7).
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Secret.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace net {

/** Maksymalna długość nazwy sieci (SSID ma z definicji 32 znaki). */
constexpr size_t kSsidMax     = 33;
/** Klucz WPA2-PSK: 63 znaki plus terminator. */
constexpr size_t kPskMax      = 64;
/** Maksymalna długość tematu MQTT obsługiwana przez framework. */
constexpr size_t kTopicMax    = 64;
/** Maksymalna liczba sieci zapasowych (rozdz. 7.1). */
#ifndef HYDRA_NET_MAX_NETWORKS
#  define HYDRA_NET_MAX_NETWORKS 3
#endif

/**
 * Stan połączenia (rozdz. 7.1).
 *
 * Degraded jest stanem osobnym, a nie odmianą Online: łącze fizyczne działa,
 * ale warstwa wyżej zgłasza kłopoty (utracony broker, sygnał poniżej progu).
 * Aplikacja może wtedy ograniczyć ruch, zamiast czekać na twarde zerwanie.
 */
enum class ConnState : u8 {
    Idle = 0,      ///< przed pierwszą próbą albo po jawnym rozłączeniu
    Connecting,    ///< trwa łączenie z siecią
    Online,        ///< łącze działa
    Degraded,      ///< łącze działa, ale poniżej założonych parametrów
    Reconnecting,  ///< po zerwaniu, w trakcie odczekiwania backoffu
};

constexpr const char* toString(ConnState s) {
    switch (s) {
        case ConnState::Idle:         return "idle";
        case ConnState::Connecting:   return "connecting";
        case ConnState::Online:       return "online";
        case ConnState::Degraded:     return "degraded";
        case ConnState::Reconnecting: return "reconnecting";
    }
    return "unknown";
}

/** Poświadczenia jednej sieci. Klucz jest typu poufnego (rozdz. 7.3). */
struct NetworkCredentials {
    char                     ssid[kSsidMax] = {};
    SecretString<kPskMax>    psk;
    /** Niższa wartość = wyższy priorytet przy wyborze sieci. */
    u8                       priority = 0;

    bool valid() const { return ssid[0] != '\0'; }
};

// ---------------------------------------------------------------------------
// Zdarzenia
// ---------------------------------------------------------------------------

/** Zmiana stanu połączenia — podstawa reakcji pozostałych modułów. */
struct ConnStateChanged {
    ConnState from;
    ConnState to;
    u8        networkIndex;  ///< która sieć z listy
    u32       attempt;       ///< numer próby w bieżącej serii
};

/** Urządzenie uzyskało adres — moment, w którym wolno otwierać gniazda. */
struct NetGotAddress {
    u32 ipv4;      ///< kolejność bajtów hosta
    i8  rssiDbm;
    u8  networkIndex;
};

/** Utrata łącza. */
struct NetLost {
    Err reason;
    u32 uptimeSec;  ///< jak długo łącze działało przed zerwaniem
};

/** Stan połączenia z brokerem MQTT. */
struct MqttStateChanged {
    bool connected;
    Err  reason;
    u16  subscriptions;
};

/** Wiadomość odebrana z brokera i przepuszczona przez mostek na EventBus. */
struct MqttMessage {
    /** nameId(topic) — pełny temat nie zmieściłby się w budżecie zdarzenia. */
    TopicId topic;
    u16     length;
    u8      qos;
    bool    retained;
};

}  // namespace net
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::net::ConnStateChanged, "net/state")
HYDRA_DECLARE_EVENT(hydra::net::NetGotAddress,    "net/address")
HYDRA_DECLARE_EVENT(hydra::net::NetLost,          "net/lost")
HYDRA_DECLARE_EVENT(hydra::net::MqttStateChanged, "net/mqtt")
HYDRA_DECLARE_EVENT(hydra::net::MqttMessage,      "net/message")

#endif  // HYDRA_ENABLE_NET
