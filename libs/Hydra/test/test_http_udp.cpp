/**
 * Testy klienta HTTP i UDP.
 *
 * HTTP sprawdzamy przez atrapę gniazda: wstrzykujemy odpowiedź bajt w bajt
 * taką, jaką przysłałby serwer, i oglądamy żądanie, które poszło w drugą
 * stronę. Dzięki temu badamy protokół, a nie sieć — i można sprawdzić rzeczy,
 * których od prawdziwego serwera nie da się wymusić na żądanie: obcięty
 * nagłówek, kodowanie kawałkami z nagłówkami końcowymi, pętlę przekierowań.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include <string.h>

#include "hydra/net/HttpClient.hpp"
#include "hydra/net/Mock.hpp"
#include "hydra/net/UdpClient.hpp"
#include "hydra_test.hpp"

using namespace hydra;
using namespace hydra::net;

namespace {

/** Zbiera treść odpowiedzi, żeby dało się ją porównać w całości. */
struct BodySink {
    char   text[2048] = {};
    size_t length     = 0;

    /**
     * Ujście przekazywane do klienta.
     *
     * Zwraca domknięcie przechwytujące **wskaźnik**, a nie kopię struktury:
     * `Delegate` trzyma wywoływalny obiekt u siebie i mieści 32 bajty,
     * a ta struktura ma dwa kilobajty bufora.
     */
    HttpClient::BodyFn sink() {
        return HttpClient::BodyFn{[this](CByteSpan chunk) { (*this)(chunk); }};
    }

    void operator()(CByteSpan chunk) {
        const size_t room = sizeof(text) - 1 - length;
        const size_t take = chunk.size() < room ? chunk.size() : room;
        memcpy(text + length, chunk.data(), take);
        length += take;
        text[length] = '\0';
    }
};

/** Czy w wysłanym żądaniu jest podany fragment. */
bool sentContains(mock::MockClient& client, const char* needle) {
    const CByteSpan sent = client.sent();
    static char copy[4096];
    const size_t length = sent.size() < sizeof(copy) - 1 ? sent.size() : sizeof(copy) - 1;
    memcpy(copy, sent.data(), length);
    copy[length] = '\0';
    return strstr(copy, needle) != nullptr;
}

void inject(mock::MockClient& client, const char* text) {
    client.injectRx(CByteSpan{reinterpret_cast<const u8*>(text), strlen(text)});
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  Adresy
// ═══════════════════════════════════════════════════════════════════════════

TEST("http: rozbior adresu na czesci") {
    u8 scratch[256];

    auto plain = parseUrl("http://example.com/api/v1?x=1", ByteSpan{scratch, sizeof(scratch)});
    REQUIRE(plain.has_value());
    CHECK(!plain->secure);
    CHECK_STR(plain->host, "example.com");
    CHECK_EQ(plain->port, 80);
    CHECK_STR(plain->path, "/api/v1?x=1");
}

TEST("http: https i port niestandardowy") {
    u8 scratch[256];

    auto secure = parseUrl("https://api.local:8443/v2", ByteSpan{scratch, sizeof(scratch)});
    REQUIRE(secure.has_value());
    CHECK(secure->secure);
    CHECK_EQ(secure->port, 8443);
    CHECK_STR(secure->host, "api.local");
    CHECK_STR(secure->path, "/v2");
}

TEST("http: brak sciezki daje ukosnik") {
    u8 scratch[256];
    auto bare = parseUrl("http://example.com", ByteSpan{scratch, sizeof(scratch)});
    REQUIRE(bare.has_value());
    // Serwer odpowiedziałby 400 na żądanie bez ścieżki.
    CHECK_STR(bare->path, "/");
}

TEST("http: adres bez schematu jest odrzucany") {
    u8 scratch[256];
    // Zgadywanie „pewnie http" połączyłoby się z przypadkowym hostem,
    // gdyby wołający podał ścieżkę względną.
    CHECK(!parseUrl("example.com/x", ByteSpan{scratch, sizeof(scratch)}).has_value());
    CHECK(!parseUrl("ftp://example.com/x", ByteSpan{scratch, sizeof(scratch)}).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
//  HTTP — żądanie
// ═══════════════════════════════════════════════════════════════════════════

TEST("http: zadanie ma wiersz statusu, Host i zamkniecie") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    REQUIRE(http.begin(transport, ByteSpan{scratch, sizeof(scratch)}).has_value());

    inject(transport, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi");

    HttpClient::Request request;
    request.host = "example.com";
    request.path = "/status";

    BodySink body;
    auto response = http.send(request, body.sink());
    REQUIRE(response.has_value());

    CHECK(sentContains(transport, "GET /status HTTP/1.1\r\n"));
    // Bez nagłówka Host serwer z wirtualnymi hostami odpowiada 400.
    CHECK(sentContains(transport, "Host: example.com\r\n"));
    CHECK(sentContains(transport, "Connection: close\r\n"));
    // Bez tego serwer ma prawo przysłać treść spakowaną, a rozpakować nie ma czym.
    CHECK(sentContains(transport, "Accept-Encoding: identity\r\n"));

    CHECK_EQ(response->status, 200);
    CHECK_STR(body.text, "hi");
}

TEST("http: port niestandardowy trafia do naglowka Host") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport, "HTTP/1.1 204 No Content\r\n\r\n");

    HttpClient::Request request;
    request.host = "box.local";
    request.port = 8080;

    auto response = http.send(request);
    REQUIRE(response.has_value());
    CHECK(sentContains(transport, "Host: box.local:8080\r\n"));
}

TEST("http: POST niesie typ, dlugosc i tresc") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");

    const char* payload = "{\"v\":42}";
    const HttpClient::Header extra[] = {{"X-Device", "hydra-1"}};

    HttpClient::Request request;
    request.method      = HttpClient::Method::Post;
    request.host        = "api.local";
    request.path        = "/telemetry";
    request.body        = CByteSpan{reinterpret_cast<const u8*>(payload), strlen(payload)};
    request.contentType = "application/json";
    request.headers     = Span<const HttpClient::Header>{extra, 1};

    auto response = http.send(request);
    REQUIRE(response.has_value());

    CHECK(sentContains(transport, "POST /telemetry HTTP/1.1\r\n"));
    CHECK(sentContains(transport, "Content-Type: application/json\r\n"));
    CHECK(sentContains(transport, "Content-Length: 8\r\n"));
    CHECK(sentContains(transport, "X-Device: hydra-1\r\n"));
    CHECK(sentContains(transport, payload));
    CHECK_EQ(response->status, 201);
}

// ═══════════════════════════════════════════════════════════════════════════
//  HTTP — odpowiedź
// ═══════════════════════════════════════════════════════════════════════════

TEST("http: tresc kodowana kawalkami sklada sie w calosc") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    // Serwery HTTP/1.1 używają tego bez pytania, gdy nie znają długości.
    inject(transport,
           "HTTP/1.1 200 OK\r\n"
           "Transfer-Encoding: chunked\r\n\r\n"
           "5\r\nHydra\r\n"
           "1\r\n \r\n"
           "6\r\ndziala\r\n"
           "0\r\n\r\n");

    HttpClient::Request request;
    request.host = "example.com";

    BodySink body;
    auto response = http.send(request, body.sink());
    REQUIRE(response.has_value());

    CHECK(response->chunked);
    CHECK_STR(body.text, "Hydra dziala");
    CHECK_EQ(response->bodyBytes, 12u);
}

TEST("http: naglowki koncowe po kawalku zerowym sa czytane") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    // Nagłówki końcowe zostawione w strumieniu zatrułyby kolejne żądanie
    // na tym samym połączeniu.
    inject(transport,
           "HTTP/1.1 200 OK\r\n"
           "Transfer-Encoding: chunked\r\n\r\n"
           "4\r\ntest\r\n"
           "0\r\n"
           "X-Checksum: abc\r\n"
           "\r\n");

    HttpClient::Request request;
    request.host = "example.com";

    BodySink body;
    auto response = http.send(request, body.sink());
    REQUIRE(response.has_value());
    CHECK_STR(body.text, "test");
    CHECK_EQ(transport.available(), 0u);
}

TEST("http: naglowki trafiaja do wywolania zwrotnego malymi literami") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport,
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "ETag: \"xyz\"\r\n"
           "Content-Length: 0\r\n\r\n");

    char etag[32] = {};
    char type[64] = {};

    HttpClient::Request request;
    request.host = "example.com";

    auto response = http.send(request, {}, HttpClient::HeaderFn{[&](const char* name,
                                                                   const char* value) {
        // Nazwy przychodzą znormalizowane, bo serwery zapisują je różnie
        // i porównywanie z uwzględnieniem wielkości liter byłoby pułapką.
        if (strcmp(name, "etag") == 0) strncpy(etag, value, sizeof(etag) - 1);
        if (strcmp(name, "content-type") == 0) strncpy(type, value, sizeof(type) - 1);
    }});

    REQUIRE(response.has_value());
    CHECK_STR(etag, "\"xyz\"");
    CHECK_STR(type, "application/json");
}

TEST("http: HEAD nie czeka na tresc mimo Content-Length") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    // Serwer podaje długość, ale treści nie przysyła — to jest zgodne
    // z protokołem. Czytanie jej zawisłoby do końca limitu czasu.
    inject(transport, "HTTP/1.1 200 OK\r\nContent-Length: 1234\r\n\r\n");

    HttpClient::Request request;
    request.method = HttpClient::Method::Head;
    request.host   = "example.com";

    auto response = http.send(request);
    REQUIRE(response.has_value());
    CHECK_EQ(response->status, 200);
    CHECK_EQ(response->contentLength, 1234);
    CHECK_EQ(response->bodyBytes, 0u);
}

TEST("http: 204 nie ma tresci") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport, "HTTP/1.1 204 No Content\r\n\r\n");

    HttpClient::Request request;
    request.host = "example.com";

    auto response = http.send(request);
    REQUIRE(response.has_value());
    CHECK_EQ(response->status, 204);
    CHECK_EQ(response->bodyBytes, 0u);
}

TEST("http: status bledu jest wynikiem, nie bledem wywolania") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport, "HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nnie");

    HttpClient::Request request;
    request.host = "example.com";

    BodySink body;
    auto response = http.send(request, body.sink());

    // 404 to poprawnie przeprowadzona rozmowa. Zwracanie tu błędu zmusiłoby
    // wołającego do odróżniania „serwer odmówił" od „nie było łączności".
    REQUIRE(response.has_value());
    CHECK_EQ(response->status, 404);
    CHECK(!response->ok());
    CHECK_STR(body.text, "nie");
}

TEST("http: smieci zamiast odpowiedzi to blad protokolu") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport, "coś, co nie jest HTTP\r\n\r\n");

    HttpClient::Request request;
    request.host = "example.com";

    auto response = http.send(request);
    REQUIRE(!response.has_value());
    CHECK(response.error() == Err::Protocol);
}

// ═══════════════════════════════════════════════════════════════════════════
//  HTTP — przekierowania i bufory
// ═══════════════════════════════════════════════════════════════════════════

TEST("http: przekierowanie wzgledne jest sledzone") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport,
           "HTTP/1.1 302 Found\r\n"
           "Location: /nowa\r\n"
           "Content-Length: 0\r\n\r\n"
           "HTTP/1.1 200 OK\r\n"
           "Content-Length: 2\r\n\r\nok");

    HttpClient::Request request;
    request.host      = "example.com";
    request.path      = "/stara";
    request.keepAlive = true;

    BodySink body;
    auto response = http.send(request, body.sink());
    REQUIRE(response.has_value());

    CHECK_EQ(response->status, 200);
    CHECK_EQ(response->redirects, 1);
    CHECK(sentContains(transport, "GET /nowa HTTP/1.1\r\n"));
    CHECK_STR(body.text, "ok");
}

TEST("http: wyczerpany limit przekierowan oddaje status 30x") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport,
           "HTTP/1.1 302 Found\r\nLocation: /a\r\nContent-Length: 0\r\n\r\n"
           "HTTP/1.1 302 Found\r\nLocation: /b\r\nContent-Length: 0\r\n\r\n");

    HttpClient::Request request;
    request.host         = "example.com";
    request.keepAlive    = true;
    request.maxRedirects = 1;

    auto response = http.send(request);
    REQUIRE(response.has_value());
    // Zapętlone przekierowanie ma się skończyć widocznym wynikiem, nie pętlą.
    CHECK_EQ(response->status, 302);
}

TEST("http: tresc dluzsza niz bufor jest zglaszana, a nie ucinana po cichu") {
    mock::MockClient transport;
    u8 scratch[512];
    HttpClient http;
    (void)http.begin(transport, ByteSpan{scratch, sizeof(scratch)});

    inject(transport, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789");

    u8 small[4];
    size_t written = 0;
    auto response = http.getToBuffer("http://example.com/x", ByteSpan{small, sizeof(small)},
                                     &written);

    REQUIRE(!response.has_value());
    CHECK(response.error() == Err::OutOfRange);
    CHECK_EQ(written, 4u);
    // Odpowiedź została doczytana do końca — strumień nadaje się do dalszej pracy.
    CHECK_EQ(transport.available(), 0u);
}

TEST("http: za maly bufor roboczy jest odrzucany od razu") {
    mock::MockClient transport;
    u8 tiny[64];
    HttpClient http;
    // Nagłówek Location bywa dłuższy niż to; obcięcie wiersza dawałoby błąd
    // parsowania w miejscu, które niczego nie tłumaczy.
    CHECK(!http.begin(transport, ByteSpan{tiny, sizeof(tiny)}).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
//  UDP
// ═══════════════════════════════════════════════════════════════════════════

TEST("udp: wysylka trafia pod ustawiony adres") {
    mock::MockUdp socket;
    UdpClient udp;
    REQUIRE(udp.begin(socket, 5000).has_value());
    CHECK_EQ(udp.localPort(), 5000);

    REQUIRE(udp.setPeer(Endpoint{ipv4(192, 168, 0, 10), 5005}).has_value());

    const u8 payload[] = {1, 2, 3};
    REQUIRE(udp.send(CByteSpan{payload, sizeof(payload)}).has_value());

    CHECK_EQ(socket.lastDestination().ipv4, ipv4(192, 168, 0, 10));
    CHECK_EQ(socket.lastDestination().port, 5005);
    CHECK_EQ(socket.lastSent().size(), 3u);
}

TEST("udp: port zerowy dostaje przydzielony przez system") {
    mock::MockUdp socket;
    UdpClient udp;
    REQUIRE(udp.begin(socket).has_value());
    // Wołający musi mieć jak odczytać przydzielony port — inaczej nie poda go
    // drugiej stronie.
    CHECK(udp.localPort() != 0);
}

TEST("udp: wysylka bez rozmowcy jest bledem, nie pakietem donikad") {
    mock::MockUdp socket;
    UdpClient udp;
    (void)udp.begin(socket);

    const u8 payload[] = {1};
    CHECK(!udp.send(CByteSpan{payload, 1}).has_value());
    CHECK_EQ(socket.sentCount(), 0u);
}

TEST("udp: datagram od rozmowcy dochodzi w calosci") {
    mock::MockUdp socket;
    UdpClient udp;
    (void)udp.begin(socket, 5000);
    (void)udp.setPeer(Endpoint{ipv4(10, 0, 0, 1), 123});

    const u8 reply[] = {0xAA, 0xBB, 0xCC};
    REQUIRE(socket.injectRx(Endpoint{ipv4(10, 0, 0, 1), 123},
                            CByteSpan{reply, sizeof(reply)}).has_value());

    u8 buffer[16];
    auto got = udp.receive(ByteSpan{buffer, sizeof(buffer)});
    REQUIRE(got.has_value());
    CHECK_EQ(got->size, 3u);
    CHECK(!got->truncated);
    CHECK_EQ(buffer[0], 0xAA);
    CHECK_EQ(got->from.port, 123);
}

TEST("udp: datagram spoza rozmowcy jest odrzucany") {
    mock::MockUdp socket;
    UdpClient udp;
    (void)udp.begin(socket, 5000);
    (void)udp.setPeer(Endpoint{ipv4(10, 0, 0, 1), 123});

    // Na port, z którego wysłaliśmy zapytanie, może odpowiedzieć ktokolwiek —
    // na tym polega podszywanie się pod serwer.
    const u8 spoof[] = {0xDE, 0xAD};
    (void)socket.injectRx(Endpoint{ipv4(10, 0, 0, 99), 123}, CByteSpan{spoof, sizeof(spoof)});

    u8 buffer[16];
    auto got = udp.receive(ByteSpan{buffer, sizeof(buffer)});
    CHECK(!got.has_value());
    CHECK_EQ(udp.rejected(), 1u);
}

TEST("udp: zdjecie filtru przepuszcza obcy datagram") {
    mock::MockUdp socket;
    UdpClient udp;
    (void)udp.begin(socket, 5000);
    (void)udp.setPeer(Endpoint{ipv4(10, 0, 0, 1), 123});
    udp.acceptFromAnyone();

    const u8 data[] = {7};
    (void)socket.injectRx(Endpoint{ipv4(10, 0, 0, 99), 5353}, CByteSpan{data, 1});

    u8 buffer[16];
    auto got = udp.receive(ByteSpan{buffer, sizeof(buffer)});
    REQUIRE(got.has_value());
    CHECK_EQ(got->from.ipv4, ipv4(10, 0, 0, 99));
}

TEST("udp: obciety datagram jest zglaszany, bo reszta przepada") {
    mock::MockUdp socket;
    UdpClient udp;
    (void)udp.begin(socket, 5000);
    (void)udp.setPeer(Endpoint{ipv4(10, 0, 0, 1), 123});

    const u8 big[64] = {};
    (void)socket.injectRx(Endpoint{ipv4(10, 0, 0, 1), 123}, CByteSpan{big, sizeof(big)});

    u8 small[8];
    auto got = udp.receive(ByteSpan{small, sizeof(small)});
    REQUIRE(got.has_value());
    CHECK_EQ(got->size, 8u);
    // Przy strumieniu resztę doczytałoby się później. Przy datagramie nie ma
    // czego doczytywać — i wołający musi o tym wiedzieć.
    CHECK(got->truncated);
    CHECK_EQ(udp.truncated(), 1u);
}

TEST("udp: brak datagramu to WouldBlock, nie blad") {
    mock::MockUdp socket;
    UdpClient udp;
    (void)udp.begin(socket, 5000);

    u8 buffer[8];
    auto got = udp.receive(ByteSpan{buffer, sizeof(buffer)});
    CHECK(!got.has_value());
    CHECK(got.error() == Err::WouldBlock);
}

TEST("udp: rozgloszenie wymaga wlaczenia zgody") {
    mock::MockUdp socket;
    UdpClient udp;
    (void)udp.begin(socket, 5000);

    const u8 hello[] = {1, 2};

    // Wysyłka wprost pod adres rozgłoszeniowy bez zgody musi odmówić —
    // prawdziwe gniazdo zwraca tu EACCES.
    CHECK(!udp.sendTo(Endpoint{kBroadcastIpv4, 5005}, CByteSpan{hello, 2}).has_value());

    // `broadcast()` włącza zgodę jawnie i dopiero wtedy pakiet wychodzi.
    REQUIRE(udp.broadcast(5005, CByteSpan{hello, 2}).has_value());
    CHECK(socket.broadcastEnabled());
    CHECK_EQ(socket.lastDestination().ipv4, kBroadcastIpv4);
}

TEST("udp: datagram wiekszy niz limit nie wychodzi") {
    mock::MockUdp socket;
    UdpClient udp;
    (void)udp.begin(socket, 5000);
    (void)udp.setPeer(Endpoint{ipv4(10, 0, 0, 1), 123});

    static const u8 huge[2000] = {};
    // Pofragmentowany UDP gubi się na pierwszym lepszym routerze — lepiej
    // odmówić u siebie niż zgubić po drodze.
    CHECK(!udp.send(CByteSpan{huge, sizeof(huge)}).has_value());
    CHECK_EQ(socket.sentCount(), 0u);
}

TEST("udp: nazwa rozwiazywana przez interfejs sieciowy") {
    mock::MockNetwork net;
    (void)net.begin();
    net.setLinkUp(true);
    REQUIRE(net.addHost("broker.local", ipv4(10, 0, 0, 7)).has_value());

    UdpClient udp;
    REQUIRE(udp.begin(net.udp, 0).has_value());

    REQUIRE(udp.setPeer(net, "broker.local", 1883).has_value());
    CHECK_EQ(udp.peer().ipv4, ipv4(10, 0, 0, 7));

    // Literówka w nazwie ma się wywalić tutaj, a nie na wysyłce pod 0.0.0.0.
    CHECK(!udp.setPeer(net, "brokerr.local", 1883).has_value());
}

#endif  // HYDRA_ENABLE_NET
