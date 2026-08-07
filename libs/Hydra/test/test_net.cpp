/** Testy maszyny stanów połączenia i modułu sieciowego (rozdz. 7.1). */

#include "hydra_test.hpp"

#include <stdio.h>
#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/net/ConnectionManager.hpp"
#include "hydra/net/Mock.hpp"
#include "hydra/net/NetModule.hpp"
#include "hydra/net/TlsClient.hpp"

using namespace hydra;
using namespace hydra::net;

namespace {

void resetNet() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

NetworkCredentials creds(const char* ssid, const char* psk) {
    NetworkCredentials c;
    strncpy(c.ssid, ssid, kSsidMax - 1);
    c.psk.set(psk);
    return c;
}

/** Domyślna konfiguracja z krótkimi czasami — testy sterują zegarem same. */
ConnectionManager::Config fastConfig() {
    ConnectionManager::Config cfg;
    cfg.connectTimeoutMs   = 1000;
    cfg.backoffBaseMs      = 100;
    cfg.backoffMaxMs       = 2000;
    cfg.attemptsPerNetwork = 2;
    return cfg;
}

}  // namespace

// ---------------------------------------------------------------------------
// Poświadczenia
// ---------------------------------------------------------------------------

TEST("SecretString: maskuje zawartość, odsłania tylko na żądanie") {
    SecretString<32> psk("tajne-haslo");

    CHECK_STR(psk.masked(), "********");
    CHECK_STR(psk.reveal(), "tajne-haslo");
    CHECK_EQ(static_cast<int>(psk.length()), 11);

    // Maska nie zdradza długości sekretu — krótkie i długie wyglądają tak samo.
    SecretString<32> other("x");
    CHECK_STR(other.masked(), "********");

    psk.clear();
    CHECK(psk.empty());
    CHECK_STR(psk.masked(), "(puste)");
    CHECK_STR(psk.reveal(), "");
}

TEST("SecretString: wartość dłuższa niż pojemność jest odrzucana") {
    // Pojemność liczona jest w bajtach, nie w znakach — polskie znaki
    // w UTF-8 zajmują po dwa bajty i to one wyznaczają granicę.
    SecretString<8> s;
    CHECK(s.set("1234567").has_value());   // 7 bajtów + terminator
    CHECK(s.set("12345678").error() == Err::OutOfRange);
    // Nieudany zapis nie zostawia śmieci po poprzedniej wartości.
    CHECK(s.empty());

    SecretString<8> utf;
    CHECK(utf.set("żółw").has_value());        // 7 bajtów
    CHECK(utf.set("żółwik").error() == Err::OutOfRange);
}

// ---------------------------------------------------------------------------
// Maszyna stanów
// ---------------------------------------------------------------------------

TEST("ConnectionManager: droga od Idle do Online") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);

    int       changes = 0;
    ConnState last    = ConnState::Idle;
    auto sub = EventBus::subscribe<ConnStateChanged>([&](const ConnStateChanged& e) {
        ++changes;
        last = e.to;
    });
    REQUIRE(sub.has_value());

    u32 gotAddress = 0;
    auto sub2 = EventBus::subscribe<NetGotAddress>([&](const NetGotAddress& e) {
        gotAddress = e.ipv4;
    });
    REQUIRE(sub2.has_value());

    REQUIRE(conn.init(fastConfig()).has_value());
    CHECK(net.begun());
    REQUIRE(conn.addNetwork(creds("dom", "haslo")).has_value());

    REQUIRE(conn.start(1000).has_value());
    CHECK(conn.state() == ConnState::Connecting);
    CHECK_EQ(static_cast<int>(net.connectCalls()), 1);
    CHECK_STR(net.lastSsid(), "dom");

    // Łącze jeszcze nie wstało — zostajemy w Connecting.
    conn.tick(1100);
    CHECK(conn.state() == ConnState::Connecting);

    net.setLinkUp(true, 0xC0A80105u);
    conn.tick(1200);
    CHECK(conn.state() == ConnState::Online);
    CHECK(last == ConnState::Online);
    CHECK_EQ(static_cast<long long>(gotAddress), 0xC0A80105LL);
    CHECK_EQ(static_cast<int>(conn.stats().connects), 1);
}

TEST("ConnectionManager: bez sieci na liście start jest odrzucany") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);

    REQUIRE(conn.init(fastConfig()).has_value());
    CHECK(conn.start(0).error() == Err::NotFound);

    // Konfiguracja bez sensu też nie przechodzi.
    ConnectionManager::Config bad = fastConfig();
    bad.backoffBaseMs = 0;
    CHECK(conn.init(bad).error() == Err::BadArgument);
}

TEST("ConnectionManager: backoff rośnie wykładniczo do sufitu") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());

    CHECK_EQ(static_cast<int>(conn.backoffFor(0)), 100);
    CHECK_EQ(static_cast<int>(conn.backoffFor(1)), 200);
    CHECK_EQ(static_cast<int>(conn.backoffFor(2)), 400);
    CHECK_EQ(static_cast<int>(conn.backoffFor(3)), 800);
    // Sufit obcina wzrost i chroni przed przekręceniem licznika.
    CHECK_EQ(static_cast<int>(conn.backoffFor(10)), 2000);
    CHECK_EQ(static_cast<int>(conn.backoffFor(1000)), 2000);
}

TEST("ConnectionManager: nieudana próba czeka backoff i ponawia") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());
    REQUIRE(conn.addNetwork(creds("dom", "haslo")).has_value());

    REQUIRE(conn.start(0).has_value());

    // Łącze nie wstaje w zadanym czasie → Reconnecting z odczekaniem.
    conn.tick(1001);
    CHECK(conn.state() == ConnState::Reconnecting);
    CHECK_EQ(static_cast<int>(conn.stats().failures), 1);
    CHECK_EQ(static_cast<long long>(conn.retryAt()), 1001LL + 200LL);

    // Przed upływem backoffu nic się nie dzieje.
    conn.tick(1100);
    CHECK(conn.state() == ConnState::Reconnecting);
    CHECK_EQ(static_cast<int>(net.connectCalls()), 1);

    conn.tick(1201);
    CHECK(conn.state() == ConnState::Connecting);
    CHECK_EQ(static_cast<int>(net.connectCalls()), 2);
}

TEST("ConnectionManager: po serii nieudanych prób sięga po sieć zapasową") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());  // 2 próby na sieć
    REQUIRE(conn.addNetwork(creds("dom", "a")).has_value());
    REQUIRE(conn.addNetwork(creds("zapasowa", "b")).has_value());

    Millis now = 0;
    REQUIRE(conn.start(now).has_value());
    CHECK_STR(net.lastSsid(), "dom");

    // Dwie nieudane próby na pierwszej sieci.
    for (int i = 0; i < 2; ++i) {
        now += 1001;
        conn.tick(now);
        CHECK(conn.state() == ConnState::Reconnecting);
        now = conn.retryAt();
        conn.tick(now);
    }

    CHECK_EQ(static_cast<int>(conn.currentNetwork()), 1);
    CHECK_STR(net.lastSsid(), "zapasowa");
}

TEST("ConnectionManager: zerwane łącze wraca przez Reconnecting") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());
    REQUIRE(conn.addNetwork(creds("dom", "a")).has_value());

    u32 lostUptime = 0;
    int losses     = 0;
    auto sub = EventBus::subscribe<NetLost>([&](const NetLost& e) {
        ++losses;
        lostUptime = e.uptimeSec;
    });
    REQUIRE(sub.has_value());

    REQUIRE(conn.start(0).has_value());
    net.setLinkUp(true);
    conn.tick(100);
    CHECK(conn.state() == ConnState::Online);

    // Łącze pada po pięciu sekundach.
    net.setLinkUp(false);
    conn.tick(5100);
    CHECK(conn.state() == ConnState::Reconnecting);
    CHECK_EQ(losses, 1);
    CHECK_EQ(static_cast<int>(lostUptime), 5);
    CHECK_EQ(static_cast<int>(conn.stats().disconnects), 1);

    // Po odczekaniu i powrocie łącza wracamy do Online.
    conn.tick(conn.retryAt());
    CHECK(conn.state() == ConnState::Connecting);
    net.setLinkUp(true);
    conn.tick(conn.retryAt() + 10);
    CHECK(conn.state() == ConnState::Online);
}

TEST("ConnectionManager: słaby sygnał daje Degraded bez zrywania łącza") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);

    ConnectionManager::Config cfg = fastConfig();
    cfg.degradedRssiDbm = -70;
    REQUIRE(conn.init(cfg).has_value());
    REQUIRE(conn.addNetwork(creds("dom", "a")).has_value());

    REQUIRE(conn.start(0).has_value());
    net.setLinkUp(true);
    net.setRssi(-50);
    conn.tick(100);
    CHECK(conn.state() == ConnState::Online);

    net.setRssi(-85);
    conn.tick(200);
    CHECK(conn.state() == ConnState::Degraded);
    // Łącze nadal działa — nie ma zerwania ani ponownego łączenia.
    CHECK_EQ(static_cast<int>(conn.stats().disconnects), 0);
    CHECK_EQ(static_cast<int>(net.connectCalls()), 1);

    net.setRssi(-55);
    conn.tick(300);
    CHECK(conn.state() == ConnState::Online);
}

TEST("ConnectionManager: utrata usługi to Degraded, nie zerwanie Wi-Fi") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());
    REQUIRE(conn.addNetwork(creds("dom", "a")).has_value());

    REQUIRE(conn.start(0).has_value());
    net.setLinkUp(true);
    conn.tick(100);
    CHECK(conn.state() == ConnState::Online);

    // Broker padł, ale łącze jest sprawne.
    conn.reportServiceHealth(false);
    conn.tick(200);
    CHECK(conn.state() == ConnState::Degraded);
    CHECK_EQ(static_cast<int>(net.connectCalls()), 1);  // Wi-Fi nietknięte

    conn.reportServiceHealth(true);
    conn.tick(300);
    CHECK(conn.state() == ConnState::Online);
}

TEST("ConnectionManager: odrzucona próba przez interfejs też uruchamia backoff") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());
    REQUIRE(conn.addNetwork(creds("dom", "a")).has_value());

    net.failNextConnect(Err::NotSupported);
    REQUIRE(conn.start(500).has_value());

    CHECK(conn.state() == ConnState::Reconnecting);
    CHECK_EQ(static_cast<int>(conn.stats().failures), 1);
    CHECK(conn.retryAt() > 500);
}

TEST("ConnectionManager: stop wraca do Idle i rozłącza interfejs") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());
    REQUIRE(conn.addNetwork(creds("dom", "a")).has_value());

    REQUIRE(conn.start(0).has_value());
    net.setLinkUp(true);
    conn.tick(100);

    conn.stop();
    CHECK(conn.state() == ConnState::Idle);
    CHECK(!net.linkUp());

    // Z Idle tykanie nie robi nic.
    conn.tick(200);
    CHECK(conn.state() == ConnState::Idle);
}

// ---------------------------------------------------------------------------
// Poświadczenia w pamięci trwałej
// ---------------------------------------------------------------------------

TEST("ConnectionManager: sieci przeżywają restart urządzenia") {
    resetNet();

    {
        mock::MockNetwork net;
        ConnectionManager conn(net);
        REQUIRE(conn.init(fastConfig()).has_value());
        REQUIRE(conn.addNetwork(creds("dom", "haslo-domowe")).has_value());
        REQUIRE(conn.addNetwork(creds("warsztat", "haslo-warsztat")).has_value());
        REQUIRE(conn.saveNetworks().has_value());
    }

    // Nowa instancja to odpowiednik ponownego uruchomienia — pamięć atrapy
    // przeżywa, tak jak NVS przeżywa reset MCU.
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());
    REQUIRE(conn.loadNetworks().has_value());

    CHECK_EQ(static_cast<int>(conn.networkCount()), 2);
    CHECK_STR(conn.network(0)->ssid, "dom");
    CHECK_STR(conn.network(1)->ssid, "warsztat");
    CHECK_STR(conn.network(0)->psk.reveal(), "haslo-domowe");

    // Urządzenie samo wraca online, bez udziału człowieka.
    REQUIRE(conn.start(0).has_value());
    net.setLinkUp(true);
    conn.tick(100);
    CHECK(conn.state() == ConnState::Online);
}

TEST("ConnectionManager: pusta pamięć zgłasza brak sieci") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());

    CHECK(conn.loadNetworks().error() == Err::NotFound);
    CHECK_EQ(static_cast<int>(conn.networkCount()), 0);
}

TEST("ConnectionManager: forgetNetworks czyści pamięć trwałą") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());
    REQUIRE(conn.addNetwork(creds("dom", "a")).has_value());
    REQUIRE(conn.saveNetworks().has_value());

    REQUIRE(conn.forgetNetworks().has_value());
    CHECK_EQ(static_cast<int>(conn.networkCount()), 0);
    CHECK(conn.loadNetworks().error() == Err::NotFound);
}

TEST("ConnectionManager: lista sieci ma twardy limit") {
    resetNet();
    mock::MockNetwork net;
    ConnectionManager conn(net);
    REQUIRE(conn.init(fastConfig()).has_value());

    for (u8 i = 0; i < HYDRA_NET_MAX_NETWORKS; ++i) {
        char ssid[8];
        snprintf(ssid, sizeof(ssid), "s%u", static_cast<unsigned>(i));
        REQUIRE(conn.addNetwork(creds(ssid, "x")).has_value());
    }
    CHECK(conn.addNetwork(creds("nadmiar", "x")).error() == Err::OutOfMemory);
    CHECK(conn.addNetwork(creds("", "x")).error() == Err::BadArgument);
}

// ---------------------------------------------------------------------------
// Gniazdo szyfrowane
// ---------------------------------------------------------------------------

namespace {

/** Atrapa o kształcie API WiFiClientSecure. */
struct FakeSecureClient {
    const char* ca          = nullptr;
    const char* cert        = nullptr;
    const char* key         = nullptr;
    bool        insecure    = false;
    char        host[64]    = {};
    u16         port        = 0;
    i32         timeout     = 0;
    bool        open        = false;
    bool        refuse      = false;

    void setCACert(const char* pem) { ca = pem; }
    void setInsecure() { insecure = true; }
    void setCertificate(const char* pem) { cert = pem; }
    void setPrivateKey(const char* pem) { key = pem; }

    bool connect(const char* h, u16 p, i32 t) {
        strncpy(host, h, sizeof(host) - 1);
        port    = p;
        timeout = t;
        open    = !refuse;
        return open;
    }
    void   stop() { open = false; }
    bool   connected() { return open; }
    size_t write(const u8*, size_t n) { return n; }
    int    read(u8*, size_t) { return 0; }
    int    available() { return 0; }
};

const char* kFakeCa = "-----BEGIN CERTIFICATE-----\nUDAWANY\n-----END CERTIFICATE-----\n";

}  // namespace

TEST("TLS: bez certyfikatu i bez jawnej zgody konfiguracja jest odrzucana") {
    resetNet();
    FakeSecureClient           secure;
    TlsClient<FakeSecureClient> client(secure);

    // Brak jakiegokolwiek sposobu weryfikacji to pomyłka, nie wybór.
    TlsClient<FakeSecureClient>::Config cfg;
    CHECK(client.configure(cfg).error() == Err::BadArgument);

    // Bez konfiguracji nie ma czym się łączyć.
    CHECK(client.connect("broker.local", 8883, 5000).error() == Err::NotInitialized);
}

TEST("TLS: certyfikat urzędu trafia do stosu przed połączeniem") {
    resetNet();
    FakeSecureClient           secure;
    TlsClient<FakeSecureClient> client(secure);

    TlsClient<FakeSecureClient>::Config cfg;
    cfg.caCertificate = kFakeCa;
    REQUIRE(client.configure(cfg).has_value());
    CHECK(client.verifiesPeer());

    REQUIRE(client.connect("broker.local", 8883, 1000).has_value());
    CHECK(secure.ca == kFakeCa);
    CHECK(!secure.insecure);
    CHECK_STR(secure.host, "broker.local");
    CHECK_EQ(static_cast<int>(secure.port), 8883);
}

TEST("TLS: limit uzgadniania nie schodzi poniżej wartości z konfiguracji") {
    resetNet();
    FakeSecureClient           secure;
    TlsClient<FakeSecureClient> client(secure);

    TlsClient<FakeSecureClient>::Config cfg;
    cfg.caCertificate      = kFakeCa;
    cfg.handshakeTimeoutMs = 15000;
    REQUIRE(client.configure(cfg).has_value());

    // Limit dobrany pod samo TCP zrywałby poprawne połączenia w trakcie
    // uzgadniania, które trwa wielokrotnie dłużej.
    REQUIRE(client.connect("broker.local", 8883, 2000).has_value());
    CHECK_EQ(static_cast<int>(secure.timeout), 15000);
}

TEST("TLS: tryb bez weryfikacji wymaga jawnego włączenia") {
    resetNet();
    FakeSecureClient           secure;
    TlsClient<FakeSecureClient> client(secure);

    TlsClient<FakeSecureClient>::Config cfg;
    cfg.allowInsecure = true;
    REQUIRE(client.configure(cfg).has_value());
    // TLS bez weryfikacji wygląda na bezpieczny, a chroni wyłącznie przed
    // biernym podsłuchem — stan musi być widoczny dla warstwy wyżej.
    CHECK(!client.verifiesPeer());

    REQUIRE(client.connect("broker.local", 8883, 1000).has_value());
    CHECK(secure.insecure);
    CHECK(secure.ca == nullptr);
}

TEST("TLS: uwierzytelnianie dwustronne wymaga pary certyfikat-klucz") {
    resetNet();
    FakeSecureClient           secure;
    TlsClient<FakeSecureClient> client(secure);

    TlsClient<FakeSecureClient>::Config cfg;
    cfg.caCertificate     = kFakeCa;
    cfg.clientCertificate = "cert";
    // Sam certyfikat bez klucza nie pozwoli się uwierzytelnić — lepiej
    // odrzucić konfigurację niż odkryć to przy pierwszym połączeniu.
    CHECK(client.configure(cfg).error() == Err::BadArgument);

    cfg.clientKey = "key";
    REQUIRE(client.configure(cfg).has_value());
    CHECK(client.mutualAuth());

    REQUIRE(client.connect("broker.local", 8883, 1000).has_value());
    CHECK_STR(secure.cert, "cert");
    CHECK_STR(secure.key, "key");
}

TEST("TLS: odrzucone połączenie zgłasza limit czasu") {
    resetNet();
    FakeSecureClient           secure;
    secure.refuse = true;
    TlsClient<FakeSecureClient> client(secure);

    TlsClient<FakeSecureClient>::Config cfg;
    cfg.caCertificate = kFakeCa;
    REQUIRE(client.configure(cfg).has_value());

    CHECK(client.connect("broker.local", 8883, 1000).error() == Err::Timeout);
    CHECK(!client.connected());
}

TEST("TLS: spełnia kontrakt gniazda i da się podstawić pod MQTT") {
    resetNet();
    FakeSecureClient           secure;
    TlsClient<FakeSecureClient> client(secure);

    TlsClient<FakeSecureClient>::Config cfg;
    cfg.caCertificate = kFakeCa;
    REQUIRE(client.configure(cfg).has_value());

    // Klient MQTT przyjmuje IClient&, więc szyfrowanie włącza się podmianą
    // jednego obiektu — bez zmian w warstwie protokołu.
    IClient& asClient = client;
    REQUIRE(asClient.connect("broker.local", kTlsDefaultPort, 1000).has_value());
    CHECK(asClient.connected());

    const u8 payload[4] = {1, 2, 3, 4};
    CHECK_EQ(static_cast<int>(asClient.write(CByteSpan{payload, 4})), 4);
    asClient.stop();
    CHECK(!asClient.connected());
}
