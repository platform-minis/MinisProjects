/**
 * Testy klienta MQTT i mostka telemetrii (rozdz. 7.2).
 *
 * Atrapa gniazda gra rolę brokera: test wstrzykuje ramki, które „przyszły
 * z serwera", i sprawdza bajty, które klient wysłał. Dzięki temu format ramek
 * jest weryfikowany na poziomie bajtów, a nie tylko przez zachowanie.
 */

#include "hydra_test.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/net/Mock.hpp"
#include "hydra/net/MqttClient.hpp"
#include "hydra/net/NetModule.hpp"
#include "hydra/net/TelemetryBridge.hpp"

using namespace hydra;
using namespace hydra::net;

namespace {

void resetMqtt() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

// --- ramki, które „wysyła broker" ---

void injectConnAck(mock::MockClient& c, u8 code = 0) {
    const u8 frame[] = {0x20, 0x02, 0x00, code};
    c.injectRx(CByteSpan{frame, sizeof(frame)});
}

void injectPingResp(mock::MockClient& c) {
    const u8 frame[] = {0xD0, 0x00};
    c.injectRx(CByteSpan{frame, sizeof(frame)});
}

void injectPubAck(mock::MockClient& c, u16 packetId) {
    const u8 frame[] = {0x40, 0x02, static_cast<u8>(packetId >> 8),
                        static_cast<u8>(packetId & 0xFF)};
    c.injectRx(CByteSpan{frame, sizeof(frame)});
}

void injectSubAck(mock::MockClient& c, u16 packetId, u8 grantedQos = 0) {
    const u8 frame[] = {0x90, 0x03, static_cast<u8>(packetId >> 8),
                        static_cast<u8>(packetId & 0xFF), grantedQos};
    c.injectRx(CByteSpan{frame, sizeof(frame)});
}

/** Buduje PUBLISH od brokera do klienta. */
void injectPublish(mock::MockClient& c, const char* topic, const char* payload,
                   u8 qos = 0, u16 packetId = 0) {
    u8           frame[256];
    const size_t topicLen   = strlen(topic);
    const size_t payloadLen = payload ? strlen(payload) : 0;
    const size_t remaining  = 2 + topicLen + (qos > 0 ? 2 : 0) + payloadLen;

    size_t pos   = 0;
    frame[pos++] = static_cast<u8>(0x30 | (qos << 1));
    frame[pos++] = static_cast<u8>(remaining);
    frame[pos++] = static_cast<u8>(topicLen >> 8);
    frame[pos++] = static_cast<u8>(topicLen & 0xFF);
    memcpy(frame + pos, topic, topicLen);
    pos += topicLen;
    if (qos > 0) {
        frame[pos++] = static_cast<u8>(packetId >> 8);
        frame[pos++] = static_cast<u8>(packetId & 0xFF);
    }
    if (payloadLen) {
        memcpy(frame + pos, payload, payloadLen);
        pos += payloadLen;
    }
    c.injectRx(CByteSpan{frame, pos});
}

/** Szuka ciągu bajtów w tym, co klient wysłał. */
bool sentContains(const mock::MockClient& c, const char* needle) {
    const CByteSpan tx  = c.sent();
    const size_t    len = strlen(needle);
    if (len > tx.size()) return false;
    for (size_t i = 0; i + len <= tx.size(); ++i) {
        if (memcmp(tx.data() + i, needle, len) == 0) return true;
    }
    return false;
}

MqttClient::Config brokerConfig() {
    MqttClient::Config cfg;
    cfg.clientId     = "rover-01";
    cfg.host         = "broker.local";
    cfg.port         = 1883;
    cfg.keepAliveSec = 10;
    return cfg;
}

/** Doprowadza klienta do stanu połączonego. */
void bringUp(MqttClient& mqtt, mock::MockClient& sock, Millis now = 1000) {
    REQUIRE(mqtt.connect(now).has_value());
    injectConnAck(sock);
    mqtt.loop(now + 10);
    REQUIRE(mqtt.connected());
    sock.clearSent();
}

}  // namespace

// ---------------------------------------------------------------------------
// Dopasowanie tematów
// ---------------------------------------------------------------------------

TEST("MQTT: dopasowanie tematów ze znakami wieloznacznymi") {
    CHECK(MqttClient::topicMatches("a/b/c", "a/b/c"));
    CHECK(!MqttClient::topicMatches("a/b/c", "a/b/d"));

    // + pochłania dokładnie jeden poziom
    CHECK(MqttClient::topicMatches("a/+/c", "a/b/c"));
    CHECK(!MqttClient::topicMatches("a/+/c", "a/b/x/c"));
    CHECK(MqttClient::topicMatches("+/b", "a/b"));

    // # pochłania resztę tematu
    CHECK(MqttClient::topicMatches("a/#", "a/b/c/d"));
    CHECK(MqttClient::topicMatches("#", "cokolwiek/tu/jest"));
    CHECK(!MqttClient::topicMatches("a/#", "b/c"));

    // "a/b/#" pasuje także do samego "a/b" (specyfikacja, rozdz. 4.7.1.2)
    CHECK(MqttClient::topicMatches("a/b/#", "a/b"));

    CHECK(!MqttClient::topicMatches(nullptr, "a"));
}

// ---------------------------------------------------------------------------
// Nawiązanie sesji
// ---------------------------------------------------------------------------

TEST("MQTT: CONNECT ma poprawny nagłówek i identyfikator klienta") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);

    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    REQUIRE(mqtt.connect(1000).has_value());

    CHECK_STR(sock.lastHost(), "broker.local");
    CHECK_EQ(static_cast<int>(sock.lastPort()), 1883);

    const CByteSpan tx = sock.sent();
    REQUIRE(tx.size() > 12);
    CHECK_EQ(static_cast<int>(tx[0]), 0x10);   // typ pakietu CONNECT
    CHECK_EQ(static_cast<int>(tx[2]), 0x00);   // długość nazwy protokołu, MSB
    CHECK_EQ(static_cast<int>(tx[3]), 0x04);
    CHECK(memcmp(tx.data() + 4, "MQTT", 4) == 0);
    CHECK_EQ(static_cast<int>(tx[8]), 0x04);   // poziom protokołu 3.1.1
    CHECK_EQ(static_cast<int>(tx[9]), 0x02);   // clean session
    CHECK_EQ(static_cast<int>(tx[10]), 0x00);  // keepalive MSB
    CHECK_EQ(static_cast<int>(tx[11]), 10);    // keepalive LSB
    CHECK(sentContains(sock, "rover-01"));

    // Przed CONNACK klient nie uważa się za połączonego.
    CHECK(!mqtt.connected());
}

TEST("MQTT: CONNACK z kodem zero kończy nawiązywanie sesji") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());

    int  changes = 0;
    bool state   = false;
    auto sub = EventBus::subscribe<MqttStateChanged>([&](const MqttStateChanged& e) {
        ++changes;
        state = e.connected;
    });
    REQUIRE(sub.has_value());

    REQUIRE(mqtt.connect(1000).has_value());
    injectConnAck(sock);
    mqtt.loop(1010);

    CHECK(mqtt.connected());
    CHECK_EQ(changes, 1);
    CHECK(state);
    CHECK_EQ(static_cast<int>(mqtt.stats().connects), 1);
}

TEST("MQTT: odrzucone poświadczenia odróżniają się od zajętego brokera") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());

    Err reason = Err::None;
    auto sub = EventBus::subscribe<MqttStateChanged>(
        [&](const MqttStateChanged& e) { reason = e.reason; });
    REQUIRE(sub.has_value());

    REQUIRE(mqtt.connect(1000).has_value());
    injectConnAck(sock, 5);  // NotAuthorized
    mqtt.loop(1010);

    CHECK(!mqtt.connected());
    CHECK(mqtt.lastConnectCode() == MqttConnectCode::NotAuthorized);
    // Odrzucone poświadczenia to nie powód do bezmyślnego ponawiania.
    CHECK(reason == Err::Protocol);

    // Broker niedostępny to co innego — warto próbować dalej.
    mock::MockClient sock2;
    MqttClient       mqtt2(sock2);
    REQUIRE(mqtt2.configure(brokerConfig()).has_value());
    REQUIRE(mqtt2.connect(2000).has_value());
    injectConnAck(sock2, 3);  // ServerUnavailable
    mqtt2.loop(2010);
    CHECK(reason == Err::Busy);
}

TEST("MQTT: brak odpowiedzi na CONNECT kończy się timeoutem") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);

    MqttClient::Config cfg = brokerConfig();
    cfg.connectTimeoutMs   = 500;
    REQUIRE(mqtt.configure(cfg).has_value());

    REQUIRE(mqtt.connect(1000).has_value());
    mqtt.loop(1400);
    CHECK(!mqtt.connected());

    mqtt.loop(1501);
    CHECK(!mqtt.connected());
    CHECK(!sock.connected());
}

TEST("MQTT: Last Will trafia do pakietu CONNECT") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);

    MqttClient::Config cfg = brokerConfig();
    cfg.willTopic   = "hydra/rover-01/status";
    cfg.willPayload = "offline";
    cfg.willRetain  = true;
    REQUIRE(mqtt.configure(cfg).has_value());
    REQUIRE(mqtt.connect(1000).has_value());

    const CByteSpan tx = sock.sent();
    REQUIRE(tx.size() > 10);
    // Flagi: clean session (0x02) + will (0x04) + will retain (0x20)
    CHECK_EQ(static_cast<int>(tx[9]), 0x26);
    CHECK(sentContains(sock, "hydra/rover-01/status"));
    CHECK(sentContains(sock, "offline"));
}

TEST("MQTT: hasło jest wysyłane, ale nie wycieka do logów") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);

    MqttClient::Config cfg = brokerConfig();
    cfg.username = "rover";
    cfg.password.set("tajne");
    REQUIRE(mqtt.configure(cfg).has_value());
    REQUIRE(mqtt.connect(1000).has_value());

    // Flagi: clean session + username (0x80) + password (0x40)
    const CByteSpan tx = sock.sent();
    CHECK_EQ(static_cast<int>(tx[9]), 0xC2);
    CHECK(sentContains(sock, "rover"));
    CHECK(sentContains(sock, "tajne"));

    // Ale reprezentacja tekstowa pozostaje maską.
    CHECK_STR(cfg.password.masked(), "********");
}

// ---------------------------------------------------------------------------
// Publikacja
// ---------------------------------------------------------------------------

TEST("MQTT: publikacja QoS 0 buduje poprawną ramkę") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    REQUIRE(mqtt.publish("hydra/temp", "21.5").has_value());

    const CByteSpan tx = sock.sent();
    REQUIRE(tx.size() >= 6);
    CHECK_EQ(static_cast<int>(tx[0]), 0x30);  // PUBLISH, QoS 0, bez retain
    CHECK_EQ(static_cast<int>(tx[1]), 2 + 10 + 4);
    CHECK_EQ(static_cast<int>(tx[3]), 10);    // długość tematu
    CHECK(sentContains(sock, "hydra/temp"));
    CHECK(sentContains(sock, "21.5"));
    CHECK_EQ(static_cast<int>(mqtt.stats().published), 1);
}

TEST("MQTT: flaga retain ustawia najmłodszy bit nagłówka") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    REQUIRE(mqtt.publish("a/b", "x", 0, true).has_value());
    CHECK_EQ(static_cast<int>(sock.sent()[0]), 0x31);
}

TEST("MQTT: publikacja bez połączenia jest odrzucana") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());

    CHECK(mqtt.publish("a/b", "x").error() == Err::NotInitialized);

    bringUp(mqtt, sock);
    CHECK(mqtt.publish("", "x").error() == Err::BadArgument);
    CHECK(mqtt.publish("a/b", "x", 2).error() == Err::NotSupported);
}

TEST("MQTT: QoS 1 czeka na PUBACK i nie retransmituje po potwierdzeniu") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);

    MqttClient::Config cfg = brokerConfig();
    cfg.ackTimeoutMs = 500;
    REQUIRE(mqtt.configure(cfg).has_value());
    bringUp(mqtt, sock);

    REQUIRE(mqtt.publish("a/b", "1", 1).has_value());
    const CByteSpan tx = sock.sent();
    CHECK_EQ(static_cast<int>(tx[0]), 0x32);  // PUBLISH z QoS 1

    // Identyfikator pakietu jest tuż za tematem.
    const u16 packetId = static_cast<u16>(tx[2 + 2 + 3] << 8 | tx[2 + 2 + 3 + 1]);
    CHECK(packetId != 0);

    injectPubAck(sock, packetId);
    mqtt.loop(2000);

    sock.clearSent();
    mqtt.loop(5000);  // dawno po ackTimeout
    CHECK_EQ(static_cast<int>(sock.sent().size()), 0);
    CHECK_EQ(static_cast<int>(mqtt.stats().retransmits), 0);
}

TEST("MQTT: brak PUBACK wyzwala retransmisję z flagą DUP") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);

    MqttClient::Config cfg = brokerConfig();
    cfg.ackTimeoutMs   = 500;
    cfg.maxRetransmits = 1;
    REQUIRE(mqtt.configure(cfg).has_value());
    bringUp(mqtt, sock);

    REQUIRE(mqtt.publish("a/b", "1", 1).has_value());
    sock.clearSent();

    mqtt.loop(rtos::nowMs() + 600);
    REQUIRE(sock.sent().size() > 0);
    // DUP (0x08) informuje brokera, że to powtórzenie, a nie nowa wiadomość.
    CHECK_EQ(static_cast<int>(sock.sent()[0]), 0x3A);
    CHECK_EQ(static_cast<int>(mqtt.stats().retransmits), 1);

    // Po wyczerpaniu prób publikacja jest porzucana, a nie powtarzana w nieskończoność.
    sock.clearSent();
    mqtt.loop(rtos::nowMs() + 1300);
    CHECK_EQ(static_cast<int>(sock.sent().size()), 0);
    CHECK(mqtt.stats().dropped >= 1);
}

TEST("MQTT: wyczerpanie slotów QoS 1 zgłasza zajętość") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    for (int i = 0; i < HYDRA_MQTT_INFLIGHT; ++i) {
        REQUIRE(mqtt.publish("a/b", "x", 1).has_value());
    }
    // Broker nie nadąża z potwierdzeniami — odrzucenie jest uczciwsze
    // niż kolejkowanie bez końca.
    CHECK(mqtt.publish("a/b", "x", 1).error() == Err::Busy);

    // QoS 0 nadal przechodzi, bo nie zajmuje slotu.
    CHECK(mqtt.publish("a/b", "x", 0).has_value());
}

TEST("MQTT: ładunek większy niż bufor jest odrzucany, a nie obcinany") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    u8 big[HYDRA_MQTT_BUFFER + 32] = {};
    CHECK(mqtt.publish("a/b", CByteSpan{big, sizeof(big)}).error() == Err::OutOfRange);
    CHECK(mqtt.stats().dropped >= 1);
}

// ---------------------------------------------------------------------------
// Subskrypcje
// ---------------------------------------------------------------------------

TEST("MQTT: subskrypcja i dostarczenie wiadomości do handlera") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    char received[64] = {};
    char gotTopic[64] = {};
    int  hits         = 0;

    REQUIRE(mqtt.subscribe("hydra/+/cmd", 0,
                           [&](const char* topic, CByteSpan payload) {
                               ++hits;
                               strncpy(gotTopic, topic, sizeof(gotTopic) - 1);
                               const size_t n = payload.size() < sizeof(received) - 1
                                                    ? payload.size()
                                                    : sizeof(received) - 1;
                               memcpy(received, payload.data(), n);
                               received[n] = '\0';
                           })
                .has_value());

    const CByteSpan tx = sock.sent();
    CHECK_EQ(static_cast<int>(tx[0]), 0x82);  // SUBSCRIBE z wymaganymi bitami
    CHECK(sentContains(sock, "hydra/+/cmd"));

    injectSubAck(sock, 1);
    injectPublish(sock, "hydra/rover/cmd", "start");
    mqtt.loop(2000);

    CHECK_EQ(hits, 1);
    CHECK_STR(gotTopic, "hydra/rover/cmd");
    CHECK_STR(received, "start");
    CHECK_EQ(static_cast<int>(mqtt.stats().received), 1);

    // Temat spoza filtra nie trafia do handlera.
    injectPublish(sock, "inny/temat", "x");
    mqtt.loop(2100);
    CHECK_EQ(hits, 1);
}

TEST("MQTT: wiadomość QoS 1 od brokera jest potwierdzana") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    REQUIRE(mqtt.subscribe("a/b", 1, [](const char*, CByteSpan) {}).has_value());
    sock.clearSent();

    injectPublish(sock, "a/b", "x", 1, 0x1234);
    mqtt.loop(2000);

    const CByteSpan tx = sock.sent();
    REQUIRE(tx.size() >= 4);
    CHECK_EQ(static_cast<int>(tx[0]), 0x40);  // PUBACK
    CHECK_EQ(static_cast<int>(tx[1]), 0x02);
    CHECK_EQ(static_cast<int>(tx[2]), 0x12);
    CHECK_EQ(static_cast<int>(tx[3]), 0x34);
}

TEST("MQTT: subskrypcje przeżywają zerwanie i są odtwarzane") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    REQUIRE(mqtt.subscribe("hydra/cmd", 0, [](const char*, CByteSpan) {}).has_value());
    CHECK_EQ(static_cast<int>(mqtt.subscriptionCount()), 1);

    // Zdalny koniec zrywa połączenie.
    sock.forceDisconnect();
    mqtt.loop(3000);
    CHECK(!mqtt.connected());
    // Subskrypcja nie znika — to ona sprawia, że po powrocie urządzenie
    // znów słucha komend bez udziału aplikacji.
    CHECK_EQ(static_cast<int>(mqtt.subscriptionCount()), 1);

    sock.clear();
    sock.clearSent();
    REQUIRE(mqtt.connect(4000).has_value());
    injectConnAck(sock);
    sock.clearSent();
    mqtt.loop(4010);

    CHECK(mqtt.connected());
    CHECK(sentContains(sock, "hydra/cmd"));  // SUBSCRIBE wysłany ponownie
}

TEST("MQTT: subskrypcja przed połączeniem czeka na CONNACK") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());

    REQUIRE(mqtt.subscribe("a/b", 0, [](const char*, CByteSpan) {}).has_value());
    CHECK_EQ(static_cast<int>(mqtt.subscriptionCount()), 1);

    REQUIRE(mqtt.connect(1000).has_value());
    sock.clearSent();
    injectConnAck(sock);
    mqtt.loop(1010);

    CHECK(sentContains(sock, "a/b"));
}

TEST("MQTT: limit subskrypcji i wypisanie się") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    for (int i = 0; i < HYDRA_MQTT_MAX_SUBS; ++i) {
        char filter[16];
        snprintf(filter, sizeof(filter), "t/%d", i);
        REQUIRE(mqtt.subscribe(filter, 0, [](const char*, CByteSpan) {}).has_value());
    }
    CHECK(mqtt.subscribe("nadmiar", 0, [](const char*, CByteSpan) {}).error() ==
          Err::OutOfMemory);

    REQUIRE(mqtt.unsubscribe("t/0").has_value());
    CHECK_EQ(static_cast<int>(mqtt.subscriptionCount()), HYDRA_MQTT_MAX_SUBS - 1);
    CHECK(mqtt.unsubscribe("t/0").error() == Err::NotFound);
    CHECK(mqtt.subscribe("znowu-jest-miejsce", 0, [](const char*, CByteSpan) {}).has_value());
}

// ---------------------------------------------------------------------------
// Podtrzymanie sesji
// ---------------------------------------------------------------------------

TEST("MQTT: brak ruchu wyzwala PINGREQ") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);

    MqttClient::Config cfg = brokerConfig();
    cfg.keepAliveSec = 10;
    REQUIRE(mqtt.configure(cfg).has_value());
    bringUp(mqtt, sock, 1000);

    // Poniżej połowy okresu keepalive — jeszcze nic nie wysyłamy.
    mqtt.loop(3000);
    CHECK_EQ(static_cast<int>(sock.sent().size()), 0);

    mqtt.loop(6100);  // ponad 5 s od ostatniego ruchu
    REQUIRE(sock.sent().size() == 2);
    CHECK_EQ(static_cast<int>(sock.sent()[0]), 0xC0);  // PINGREQ

    injectPingResp(sock);
    mqtt.loop(6200);
    CHECK(mqtt.connected());
}

TEST("MQTT: brak PINGRESP przez pełny okres zrywa martwe połączenie") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);

    MqttClient::Config cfg = brokerConfig();
    cfg.keepAliveSec = 10;
    REQUIRE(mqtt.configure(cfg).has_value());
    bringUp(mqtt, sock, 1000);

    mqtt.loop(6100);  // wysyła PINGREQ
    CHECK(mqtt.connected());

    // Łącze „działa" na poziomie TCP, ale nic nie przepuszcza — sam TCP
    // wykryłby to dopiero po wielu minutach.
    mqtt.loop(16200);
    CHECK(!mqtt.connected());
}

// ---------------------------------------------------------------------------
// Mostek telemetrii
// ---------------------------------------------------------------------------

namespace {

struct Reading {
    u32 value;
};

struct Command {
    u32 speed;
};

int formatReading(const Reading& r, char* out, size_t cap) {
    return snprintf(out, cap, "{\"v\":%lu}", static_cast<unsigned long>(r.value));
}

bool parseCommand(CByteSpan payload, Command& out) {
    char buf[16] = {};
    if (payload.size() >= sizeof(buf)) return false;
    memcpy(buf, payload.data(), payload.size());
    long v = atol(buf);
    if (v < 0 || v > 1000) return false;
    out.speed = static_cast<u32>(v);
    return true;
}

}  // namespace

HYDRA_DECLARE_EVENT(Reading, "test/reading")
HYDRA_DECLARE_EVENT(Command, "test/command")

TEST("Mostek: zdarzenie z magistrali trafia na brokera") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    TelemetryBridge bridge(mqtt);
    REQUIRE(bridge.publishOn<Reading>("hydra/rover/temp", 0, false, formatReading)
                .has_value());

    // Od tej chwili nie ma już kodu łączącego jedno z drugim.
    EventBus::publish(Reading{42});

    CHECK(sentContains(sock, "hydra/rover/temp"));
    CHECK(sentContains(sock, "{\"v\":42}"));
    CHECK_EQ(static_cast<int>(bridge.stats().publishedOk), 1);
}

TEST("Mostek: wiadomość od brokera staje się zdarzeniem") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    TelemetryBridge bridge(mqtt);
    REQUIRE(bridge.subscribeTo<Command>("hydra/rover/cmd", 0, parseCommand).has_value());

    u32  got  = 0;
    int  hits = 0;
    auto sub  = EventBus::subscribe<Command>([&](const Command& c) {
        ++hits;
        got = c.speed;
    });
    REQUIRE(sub.has_value());

    injectPublish(sock, "hydra/rover/cmd", "250");
    mqtt.loop(2000);

    CHECK_EQ(hits, 1);
    CHECK_EQ(static_cast<int>(got), 250);
    CHECK_EQ(static_cast<int>(bridge.stats().ingested), 1);
}

TEST("Mostek: ładunek nie do przyjęcia nie tworzy zdarzenia") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    TelemetryBridge bridge(mqtt);
    REQUIRE(bridge.subscribeTo<Command>("hydra/rover/cmd", 0, parseCommand).has_value());

    int  hits = 0;
    auto sub  = EventBus::subscribe<Command>([&](const Command&) { ++hits; });
    REQUIRE(sub.has_value());

    injectPublish(sock, "hydra/rover/cmd", "99999");  // poza dopuszczalnym zakresem
    mqtt.loop(2000);

    CHECK_EQ(hits, 0);
    CHECK_EQ(static_cast<int>(bridge.stats().rejected), 1);
    CHECK_EQ(static_cast<int>(bridge.stats().ingested), 0);
}

TEST("Mostek: obcięty ładunek jest porzucany, a nie wysyłany uszkodzony") {
    resetMqtt();
    mock::MockClient sock;
    MqttClient       mqtt(sock);
    REQUIRE(mqtt.configure(brokerConfig()).has_value());
    bringUp(mqtt, sock);

    TelemetryBridge bridge(mqtt);
    REQUIRE(bridge
                .publishOn<Reading>("a/b", 0, false,
                                    [](const Reading&, char* out, size_t cap) {
                                        // Formater celowo przekracza bufor.
                                        return snprintf(out, cap, "%*s", 1000, "x");
                                    })
                .has_value());

    sock.clearSent();
    EventBus::publish(Reading{1});

    CHECK_EQ(static_cast<int>(sock.sent().size()), 0);
    CHECK_EQ(static_cast<int>(bridge.stats().publishFailed), 1);
}

// ---------------------------------------------------------------------------
// Moduł sieciowy
// ---------------------------------------------------------------------------

TEST("NetModule: po uzyskaniu łącza sam łączy się z brokerem") {
    resetMqtt();
    mock::MockNetwork net;
    mock::MockMdns    mdns;
    NetModule         mod(net, &mdns);

    NetModule::Config cfg;
    cfg.connection      = ConnectionManager::Config{};
    cfg.connection.connectTimeoutMs = 1000;
    cfg.connection.backoffBaseMs    = 100;
    cfg.mqtt            = brokerConfig();
    cfg.mdnsHostname    = "rover-01";
    REQUIRE(mod.configure(cfg).has_value());

    NetworkCredentials creds;
    strncpy(creds.ssid, "dom", kSsidMax - 1);
    creds.psk.set("haslo");
    REQUIRE(mod.connection().addNetwork(creds).has_value());
    REQUIRE(mod.init().has_value());
    // onStart() tworzy task net.worker; w teście sterujemy czasem sami,
    // więc uruchamiamy samą maszynę stanów i wołamy step() ręcznie.
    REQUIRE(mod.connection().start(0).has_value());

    mod.step(100);
    CHECK(mod.connection().state() == ConnState::Connecting);

    net.setLinkUp(true);
    mod.step(200);
    CHECK(mod.connection().state() == ConnState::Online);
    // mDNS ogłasza urządzenie dopiero wtedy, gdy jest pod jakim adresem.
    CHECK(mdns.active());
    CHECK_STR(mdns.hostname(), "rover-01");

    // Ten sam krok inicjuje połączenie z brokerem.
    CHECK(net.client.connectCalls() >= 1);
    injectConnAck(net.client);
    mod.step(300);
    CHECK(mod.mqtt().connected());
}

TEST("NetModule: utrata brokera nie zrywa Wi-Fi") {
    resetMqtt();
    mock::MockNetwork net;
    NetModule         mod(net);

    NetModule::Config cfg;
    cfg.mqtt         = brokerConfig();
    cfg.mqttRetryMs  = 1000;
    REQUIRE(mod.configure(cfg).has_value());

    NetworkCredentials creds;
    strncpy(creds.ssid, "dom", kSsidMax - 1);
    REQUIRE(mod.connection().addNetwork(creds).has_value());
    REQUIRE(mod.init().has_value());
    REQUIRE(mod.connection().start(0).has_value());

    mod.step(100);
    net.setLinkUp(true);
    mod.step(200);
    injectConnAck(net.client);
    mod.step(300);
    REQUIRE(mod.mqtt().connected());

    const u32 wifiConnects = net.connectCalls();

    net.client.forceDisconnect();
    mod.step(400);
    CHECK(!mod.mqtt().connected());
    // Łącze zostaje, stan schodzi do Degraded.
    mod.step(500);
    CHECK(mod.connection().state() == ConnState::Degraded);
    CHECK_EQ(static_cast<int>(net.connectCalls()), static_cast<int>(wifiConnects));

    // Po odczekaniu klient sam próbuje ponownie.
    net.client.clear();
    mod.step(1500);
    CHECK(net.client.connectCalls() >= 1);
}

TEST("NetModule: bez łącza nie próbuje pisać do brokera") {
    resetMqtt();
    mock::MockNetwork net;
    NetModule         mod(net);

    NetModule::Config cfg;
    cfg.mqtt = brokerConfig();
    REQUIRE(mod.configure(cfg).has_value());

    NetworkCredentials creds;
    strncpy(creds.ssid, "dom", kSsidMax - 1);
    REQUIRE(mod.connection().addNetwork(creds).has_value());
    REQUIRE(mod.init().has_value());
    REQUIRE(mod.connection().start(0).has_value());

    for (Millis t = 0; t < 500; t += 50) mod.step(t);
    CHECK_EQ(static_cast<int>(net.client.connectCalls()), 0);
}
