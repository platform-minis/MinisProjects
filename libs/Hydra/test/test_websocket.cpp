/**
 * Testy klienta WebSocket.
 *
 * Broker MyCastle słucha na `ws://{host}:1902/mqtt`, więc to jest główna droga
 * do platformy, a nie wariant. Sprawdzamy tu ramkowanie bajt w bajt — błąd
 * w masce albo w polu długości objawia się jako broker, który zrywa połączenie
 * bez słowa wyjaśnienia.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/net/WebSocketClient.hpp"

using namespace hydra;
using namespace hydra::net;

namespace {

/** Gniazdo-atrapa: zapisuje, co klient wysłał, i podaje, co „przysłał serwer". */
class LoopClient : public IClient {
public:
    Status connect(const char*, u16, u32) override { open_ = true; return ok(); }
    void   stop() override { open_ = false; }
    bool   connected() const override { return open_; }

    size_t write(CByteSpan data) override {
        for (size_t i = 0; i < data.size() && sentLen < sizeof(sent); ++i) {
            sent[sentLen++] = data.data()[i];
        }
        return data.size();
    }

    size_t read(ByteSpan out) override {
        const size_t ready = inboxLen - inboxRead;
        const size_t take = ready < out.size() ? ready : out.size();
        memcpy(out.data(), inbox + inboxRead, take);
        inboxRead += take;
        return take;
    }

    size_t available() override { return inboxLen - inboxRead; }

    /** Wstawia bajty tak, jakby przysłał je serwer. */
    void feed(const void* data, size_t n) {
        memcpy(inbox + inboxLen, data, n);
        inboxLen += n;
    }
    void feed(const char* text) { feed(text, strlen(text)); }

    /** Kolejny fragment tego, co klient wysłał, licząc od `sentRead`. */
    const u8* sentFrom(size_t offset) const { return sent + offset; }

    u8     sent[1024] = {};
    size_t sentLen = 0;
    u8     inbox[1024] = {};
    size_t inboxLen = 0;
    size_t inboxRead = 0;

private:
    bool open_ = false;
};

constexpr const char* kAccept =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";

/** Klient po udanym uzgodnieniu; `sentLen` wyzerowane, żeby liczyć od ramek. */
struct Opened {
    LoopClient      tcp;
    WebSocketClient ws{tcp};

    Opened() {
        tcp.feed(kAccept);
        (void)ws.connect("mqtt.local", 1902, 1000);
        tcp.sentLen = 0;
    }
};

/** Ramka od serwera: bez maski, długość 7-bitowa. */
void serverFrame(LoopClient& tcp, u8 opcode, const void* payload, size_t n) {
    u8 head[2] = {static_cast<u8>(0x80 | opcode), static_cast<u8>(n)};
    tcp.feed(head, 2);
    if (n > 0) tcp.feed(payload, n);
}

}  // namespace

TEST("WebSocket: uzgodnienie zawiera nagłówki wymagane przez brokera") {
    LoopClient tcp;
    WebSocketClient ws{tcp};
    WebSocketClient::Config cfg;
    cfg.path = "/mqtt";
    ws.configure(cfg);

    tcp.feed(kAccept);
    REQUIRE(ws.connect("mqtt.local", 1902, 1000).has_value());

    tcp.sent[tcp.sentLen] = 0;
    const char* request = reinterpret_cast<const char*>(tcp.sent);

    CHECK(strstr(request, "GET /mqtt HTTP/1.1\r\n") != nullptr);
    CHECK(strstr(request, "Host: mqtt.local:1902\r\n") != nullptr);
    CHECK(strstr(request, "Upgrade: websocket\r\n") != nullptr);
    CHECK(strstr(request, "Sec-WebSocket-Version: 13\r\n") != nullptr);
    CHECK(strstr(request, "Sec-WebSocket-Key: ") != nullptr);
    // Bez tego nagłówka Aedes przyjmuje połączenie i zamyka je po pierwszej
    // ramce, bo nie wie, czym jest to, co dostał.
    CHECK(strstr(request, "Sec-WebSocket-Protocol: mqtt\r\n") != nullptr);
    CHECK(ws.connected());
}

TEST("WebSocket: odpowiedź inna niż 101 to błąd, nie połączenie") {
    // Kod 200 oznacza serwer, który odesłał stronę zamiast uaktualnić
    // połączenie — najczęstszy objaw pomylonej ścieżki albo portu.
    LoopClient tcp;
    WebSocketClient ws{tcp};
    tcp.feed("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

    CHECK(!ws.connect("mqtt.local", 1902, 1000).has_value());
    CHECK(!ws.connected());
}

TEST("WebSocket: zapis daje zamaskowaną ramkę binarną") {
    // Klient RFC 6455 musi maskować każdą ramkę; brokery zrywają połączenie
    // na niezamaskowanej, nie zgłaszając powodu.
    Opened fx;
    const char* payload = "MQTT-PACKET";
    const size_t n = strlen(payload);

    CHECK_EQ(static_cast<int>(fx.ws.write(
                 CByteSpan{reinterpret_cast<const u8*>(payload), n})),
             static_cast<int>(n));

    const u8* frame = fx.tcp.sent;
    CHECK_EQ(static_cast<int>(frame[0]), 0x82);               // FIN + binary
    CHECK_EQ(static_cast<int>(frame[1] & 0x80), 0x80);        // bit maski
    CHECK_EQ(static_cast<int>(frame[1] & 0x7F), static_cast<int>(n));

    const u8* mask = frame + 2;
    const u8* body = frame + 6;
    for (size_t i = 0; i < n; ++i) {
        CHECK_EQ(static_cast<int>(body[i] ^ mask[i & 3]), static_cast<int>(payload[i]));
    }
}

TEST("WebSocket: ładunek powyżej 125 bajtów używa pola 16-bitowego") {
    Opened fx;
    u8 payload[300];
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<u8>(i);

    fx.ws.write(CByteSpan{payload, sizeof(payload)});

    const u8* frame = fx.tcp.sent;
    CHECK_EQ(static_cast<int>(frame[1] & 0x7F), 126);
    CHECK_EQ(static_cast<int>((frame[2] << 8) | frame[3]), 300);
}

TEST("WebSocket: odczyt składa treść z ramek serwera") {
    Opened fx;
    serverFrame(fx.tcp, 0x2, "CONNACK", 7);

    CHECK_EQ(static_cast<int>(fx.ws.available()), 7);

    u8 out[16] = {};
    CHECK_EQ(static_cast<int>(fx.ws.read(ByteSpan{out, sizeof(out)})), 7);
    CHECK_EQ(memcmp(out, "CONNACK", 7), 0);
    CHECK_EQ(static_cast<int>(fx.ws.available()), 0);
}

TEST("WebSocket: fragmenty sklejają się w jeden strumień") {
    // Dla warstwy wyżej WebSocket jest strumieniem — granice ramek nie niosą
    // informacji, której MQTT by potrzebował, a pakiet potrafi je przekroczyć.
    Opened fx;
    u8 first[2] = {0x02, 0x03};   // binary, bez FIN
    fx.tcp.feed(first, 2);
    fx.tcp.feed("ABC", 3);
    u8 last[2] = {0x80, 0x02};    // continuation z FIN
    fx.tcp.feed(last, 2);
    fx.tcp.feed("DE", 2);

    u8 out[16] = {};
    CHECK_EQ(static_cast<int>(fx.ws.read(ByteSpan{out, sizeof(out)})), 5);
    CHECK_EQ(memcmp(out, "ABCDE", 5), 0);
}

TEST("WebSocket: ping dostaje pong z tą samą treścią") {
    // Bez odpowiedzi broker uzna połączenie za martwe, choć ruch płynie.
    Opened fx;
    serverFrame(fx.tcp, 0x9, "hb", 2);

    CHECK_EQ(static_cast<int>(fx.ws.available()), 0);   // ping nie jest danymi
    REQUIRE(fx.tcp.sentLen >= 8);

    const u8* pong = fx.tcp.sent;
    CHECK_EQ(static_cast<int>(pong[0]), 0x8A);          // FIN + pong
    const u8* mask = pong + 2;
    CHECK_EQ(static_cast<int>(pong[6] ^ mask[0]), 'h');
    CHECK_EQ(static_cast<int>(pong[7] ^ mask[1]), 'b');
    CHECK_EQ(static_cast<int>(fx.ws.stats().pings), 1);
}

TEST("WebSocket: ramka zamknięcia kończy połączenie") {
    Opened fx;
    serverFrame(fx.tcp, 0x8, nullptr, 0);
    (void)fx.ws.available();
    CHECK(!fx.ws.connected());
}

TEST("WebSocket: ramka większa niż bufor jest zgłaszana, a nie milcząco gubiona") {
    // Cichy pominięty pakiet MQTT objawiłby się jako subskrypcja, która
    // „czasem nie działa" — najgorszy możliwy rodzaj usterki.
    Opened fx;
    u8 head[4] = {0x82, 126,
                  static_cast<u8>((HYDRA_WS_BUFFER + 100) >> 8),
                  static_cast<u8>(HYDRA_WS_BUFFER + 100)};
    fx.tcp.feed(head, 4);
    u8 filler[HYDRA_WS_BUFFER + 100];
    memset(filler, 'x', sizeof(filler));
    fx.tcp.feed(filler, sizeof(filler));

    CHECK_EQ(static_cast<int>(fx.ws.available()), 0);
    CHECK_EQ(static_cast<int>(fx.ws.stats().overflows), 1);
}
