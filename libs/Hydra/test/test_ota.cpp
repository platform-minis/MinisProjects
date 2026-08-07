/**
 * Testy skrótu i aktualizacji przez sieć (etap M6b).
 *
 * Skrót sprawdzany jest wektorami z FIPS 180-4 i RFC 4231 — implementacja
 * własna wymaga dowodu poprawności, a nie zaufania. Aktualizacja przechodzi
 * całą drogę na atrapach: pobranie, weryfikację, przełączenie, tryb próbny
 * i powrót, wraz ze wszystkimi sposobami, na jakie może się nie udać.
 */

#include "hydra_test.hpp"

#include <stdio.h>
#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/net/Mock.hpp"
#include "hydra/ota/Mock.hpp"
#include "hydra/ota/OtaUpdater.hpp"
#include "hydra/util/Sha256.hpp"

using namespace hydra;
using namespace hydra::util;
using namespace hydra::ota;

namespace {

void resetOta() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

/** Skrót w zapisie szesnastkowym — wygodniej porównywać niż bajty. */
void hashHex(CByteSpan data, char* out) {
    u8 digest[kSha256Size];
    Sha256::hash(data, digest);
    Sha256::toHex(digest, out, kSha256Size * 2 + 1);
}

/** Buduje odpowiedź HTTP z podanym ciałem. */
struct HttpResponse {
    static constexpr size_t kMax = 4096;
    u8     data[kMax] = {};
    size_t length     = 0;

    void build(int status, const u8* body, size_t bodyLength, bool withLength = true) {
        char header[192];
        int  n;
        if (withLength) {
            n = snprintf(header, sizeof(header),
                         "HTTP/1.1 %d OK\r\nContent-Type: application/octet-stream\r\n"
                         "Content-Length: %lu\r\n\r\n",
                         status, static_cast<unsigned long>(bodyLength));
        } else {
            n = snprintf(header, sizeof(header),
                         "HTTP/1.1 %d OK\r\nTransfer-Encoding: chunked\r\n\r\n", status);
        }
        length = 0;
        memcpy(data, header, static_cast<size_t>(n));
        length += static_cast<size_t>(n);
        if (body && bodyLength) {
            memcpy(data + length, body, bodyLength);
            length += bodyLength;
        }
    }

    CByteSpan span() const { return CByteSpan{data, length}; }
};

/** Zestaw do testów aktualizacji: gniazdo atrapowe plus magazyn obrazu. */
struct Rig {
    net::mock::MockClient        client;
    mock::MockFirmwareStore      store;
    OtaUpdater                   updater;
    u8                           firmware[1024] = {};
    char                         expectedHash[kSha256Size * 2 + 1] = {};

    Rig() {
        // Zawartość obrazu bez znaczenia — istotne, że jest powtarzalna.
        for (size_t i = 0; i < sizeof(firmware); ++i) firmware[i] = static_cast<u8>(i * 7 + 3);
        hashHex(CByteSpan{firmware, sizeof(firmware)}, expectedHash);
    }

    OtaUpdater::Config config() {
        OtaUpdater::Config cfg;
        cfg.host           = "aktualizacje.local";
        cfg.port           = 80;
        cfg.path           = "/rover.bin";
        cfg.expectedSha256 = expectedHash;
        cfg.stallTimeoutMs = 1000;
        return cfg;
    }

    /** Doprowadza aktualizację do końca, podając odpowiedź serwera. */
    void runToCompletion(const HttpResponse& response, Millis start = 100) {
        client.injectRx(response.span());
        // Ciche obcięcie w atrapie wyglądałoby jak błąd pobierania.
        CHECK_EQ(static_cast<int>(client.injectDropped()), 0);
        for (int i = 0; i < 40; ++i) {
            updater.step(start + static_cast<Millis>(i * 10));
            if (updater.state() == OtaState::Ready || updater.state() == OtaState::Failed) {
                break;
            }
        }
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------

TEST("SHA-256: wektory testowe z FIPS 180-4") {
    char hex[kSha256Size * 2 + 1];

    hashHex(CByteSpan{reinterpret_cast<const u8*>("abc"), 3}, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    hashHex(CByteSpan{}, hex);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const char* longer = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    hashHex(CByteSpan{reinterpret_cast<const u8*>(longer), strlen(longer)}, hex);
    CHECK_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST("SHA-256: podawanie fragmentami daje ten sam wynik") {
    const char* text = "Hydra Framework aktualizacja oprogramowania";
    const size_t length = strlen(text);

    u8 whole[kSha256Size];
    Sha256::hash(CByteSpan{reinterpret_cast<const u8*>(text), length}, whole);

    // Obraz przychodzi z gniazda porcjami o dowolnej długości — skrót musi
    // wyjść ten sam niezależnie od tego, jak został pocięty.
    Sha256 piecewise;
    for (size_t i = 0; i < length; i += 7) {
        const size_t take = (length - i) < 7 ? (length - i) : 7;
        piecewise.update(CByteSpan{reinterpret_cast<const u8*>(text) + i, take});
    }
    u8 pieces[kSha256Size];
    piecewise.finish(pieces);

    CHECK(Sha256::equal(whole, pieces));
}

TEST("SHA-256: zapis szesnastkowy w obie strony") {
    u8 digest[kSha256Size];
    Sha256::hash(CByteSpan{reinterpret_cast<const u8*>("abc"), 3}, digest);

    char hex[kSha256Size * 2 + 1];
    Sha256::toHex(digest, hex, sizeof(hex));

    u8 parsed[kSha256Size];
    CHECK(Sha256::fromHex(hex, parsed));
    CHECK(Sha256::equal(digest, parsed));

    // Zapis niepełny, z niedozwolonym znakiem albo z ogonem musi być odrzucony.
    CHECK(!Sha256::fromHex("abc", parsed));
    CHECK(!Sha256::fromHex(
        "zzz816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", parsed));
    CHECK(!Sha256::fromHex(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015adXX", parsed));
}

TEST("SHA-256: porównanie nie zdradza liczby zgodnych bajtów") {
    u8 a[kSha256Size] = {};
    u8 b[kSha256Size] = {};
    CHECK(Sha256::equal(a, b));

    // Różnica na ostatnim bajcie musi być wykryta tak samo jak na pierwszym —
    // wcześniejsze wyjście pozwalałoby odgadnąć skrót bajt po bajcie.
    b[kSha256Size - 1] = 1;
    CHECK(!Sha256::equal(a, b));
    b[kSha256Size - 1] = 0;
    b[0]               = 1;
    CHECK(!Sha256::equal(a, b));
}

TEST("HMAC-SHA256: wektory testowe z RFC 4231") {
    u8 key[20];
    memset(key, 0x0b, sizeof(key));

    u8 mac[kSha256Size];
    HmacSha256::compute(CByteSpan{key, sizeof(key)},
                        CByteSpan{reinterpret_cast<const u8*>("Hi There"), 8}, mac);

    char hex[kSha256Size * 2 + 1];
    Sha256::toHex(mac, hex, sizeof(hex));
    CHECK_STR(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST("HMAC-SHA256: klucz dłuższy niż blok zastępuje się skrótem") {
    u8 longKey[100];
    memset(longKey, 0xAA, sizeof(longKey));

    u8 mac[kSha256Size];
    HmacSha256::compute(CByteSpan{longKey, sizeof(longKey)},
                        CByteSpan{reinterpret_cast<const u8*>("dane"), 4}, mac);

    // Ten sam klucz podany jako własny skrót daje ten sam wynik — tak
    // wymaga RFC 2104 i tak sprawdzi to druga strona.
    u8 hashedKey[kSha256Size];
    Sha256::hash(CByteSpan{longKey, sizeof(longKey)}, hashedKey);

    u8 macFromHashed[kSha256Size];
    HmacSha256::compute(CByteSpan{hashedKey, kSha256Size},
                        CByteSpan{reinterpret_cast<const u8*>("dane"), 4}, macFromHashed);
    CHECK(Sha256::equal(mac, macFromHashed));
}

// ---------------------------------------------------------------------------
// Magazyn obrazu
// ---------------------------------------------------------------------------

TEST("Magazyn: obraz większy niż miejsce jest odrzucany przed pobraniem") {
    mock::MockFirmwareStore store;
    store.setCapacity(1024);

    // Dowiedzenie się o tym po ściągnięciu kilkuset kilobajtów byłoby
    // marnotrawstwem łącza i czasu.
    CHECK(store.begin(2048).error() == Err::OutOfRange);
    CHECK(store.begin(0).error() == Err::BadArgument);
    CHECK(store.begin(512).has_value());
}

TEST("Magazyn: niekompletny obraz nie daje się domknąć") {
    mock::MockFirmwareStore store;
    REQUIRE(store.begin(100).has_value());

    const u8 chunk[40] = {};
    REQUIRE(store.write(CByteSpan{chunk, sizeof(chunk)}).has_value());

    // Przełączenie na obraz krótszy niż zapowiedziany kończy się urządzeniem,
    // które nie wstaje.
    CHECK(store.finish().error() == Err::Protocol);
    CHECK(!store.finished());
    CHECK(store.commit().error() == Err::NotInitialized);
}

TEST("Magazyn: przełączenie uruchamia tryb próbny") {
    mock::MockFirmwareStore store;
    const u8 image[64] = {};

    REQUIRE(store.begin(sizeof(image)).has_value());
    REQUIRE(store.write(CByteSpan{image, sizeof(image)}).has_value());
    REQUIRE(store.finish().has_value());
    CHECK(!store.pendingVerify());

    REQUIRE(store.commit().has_value());
    CHECK(store.committed());
    // Nowy obraz startuje warunkowo — dopóki nie potwierdzi sprawności.
    CHECK(store.pendingVerify());

    REQUIRE(store.markValid().has_value());
    CHECK(!store.pendingVerify());
    CHECK_EQ(static_cast<int>(store.validations()), 1);
}

// ---------------------------------------------------------------------------
// Aktualizacja
// ---------------------------------------------------------------------------

TEST("Aktualizacja: pełna droga od pobrania do przełączenia") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());
    CHECK(rig.updater.state() == OtaState::Idle);

    bool finished = false;
    u32  bytes    = 0;
    auto sub = EventBus::subscribe<OtaFinished>([&](const OtaFinished& e) {
        finished = e.success;
        bytes    = e.bytes;
    });
    REQUIRE(sub.has_value());

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware));

    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.runToCompletion(response);

    CHECK(rig.updater.state() == OtaState::Ready);
    CHECK_EQ(static_cast<int>(rig.updater.received()), static_cast<int>(sizeof(rig.firmware)));
    CHECK_EQ(static_cast<int>(rig.updater.percent()), 100);
    CHECK(finished);
    CHECK_EQ(static_cast<int>(bytes), static_cast<int>(sizeof(rig.firmware)));

    // Obraz w magazynie musi być tożsamy z wysłanym co do bajtu.
    CHECK_EQ(static_cast<int>(rig.store.image().size()), static_cast<int>(sizeof(rig.firmware)));
    CHECK_EQ(memcmp(rig.store.image().data(), rig.firmware, sizeof(rig.firmware)), 0);
    CHECK(rig.store.committed());
    CHECK(rig.store.pendingVerify());
}

TEST("Aktualizacja: żądanie zawiera ścieżkę i nagłówek Host") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());
    REQUIRE(rig.updater.start(rig.config(), 100).has_value());

    const CByteSpan sent = rig.client.sent();
    const char*     text = reinterpret_cast<const char*>(sent.data());
    CHECK(strstr(text, "GET /rover.bin HTTP/1.1") != nullptr);
    CHECK(strstr(text, "Host: aktualizacje.local") != nullptr);
    CHECK_STR(rig.client.lastHost(), "aktualizacje.local");
}

TEST("Aktualizacja: uszkodzony obraz nie zostaje przełączony") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    // Jeden przekłamany bajt — tyle wystarczy, by obraz był bezużyteczny.
    u8 corrupted[sizeof(rig.firmware)];
    memcpy(corrupted, rig.firmware, sizeof(corrupted));
    corrupted[500] ^= 0xFF;

    HttpResponse response;
    response.build(200, corrupted, sizeof(corrupted));

    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.runToCompletion(response);

    CHECK(rig.updater.state() == OtaState::Failed);
    CHECK(rig.updater.lastError() == Err::Protocol);
    // Kluczowe: urządzenie nadal działa na starym obrazie.
    CHECK(!rig.store.committed());
    CHECK(!rig.store.pendingVerify());
}

TEST("Aktualizacja: odpowiedź inna niż 200 nie trafia do pamięci programu") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    // Serwer zwracający 404 przysyła w ciele stronę błędu.
    const char* errorPage = "<html>nie znaleziono</html>";
    HttpResponse response;
    response.build(404, reinterpret_cast<const u8*>(errorPage), strlen(errorPage));

    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.runToCompletion(response);

    CHECK(rig.updater.state() == OtaState::Failed);
    CHECK(rig.updater.lastError() == Err::Protocol);
    CHECK_EQ(static_cast<int>(rig.store.written()), 0);
}

TEST("Aktualizacja: brak rozmiaru w odpowiedzi jest odrzucany") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware), false);

    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.runToCompletion(response);

    // Bez znanego rozmiaru nie da się sprawdzić, czy obraz się zmieści,
    // ani stwierdzić, że pobrano całość.
    CHECK(rig.updater.state() == OtaState::Failed);
}

TEST("Aktualizacja: obraz większy niż miejsce przerywa pobieranie") {
    resetOta();
    Rig rig;
    rig.store.setCapacity(256);
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware));

    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.runToCompletion(response);

    CHECK(rig.updater.state() == OtaState::Failed);
    CHECK(rig.updater.lastError() == Err::OutOfRange);
}

TEST("Aktualizacja: zerwane połączenie w połowie nie daje obrazu") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware));

    REQUIRE(rig.updater.start(rig.config(), 100).has_value());

    // Podajemy tylko nagłówki i część ciała, po czym zrywamy połączenie.
    rig.client.injectRx(CByteSpan{response.data, 300});
    rig.updater.step(110);
    rig.updater.step(120);
    CHECK(rig.updater.state() == OtaState::Downloading);

    rig.client.forceDisconnect();
    rig.updater.step(130);

    // Brakuje choćby jednego bajtu — to nie jest kompletna aktualizacja.
    CHECK(rig.updater.state() == OtaState::Failed);
    CHECK(!rig.store.committed());
}

TEST("Aktualizacja: milczenie serwera kończy się limitem czasu") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware));

    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.client.injectRx(CByteSpan{response.data, 300});

    // Dopóki dane napływają, licznik bezczynności się nie uruchamia — te dwa
    // kroki wyczerpują wstrzyknięty fragment.
    rig.updater.step(110);
    rig.updater.step(120);
    CHECK(rig.updater.state() == OtaState::Downloading);

    // Połączenie żyje, ale dane przestały płynąć — TCP wykryłby to dopiero
    // po wielu minutach.
    rig.updater.step(2000);
    CHECK(rig.updater.state() == OtaState::Failed);
    CHECK(rig.updater.lastError() == Err::Timeout);
}

TEST("Aktualizacja: błąd zapisu przerywa i nie zostawia połowicznego obrazu") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());
    rig.store.failNextWrite(Err::IoError);

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware));

    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.runToCompletion(response);

    CHECK(rig.updater.state() == OtaState::Failed);
    CHECK(rig.updater.lastError() == Err::IoError);
    CHECK_EQ(static_cast<int>(rig.store.written()), 0);  // zapis porzucony
}

TEST("Aktualizacja: postęp jest zgłaszany na magistralę") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    int         updates = 0;
    OtaProgress last{};
    auto sub = EventBus::subscribe<OtaProgress>([&](const OtaProgress& e) {
        ++updates;
        last = e;
    });
    REQUIRE(sub.has_value());

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware));
    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.runToCompletion(response);

    CHECK(updates >= 3);  // connecting, headers, downloading, verifying, ready
    CHECK_EQ(static_cast<int>(last.state), static_cast<int>(OtaState::Ready));
    CHECK_EQ(static_cast<int>(last.percent), 100);
}

// ---------------------------------------------------------------------------
// Tryb próbny i powrót
// ---------------------------------------------------------------------------

TEST("Tryb próbny: nowy obraz musi potwierdzić sprawność") {
    resetOta();
    Rig rig;
    // Stan po restarcie na świeżo wgranym obrazie.
    rig.store.setPendingVerify(true);

    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());
    CHECK(rig.updater.state() == OtaState::PendingConfirm);

    REQUIRE(rig.updater.confirmRunningImage().has_value());
    CHECK(rig.updater.state() == OtaState::Idle);
    CHECK(!rig.store.pendingVerify());
    CHECK_EQ(static_cast<int>(rig.store.validations()), 1);
}

TEST("Tryb próbny: brak potwierdzenia cofa aktualizację") {
    resetOta();
    Rig rig;
    rig.store.setPendingVerify(true);

    OtaUpdater::Config cfg = rig.config();
    cfg.confirmTimeoutMs   = 5000;

    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    int  reboots = 0;
    auto sub = EventBus::subscribe<RebootRequest>([&](const RebootRequest&) { ++reboots; });
    REQUIRE(sub.has_value());

    // Obraz wstał, ale nic nie potwierdziło, że działa poprawnie.
    rig.updater.step(1000);
    CHECK(rig.updater.state() == OtaState::PendingConfirm);

    // Bez tego mechanizmu pierwsza wersja, która wstaje i natychmiast się
    // wywraca, kończy wizytą z programatorem.
    rig.updater.step(120000);
    CHECK_EQ(static_cast<int>(rig.store.rollbacks()), 1);
    CHECK_EQ(reboots, 1);
    CHECK(!rig.store.pendingVerify());
}

TEST("Tryb próbny: aktualizacja w jego trakcie jest odrzucana") {
    resetOta();
    Rig rig;
    rig.store.setPendingVerify(true);
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    // Urządzenie ma wtedy jeden znany sprawny obraz — ten poprzedni.
    // Nadpisanie go zostawiłoby je bez żadnego.
    CHECK(rig.updater.start(rig.config(), 100).error() == Err::Busy);
}

TEST("Aktualizacja: druga próba w trakcie pobierania jest odrzucana") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());
    REQUIRE(rig.updater.start(rig.config(), 100).has_value());

    CHECK(rig.updater.start(rig.config(), 110).error() == Err::Busy);
}

TEST("Aktualizacja: przerwanie porzuca zapisany fragment") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware));
    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.client.injectRx(CByteSpan{response.data, 400});
    rig.updater.step(110);

    rig.updater.abort();
    CHECK(rig.updater.state() == OtaState::Failed);
    CHECK_EQ(static_cast<int>(rig.store.written()), 0);
    CHECK(!rig.store.committed());
}

TEST("Aktualizacja: błędne argumenty są odrzucane") {
    resetOta();
    Rig rig;

    OtaUpdater bare;
    // Bez podpiętego gniazda i magazynu nie ma czego uruchamiać.
    CHECK(bare.start(rig.config(), 0).error() == Err::NotInitialized);

    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());
    OtaUpdater::Config cfg = rig.config();
    cfg.host = nullptr;
    CHECK(rig.updater.start(cfg, 100).error() == Err::BadArgument);
}

TEST("Aktualizacja: statystyki liczą próby i wyniki") {
    resetOta();
    Rig rig;
    REQUIRE(rig.updater.begin(rig.client, rig.store, 0).has_value());

    HttpResponse response;
    response.build(200, rig.firmware, sizeof(rig.firmware));
    REQUIRE(rig.updater.start(rig.config(), 100).has_value());
    rig.runToCompletion(response);

    CHECK_EQ(static_cast<int>(rig.updater.stats().attempts), 1);
    CHECK_EQ(static_cast<int>(rig.updater.stats().successes), 1);
    CHECK_EQ(static_cast<int>(rig.updater.stats().failures), 0);
}
