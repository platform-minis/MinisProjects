#pragma once
/**
 * Hydra — klient MQTT 3.1.1 po TCP (rozdz. 7.2).
 *
 * Główny kanał telemetrii i sterowania. Zakres świadomie ograniczony do tego,
 * co urządzenie wbudowane naprawdę wykorzystuje:
 *   - QoS 0 i 1 (QoS 2 wymaga czterofazowego uzgadniania i buforowania,
 *     a w telemetrii nie daje nic, czego nie daje QoS 1 z idempotentnym
 *     tematem),
 *   - Last Will and Testament — broker ogłasza zgon urządzenia, gdy przestaje
 *     odpowiadać; bez tego panel pokazywałby ostatnią znaną wartość w nieskończoność,
 *   - automatyczna resubskrypcja po odtworzeniu połączenia,
 *   - podtrzymanie sesji (PINGREQ) z wykryciem martwego łącza.
 *
 * Klient nie tworzy własnego taska ani nie blokuje: loop() woła się cyklicznie
 * z net.worker. Cała pamięć jest statyczna (rozdz. 11).
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Secret.hpp"
#include "hydra/net/ITransport.hpp"
#include "hydra/net/NetTypes.hpp"

namespace hydra {
namespace net {

/** Rozmiar bufora nadawczo-odbiorczego. Ogranicza maksymalny pakiet. */
#ifndef HYDRA_MQTT_BUFFER
#  define HYDRA_MQTT_BUFFER 256
#endif
/** Liczba jednoczesnych subskrypcji. */
#ifndef HYDRA_MQTT_MAX_SUBS
#  define HYDRA_MQTT_MAX_SUBS 8
#endif
/** Ile publikacji QoS 1 może naraz czekać na potwierdzenie. */
#ifndef HYDRA_MQTT_INFLIGHT
#  define HYDRA_MQTT_INFLIGHT 2
#endif
/** Maksymalny ładunek publikacji QoS 1 przechowywany do retransmisji. */
#ifndef HYDRA_MQTT_INFLIGHT_PAYLOAD
#  define HYDRA_MQTT_INFLIGHT_PAYLOAD 96
#endif

/** Kod odpowiedzi brokera na CONNECT (MQTT 3.1.1, tabela 3.1). */
enum class MqttConnectCode : u8 {
    Accepted           = 0,
    BadProtocolVersion = 1,
    ClientIdRejected   = 2,
    ServerUnavailable  = 3,
    BadCredentials     = 4,
    NotAuthorized      = 5,
};

class MqttClient {
public:
    struct Config {
        const char* clientId     = "hydra";
        const char* host         = nullptr;
        u16         port         = 1883;
        const char* username     = nullptr;
        SecretString<64> password;
        u16         keepAliveSec = 30;
        bool        cleanSession = true;

        /** Last Will: broker ogłosi to, gdy urządzenie zamilknie. */
        const char* willTopic    = nullptr;
        const char* willPayload  = nullptr;
        u8          willQos      = 0;
        bool        willRetain   = true;

        u32 connectTimeoutMs = 5000;
        /** Po tym czasie bez PUBACK publikacja QoS 1 jest powtarzana. */
        u32 ackTimeoutMs     = 3000;
        u8  maxRetransmits   = 2;
    };

    struct Stats {
        u32 published     = 0;
        u32 received      = 0;
        u32 retransmits   = 0;
        u32 dropped       = 0;  ///< pakiety większe niż bufor
        u32 connects      = 0;
        u32 disconnects   = 0;
    };

    /** Handler wiadomości: temat jako napis, ładunek jako surowe bajty. */
    using MessageHandler = Delegate<void(const char*, CByteSpan)>;

    explicit MqttClient(IClient& client) : client_(client) {}

    Status configure(const Config& cfg);

    /** Otwiera połączenie i wysyła CONNECT. Nie czeka na CONNACK. */
    Status connect(Millis now);
    void   disconnect();
    bool   connected() const { return state_ == State::Connected; }

    /** Jeden krok: odbiór pakietów, podtrzymanie sesji, retransmisje. */
    void loop(Millis now);

    Status publish(const char* topic, CByteSpan payload, u8 qos = 0, bool retain = false);
    Status publish(const char* topic, const char* payload, u8 qos = 0, bool retain = false);

    /**
     * Subskrypcja tematu. Filtr może zawierać znaki wieloznaczne + oraz #.
     * Subskrypcje przeżywają zerwanie połączenia i są odtwarzane automatycznie.
     */
    Status subscribe(const char* filter, u8 qos, MessageHandler handler);
    Status unsubscribe(const char* filter);

    u16   subscriptionCount() const;
    Stats stats() const { return stats_; }
    MqttConnectCode lastConnectCode() const { return connectCode_; }

    /** Dopasowanie tematu do filtra z + i #. Wystawione publicznie do testów. */
    static bool topicMatches(const char* filter, const char* topic);

private:
    enum class State : u8 { Disconnected, Connecting, Connected };

    struct Subscription {
        char           filter[kTopicMax] = {};
        MessageHandler handler{};
        u8             qos    = 0;
        bool           active = false;
        bool           acked  = false;
    };

    struct InFlight {
        u16    packetId = 0;
        char   topic[kTopicMax] = {};
        u8     payload[HYDRA_MQTT_INFLIGHT_PAYLOAD] = {};
        u16    length   = 0;
        Millis sentAt   = 0;
        u8     retries  = 0;
        bool   retain   = false;
        bool   busy     = false;
    };

    Status sendConnect();
    Status sendSubscribe(Subscription& sub);
    Status sendPublish(const char* topic, CByteSpan payload, u8 qos, bool retain,
                       u16 packetId, bool dup);
    Status sendPingReq();
    Status writeAll(CByteSpan data);

    void   handlePacket(u8 header, u32 remaining, Millis now);
    void   handlePublish(u8 header, u32 remaining, Millis now);
    void   handleConnAck(u32 remaining, Millis now);
    void   handlePubAck(u32 remaining);
    void   handleSubAck(u32 remaining);
    void   dispatch(const char* topic, CByteSpan payload);
    void   dropConnection(Err reason, Millis now);
    void   resubscribeAll();
    void   retransmit(Millis now);

    InFlight* takeInFlightSlot();
    u16       nextPacketId();

    IClient& client_;
    Config   cfg_{};
    State    state_ = State::Disconnected;

    Subscription subs_[HYDRA_MQTT_MAX_SUBS];
    InFlight     inflight_[HYDRA_MQTT_INFLIGHT];

    u8  buf_[HYDRA_MQTT_BUFFER] = {};
    u16 packetId_ = 0;

    Millis lastActivityMs_ = 0;
    Millis lastPingMs_     = 0;
    Millis connectStartMs_ = 0;
    bool   pingPending_    = false;

    MqttConnectCode connectCode_ = MqttConnectCode::Accepted;
    Stats           stats_{};
};

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
