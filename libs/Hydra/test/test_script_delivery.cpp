/**
 * Hydra — testy dostarczania skryptu przez sieć.
 *
 * Trzy warstwy osobno, a potem całość razem:
 *   1. base64 — bo bez niego obraz nie przejdzie przez JSON,
 *   2. magazyn obrazów — sloty, skrót, wycofanie; bez sieci i bez interpretera,
 *   3. rozszerzenie — pełna droga od ramki z łącza do podmienionego skryptu.
 *
 * Najważniejszy przypadek w tym pliku to „wersja psująca się w loop() zostaje
 * wycofana". Bez niego zdalna aktualizacja skryptu jest sposobem na zdalne
 * zepsucie urządzenia, a nie na jego naprawę.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/hal/HostFileSystem.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/script/ImageFile.hpp"
#include "hydra/script/ImageStore.hpp"
#include "hydra/script/LuaEngine.hpp"
#include "hydra/script/ScriptDelivery.hpp"
#include "hydra/util/Base64.hpp"

using namespace hydra;
using namespace hydra::script;

namespace {

// ---------------------------------------------------------------------------
// Zaplecze
// ---------------------------------------------------------------------------

/** Świeży atrapowy backend HAL — bindingi skryptu sięgają po GPIO. */
hal::mock::Backend& freshHal() {
    hal::mock::backend().clear();
    hal::mock::install();
    return hal::mock::backend();
}

/** Łącze zapisujące ostatnią nadaną ramkę i wstrzykujące przychodzące. */
class LoopbackLink : public minis::ILink {
public:
    const char* name() const override { return "test"; }
    Status begin() override { return ok(); }
    bool   up() const override { return true; }
    size_t mtu() const override { return 1024; }
    bool   isUplink() const override { return true; }

    Status send(const minis::Frame& frame) override {
        last = frame;
        const size_t n = frame.payload.size() < sizeof(text) - 1
                             ? frame.payload.size() : sizeof(text) - 1;
        memcpy(text, frame.payload.data(), n);
        text[n] = '\0';
        ++sent;
        return ok();
    }
    void poll(Millis) override {}

    void inject(minis::Frame frame) { deliver(frame); }

    minis::Frame last{};
    char         text[1024] = {};
    u32          sent = 0;
};

bool has(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != nullptr;
}

/** Skrót obrazu w zapisie szesnastkowym — tak, jak poda go serwer. */
void shaHexOf(const char* image, char* out, size_t cap) {
    u8 digest[util::kSha256Size] = {};
    // Razem z zerem kończącym: silnik tekstowy wymaga terminatora w obrazie.
    util::Sha256::hash(CByteSpan{reinterpret_cast<const u8*>(image), strlen(image) + 1},
                       digest);
    util::Sha256::toHex(digest, out, cap);
}

/**
 * Pełne stanowisko: moduł IoT z atrapowym łączem, moduł skryptowy na Lua,
 * magazyn na dwa sloty i rozszerzenie spinające to w całość.
 */
struct Rig {
    minis::MinisIotModule iot;
    LoopbackLink          link;
    LuaEngine             engine;
    ScriptModule          script;
    ImageStore            store;
    ScriptDelivery        delivery;

    alignas(8) u8 slotA[2048] = {};
    alignas(8) u8 slotB[2048] = {};

    u32 nextId = 1;
    u32 trialMs_ = 10000;
    ImageFile*  persist_ = nullptr;
    const char* key_     = nullptr;

    /** Włącza trwałość między restartami. Wołać przed `bringUp()`. */
    void persistTo(ImageFile& file) {
        persist_ = &file;
        reconfigure();
    }

    /** Włącza podpisywanie obrazów tym kluczem. Wołać przed `bringUp()`. */
    void requireSignature(const char* key) {
        key_ = key;
        reconfigure();
    }

    void reconfigure() {
        ScriptDelivery::Config dcfg{};
        dcfg.minis   = &iot;
        dcfg.script  = &script;
        dcfg.store   = &store;
        dcfg.persist = persist_;
        dcfg.trialMs = trialMs_;
        if (key_ != nullptr) dcfg.hmacKey.set(key_);
        delivery.configure(dcfg);
    }

    explicit Rig(const char* builtin, u32 trialMs = 10000) {
        trialMs_ = trialMs;
        freshHal();
        EventBus::reset();
        (void)EventBus::init();

        minis::MinisIotModule::Config icfg;
        icfg.self         = minis::DeviceAddr::of("user1", "dev-iot1");
        icfg.autoRegister = false;
        iot.configure(icfg);
        iot.addLink(link);

        ScriptModule::Config scfg{};
        scfg.engine               = &engine;
        scfg.source               = builtin;
        scfg.maxConsecutiveErrors = 3;
        script.configure(scfg);

        ImageStore::Config mcfg{};
        mcfg.slotA = ByteSpan{slotA, sizeof(slotA)};
        mcfg.slotB = ByteSpan{slotB, sizeof(slotB)};
        store.configure(mcfg);

        ScriptDelivery::Config dcfg{};
        dcfg.minis   = &iot;
        dcfg.script  = &script;
        dcfg.store   = &store;
        dcfg.trialMs = trialMs;
        delivery.configure(dcfg);
    }

    ~Rig() {
        script.stop();
        EventBus::reset();
    }

    /** Kolejność jest istotna: magazyn przejmuje obraz wczytany przez moduł. */
    Status bringUp() {
        HYDRA_CHECK(script.init());
        return delivery.init();
    }

    /** Wysyła żądanie do rozszerzenia tak, jak zrobiłby to serwer. */
    void request(const char* op, const char* paramsJson = "{}") {
        char payload[1400];
        snprintf(payload, sizeof(payload), "{\"id\":\"r%u\",\"op\":\"%s\",\"params\":%s}",
                 nextId++, op, paramsJson);

        minis::Frame f;
        f.addr.set("user1", "dev-iot1");
        f.kind    = minis::MsgKind::ExtRequest;
        f.payload = CByteSpan{reinterpret_cast<const u8*>(payload), strlen(payload)};
        strcpy(f.extType, "script");
        link.inject(f);
    }

    /** Cały obraz jednym `chunk` — mieści się w buforze fragmentu. */
    void upload(const char* image, const char* name = "=nowy") {
        char hex[2 * util::kSha256Size + 1];
        shaHexOf(image, hex, sizeof(hex));

        const size_t bytes = strlen(image) + 1;

        char params[256];
        snprintf(params, sizeof(params), "{\"size\":%u,\"sha256\":\"%s\",\"name\":\"%s\"}",
                 static_cast<unsigned>(bytes), hex, name);
        request("begin", params);

        char b64[1200];
        auto encoded = util::base64Encode(
            CByteSpan{reinterpret_cast<const u8*>(image), bytes}, b64, sizeof(b64));
        REQUIRE(encoded.has_value());

        char chunkParams[1300];
        snprintf(chunkParams, sizeof(chunkParams), "{\"seq\":0,\"data\":\"%s\"}", b64);
        request("chunk", chunkParams);
    }

};

}  // namespace

// ---------------------------------------------------------------------------
// base64
// ---------------------------------------------------------------------------

TEST("base64: koduje i dekoduje z powrotem to samo") {
    const u8 data[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0xFF, 0x80};

    char out[32] = {};
    auto encoded = util::base64Encode(CByteSpan{data, sizeof(data)}, out, sizeof(out));
    REQUIRE(encoded.has_value());
    CHECK_STR(out, "AGFzbQEAAAD/gA==");

    u8   back[16] = {};
    auto decoded = util::base64Decode(out, encoded.value(), ByteSpan{back, sizeof(back)});
    REQUIRE(decoded.has_value());
    CHECK_EQ(static_cast<int>(decoded.value()), static_cast<int>(sizeof(data)));
    CHECK(memcmp(back, data, sizeof(data)) == 0);
}

TEST("base64: nagłówek modułu WASM przeżywa zero w pierwszym bajcie") {
    // To jest powód istnienia tego kodowania: `00 61 73 6D` w napisie C
    // kończyłoby się na pierwszym bajcie.
    const u8 magic[] = {0x00, 0x61, 0x73, 0x6D};
    char     out[16] = {};
    REQUIRE(util::base64Encode(CByteSpan{magic, sizeof(magic)}, out, sizeof(out)).has_value());

    u8   back[8] = {};
    auto decoded = util::base64Decode(out, strlen(out), ByteSpan{back, sizeof(back)});
    REQUIRE(decoded.has_value());
    CHECK_EQ(static_cast<int>(decoded.value()), 4);
    CHECK_EQ(static_cast<int>(back[0]), 0);
    CHECK_EQ(static_cast<int>(back[3]), 0x6D);
}

TEST("base64: znak spoza alfabetu jest odrzucany, a nie pomijany") {
    u8 back[8] = {};
    CHECK(!util::base64Decode("AA A=", 5, ByteSpan{back, sizeof(back)}).has_value());
    CHECK(!util::base64Decode("A@==", 4, ByteSpan{back, sizeof(back)}).has_value());
    // Długość niepodzielna przez cztery: nie da się odtworzyć liczby bajtów.
    CHECK(!util::base64Decode("AAA", 3, ByteSpan{back, sizeof(back)}).has_value());
    // Dopełnienie w środku to dwa sklejone dokumenty, nie jeden obraz.
    CHECK(!util::base64Decode("A=AAAAAA", 8, ByteSpan{back, sizeof(back)}).has_value());
}

TEST("base64: zbyt mały bufor wyjściowy jest zgłaszany, a nie obcinany") {
    const u8 data[] = {1, 2, 3, 4, 5, 6};
    char     small[4] = {};
    CHECK(!util::base64Encode(CByteSpan{data, sizeof(data)}, small, sizeof(small)).has_value());

    u8 tiny[2] = {};
    CHECK(!util::base64Decode("AQIDBAUG", 8, ByteSpan{tiny, sizeof(tiny)}).has_value());
}

// ---------------------------------------------------------------------------
// Magazyn obrazów
// ---------------------------------------------------------------------------

namespace {

struct StoreRig {
    alignas(8) u8 a[256] = {};
    alignas(8) u8 b[256] = {};
    ImageStore    store;

    StoreRig() {
        ImageStore::Config cfg{};
        cfg.slotA = ByteSpan{a, sizeof(a)};
        cfg.slotB = ByteSpan{b, sizeof(b)};
        store.configure(cfg);
    }

    /** Wgrywa obraz w dwóch kawałkach i domyka. Zwraca, czy skrót się zgodził. */
    bool put(const char* text) {
        const size_t n = strlen(text);
        u8 sha[util::kSha256Size] = {};
        util::Sha256::hash(CByteSpan{reinterpret_cast<const u8*>(text), n}, sha);

        if (!store.beginTransfer(n, sha)) return false;
        const size_t half = n / 2;
        if (!store.appendChunk(0, CByteSpan{reinterpret_cast<const u8*>(text), half})) return false;
        if (!store.appendChunk(1, CByteSpan{reinterpret_cast<const u8*>(text) + half, n - half})) return false;
        return store.verifyStaged().has_value();
    }
};

}  // namespace

TEST("ImageStore: obraz zebrany z fragmentów przechodzi weryfikację") {
    StoreRig rig;
    CHECK(rig.put("wersja pierwsza"));
    CHECK(rig.store.staged());

    auto image = rig.store.activateStaged();
    REQUIRE(image.has_value());
    CHECK_EQ(static_cast<int>(image.value().bytes), 15);
    CHECK(memcmp(image.value().data, "wersja pierwsza", 15) == 0);
}

TEST("ImageStore: obraz z niezgodnym skrótem nie staje się aktywny") {
    StoreRig rig;
    const char* text = "tresc";
    u8 wrong[util::kSha256Size] = {};
    memset(wrong, 0xAB, sizeof(wrong));

    REQUIRE(rig.store.beginTransfer(strlen(text), wrong).has_value());
    REQUIRE(rig.store.appendChunk(0, CByteSpan{reinterpret_cast<const u8*>(text),
                                               strlen(text)}).has_value());

    CHECK(!rig.store.verifyStaged().has_value());
    CHECK(!rig.store.staged());
    // Nieudana weryfikacja zwalnia slot — inaczej jedna zepsuta paczka
    // blokowałaby kanał do restartu.
    CHECK(!rig.store.receiving());
    CHECK_EQ(static_cast<int>(rig.store.stats().rejects), 1);
    CHECK(!rig.store.activateStaged().has_value());
}

TEST("ImageStore: fragment poza kolejnością jest odrzucany") {
    StoreRig rig;
    u8 sha[util::kSha256Size] = {};
    REQUIRE(rig.store.beginTransfer(10, sha).has_value());

    const u8 bytes[5] = {1, 2, 3, 4, 5};
    REQUIRE(rig.store.appendChunk(0, CByteSpan{bytes, 5}).has_value());
    // Przeskok numeru to zgubiona paczka. Sklejanie dziurawego obrazu nie ma
    // sensu — i tak nie przeszedłby weryfikacji, tylko później.
    CHECK(!rig.store.appendChunk(2, CByteSpan{bytes, 5}).has_value());
    CHECK(!rig.store.appendChunk(0, CByteSpan{bytes, 5}).has_value());
}

TEST("ImageStore: obraz większy niż slot jest odrzucany przed transferem") {
    StoreRig rig;
    u8 sha[util::kSha256Size] = {};
    CHECK(!rig.store.beginTransfer(rig.store.capacity() + 1, sha).has_value());
    CHECK(!rig.store.receiving());
}

TEST("ImageStore: drugi transfer czeka, aż pierwszy się potwierdzi") {
    StoreRig rig;
    rig.store.adoptBuiltin(CByteSpan{reinterpret_cast<const u8*>("wbudowany"), 9});

    REQUIRE(rig.put("wersja druga"));
    REQUIRE(rig.store.activateStaged().has_value());

    // Aktywny zajmuje slot 0, poprzedni jest obrazem wbudowanym (bez slotu),
    // więc miejsce jeszcze jest.
    CHECK(rig.store.canRollback());
    REQUIRE(rig.put("wersja trzecia"));
    REQUIRE(rig.store.activateStaged().has_value());

    // Teraz oba sloty trzyma para aktywny/poprzedni — kolejny transfer musi
    // poczekać, bo inaczej wyparłby jedyną wersję, o której wiadomo, że działa.
    u8 sha[util::kSha256Size] = {};
    auto blocked = rig.store.beginTransfer(8, sha);
    CHECK(!blocked.has_value());
    CHECK_EQ(static_cast<int>(blocked.error()), static_cast<int>(Err::Busy));

    rig.store.confirm();
    CHECK(!rig.store.canRollback());
    CHECK(rig.store.beginTransfer(8, sha).has_value());
}

TEST("ImageStore: wycofanie wraca do poprzedniego obrazu") {
    StoreRig rig;
    rig.store.adoptBuiltin(CByteSpan{reinterpret_cast<const u8*>("wbudowany"), 9});

    REQUIRE(rig.put("nowa wersja"));
    REQUIRE(rig.store.activateStaged().has_value());
    CHECK(memcmp(rig.store.active().data, "nowa wersja", 11) == 0);

    auto restored = rig.store.rollback();
    REQUIRE(restored.has_value());
    CHECK(memcmp(restored.value().data, "wbudowany", 9) == 0);
    CHECK(!rig.store.canRollback());
    CHECK_EQ(static_cast<int>(rig.store.stats().rollbacks), 1);
}

// ---------------------------------------------------------------------------
// Rozszerzenie — pełna droga
// ---------------------------------------------------------------------------

TEST("Dostarczanie: skrypt wgrany przez łącze podmienia się bez restartu") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kNowy    = "wersja = 2\nfunction loop() end\n";

    Rig rig{kBuiltin};
    REQUIRE(rig.bringUp().has_value());

    rig.upload(kNowy, "=v2");
    rig.request("commit");

    CHECK(has(rig.link.text, "\"ok\":true"));
    CHECK(has(rig.link.text, "\"trial\":true"));
    CHECK(rig.delivery.inTrial());
    CHECK_EQ(static_cast<int>(rig.delivery.stats().commits), 1);

    // Skutek: w interpreterze siedzi już nowa wersja skryptu.
    CHECK(rig.engine.interp().doString("if wersja ~= 2 then error('stara') end",
                                       "=check").has_value());
}

TEST("Dostarczanie: obraz z niezgodnym skrótem nie dociera do interpretera") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kNowy    = "wersja = 2\nfunction loop() end\n";

    Rig rig{kBuiltin};
    REQUIRE(rig.bringUp().has_value());

    // Zapowiadamy skrót obrazu, którego nie wyślemy.
    char hex[2 * util::kSha256Size + 1];
    shaHexOf("zupelnie co innego", hex, sizeof(hex));

    char params[256];
    snprintf(params, sizeof(params), "{\"size\":%u,\"sha256\":\"%s\"}",
             static_cast<unsigned>(strlen(kNowy) + 1), hex);
    rig.request("begin", params);

    char b64[1200];
    REQUIRE(util::base64Encode(
        CByteSpan{reinterpret_cast<const u8*>(kNowy), strlen(kNowy) + 1},
        b64, sizeof(b64)).has_value());

    char chunkParams[1300];
    snprintf(chunkParams, sizeof(chunkParams), "{\"seq\":0,\"data\":\"%s\"}", b64);
    rig.request("chunk", chunkParams);

    rig.request("commit");

    CHECK(has(rig.link.text, "\"ok\":false"));
    CHECK(has(rig.link.text, "\"code\":\"checksum\""));
    CHECK(!rig.delivery.inTrial());

    // Stary skrypt działa dalej.
    CHECK(rig.engine.interp().doString("if wersja ~= 1 then error('podmieniony') end",
                                       "=check").has_value());
}

TEST("Dostarczanie: wersja psujaca sie w loop() jest wycofana") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kZly     = "wersja = 2\nfunction loop() error('padam') end\n";

    Rig rig{kBuiltin, /*trialMs=*/1000};
    REQUIRE(rig.bringUp().has_value());

    rig.upload(kZly, "=zly");
    rig.request("commit");
    REQUIRE(rig.delivery.inTrial());

    // Trzy błędy z rzędu wyłączają `loop()` — tyle ustawia maxConsecutiveErrors.
    for (int i = 0; i < 5; ++i) rig.script.step();
    CHECK(rig.script.loopStopped());

    // Obserwator zauważa to w najbliższym przebiegu, nie czekając na koniec
    // okresu próbnego: nie ma już czego obserwować.
    rig.delivery.step(App::uptimeMs());

    CHECK(!rig.delivery.inTrial());
    CHECK_EQ(static_cast<int>(rig.delivery.stats().rollbacks), 1);
    CHECK(rig.engine.interp().doString("if wersja ~= 1 then error('nie wycofano') end",
                                       "=check").has_value());
}

TEST("Dostarczanie: wersja, ktora przetrwala okres probny, zostaje potwierdzona") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kDobry   = "wersja = 2\nfunction loop() end\n";

    Rig rig{kBuiltin, /*trialMs=*/1000};
    REQUIRE(rig.bringUp().has_value());

    rig.upload(kDobry);
    rig.request("commit");
    REQUIRE(rig.delivery.inTrial());
    CHECK(rig.store.canRollback());

    for (int i = 0; i < 5; ++i) rig.script.step();

    // Przed upływem okresu nic się nie zmienia.
    rig.delivery.step(App::uptimeMs());
    CHECK(rig.delivery.inTrial());

    rig.delivery.step(App::uptimeMs() + 1001);
    CHECK(!rig.delivery.inTrial());
    CHECK_EQ(static_cast<int>(rig.delivery.stats().confirms), 1);
    // Po potwierdzeniu slot poprzedniej wersji wraca do puli.
    CHECK(!rig.store.canRollback());
}

TEST("Dostarczanie: skrypt z bledem skladni jest cofany od razu, bez okresu probnego") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kZepsuty = "function loop( -- brak domkniecia\n";

    Rig rig{kBuiltin};
    REQUIRE(rig.bringUp().has_value());

    rig.upload(kZepsuty, "=zepsuty");
    rig.request("commit");

    CHECK(has(rig.link.text, "\"ok\":false"));
    CHECK(has(rig.link.text, "\"code\":\"load_failed\""));
    CHECK(!rig.delivery.inTrial());
    CHECK_EQ(static_cast<int>(rig.delivery.stats().rollbacks), 1);
    CHECK(rig.engine.interp().doString("if wersja ~= 1 then error('nie wycofano') end",
                                       "=check").has_value());
}

TEST("Dostarczanie: status podaje pojemnosc i stan okresu probnego") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";

    Rig rig{kBuiltin};
    REQUIRE(rig.bringUp().has_value());

    rig.request("status");
    CHECK(has(rig.link.text, "\"ok\":true"));
    CHECK(has(rig.link.text, "\"trial\":false"));
    CHECK(has(rig.link.text, "\"capacity\":2048"));
    CHECK(has(rig.link.text, "\"receiving\":false"));
}

TEST("Dostarczanie: nieznana operacja dostaje odpowiedz z bledem") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";

    Rig rig{kBuiltin};
    REQUIRE(rig.bringUp().has_value());

    rig.request("wyslij_wszystko");
    CHECK(has(rig.link.text, "\"ok\":false"));
    CHECK(has(rig.link.text, "\"code\":\"unsupported\""));
}

// ---------------------------------------------------------------------------
// Wariant obrazu — przygotowanie pod kompilację z wyprzedzeniem
// ---------------------------------------------------------------------------

TEST("Dostarczanie: obraz w obcym wariancie jest odrzucany przed transferem") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";

    Rig rig{kBuiltin};
    REQUIRE(rig.bringUp().has_value());

    // Kod skompilowany z wyprzedzeniem dla Xtensy nie jest „gorszy" dla
    // interpretera Lua — jest niewykonywalny. Odmowa musi paść przed
    // przesłaniem kilkudziesięciu kilobajtów, a nie na `commit`.
    rig.request("begin",
                "{\"size\":32,\"variant\":\"aot:xtensa-esp32s3\","
                "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}");

    CHECK(has(rig.link.text, "\"ok\":false"));
    CHECK(has(rig.link.text, "\"code\":\"variant\""));
    CHECK(!rig.store.receiving());
}

TEST("Dostarczanie: wariant zrodlowy przechodzi u silnika Lua") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kNowy    = "wersja = 2\nfunction loop() end\n";

    Rig rig{kBuiltin};
    REQUIRE(rig.bringUp().has_value());

    char hex[2 * util::kSha256Size + 1];
    shaHexOf(kNowy, hex, sizeof(hex));

    char params[256];
    snprintf(params, sizeof(params), "{\"size\":%u,\"variant\":\"src\",\"sha256\":\"%s\"}",
             static_cast<unsigned>(strlen(kNowy) + 1), hex);
    rig.request("begin", params);

    CHECK(has(rig.link.text, "\"ok\":true"));
    CHECK(rig.store.receiving());
}

TEST("Dostarczanie: status podaje silnik, zeby serwer wiedzial co przyslac") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";

    Rig rig{kBuiltin};
    REQUIRE(rig.bringUp().has_value());

    rig.request("status");
    CHECK(has(rig.link.text, "\"engine\":\"lua\""));
}

// ---------------------------------------------------------------------------
// Trwałość między restartami
// ---------------------------------------------------------------------------

namespace {

/** Katalog roboczy testów trwałości. */
constexpr const char* kPersistRoot = "build/script-img-test";

/** Świeży, pusty system plików hosta. */
struct FsFixture {
    hal::HostFileSystem fs{kPersistRoot};
    FsFixture() {
        (void)fs.mount();
        (void)fs.format();
    }
    ~FsFixture() { fs.unmount(); }
};

CByteSpan bytesOf(const char* s) {
    return CByteSpan{reinterpret_cast<const u8*>(s), strlen(s) + 1};
}

}  // namespace

TEST("ImageFile: obraz przezywa zapis i odczyt") {
    FsFixture fsx;

    ImageFile file;
    ImageFile::Config cfg{};
    cfg.fs   = &fsx.fs;
    cfg.path = "/script.img";
    REQUIRE(file.configure(cfg).has_value());
    CHECK(file.enabled());

    static const char* kImage = "wersja = 7\nfunction loop() end\n";
    REQUIRE(file.save(bytesOf(kImage)).has_value());

    u8   buffer[256] = {};
    auto loaded = file.load(ByteSpan{buffer, sizeof(buffer)});
    REQUIRE(loaded.has_value());
    CHECK_EQ(static_cast<int>(loaded.value()), static_cast<int>(strlen(kImage) + 1));
    CHECK_STR(reinterpret_cast<const char*>(buffer), kImage);
}

TEST("ImageFile: brak pliku to NotFound, a nie awaria") {
    FsFixture fsx;

    ImageFile file;
    ImageFile::Config cfg{};
    cfg.fs = &fsx.fs;
    REQUIRE(file.configure(cfg).has_value());

    u8   buffer[64] = {};
    auto loaded = file.load(ByteSpan{buffer, sizeof(buffer)});
    CHECK(!loaded);
    CHECK_EQ(loaded.error(), Err::NotFound);
}

TEST("ImageFile: uszkodzony obraz jest odrzucany i kasowany") {
    FsFixture fsx;

    ImageFile file;
    ImageFile::Config cfg{};
    cfg.fs   = &fsx.fs;
    cfg.path = "/script.img";
    REQUIRE(file.configure(cfg).has_value());
    REQUIRE(file.save(bytesOf("tresc oryginalna")).has_value());

    // Podmieniamy jeden bajt ładunku — skrót przestaje się zgadzać.
    {
        auto handle = fsx.fs.open("/script.img", hal::OpenMode::Read);
        REQUIRE(handle.has_value());
        u8 whole[256] = {};
        auto n = handle.value()->read(ByteSpan{whole, sizeof(whole)});
        (void)handle.value()->close();
        REQUIRE(n.has_value());

        whole[ImageFile::kHeaderSize] ^= 0xFF;

        auto out = fsx.fs.open("/script.img", hal::OpenMode::Write);
        REQUIRE(out.has_value());
        (void)out.value()->write(CByteSpan{whole, n.value()});
        (void)out.value()->close();
    }

    u8   buffer[256] = {};
    auto loaded = file.load(ByteSpan{buffer, sizeof(buffer)});
    CHECK(!loaded);
    // Skasowany, żeby nie wracać do niego przy każdym rozruchu — pętla
    // restartów byłaby gorsza od utraty ostatniej wersji.
    CHECK(!fsx.fs.exists("/script.img"));
}

TEST("ImageFile: obraz wiekszy niz bufor jest zglaszany, nie obcinany") {
    FsFixture fsx;

    ImageFile file;
    ImageFile::Config cfg{};
    cfg.fs = &fsx.fs;
    REQUIRE(file.configure(cfg).has_value());
    REQUIRE(file.save(bytesOf("dosc dlugi obraz skryptu")).has_value());

    u8   tiny[4] = {};
    auto loaded = file.load(ByteSpan{tiny, sizeof(tiny)});
    CHECK(!loaded);
    CHECK_EQ(loaded.error(), Err::OutOfRange);
}

// ---------------------------------------------------------------------------
// Podpis — autentyczność, nie tylko spójność
// ---------------------------------------------------------------------------

namespace {

/** Podpis obrazu w zapisie szesnastkowym, tak jak policzy go serwer. */
void hmacHexOf(const char* key, const char* image, char* out, size_t cap) {
    u8 mac[util::kSha256Size] = {};
    util::HmacSha256::compute(
        CByteSpan{reinterpret_cast<const u8*>(key), strlen(key)},
        CByteSpan{reinterpret_cast<const u8*>(image), strlen(image) + 1},
        mac);
    util::Sha256::toHex(mac, out, cap);
}

}  // namespace

TEST("Podpis: obraz bez podpisu jest odrzucany, gdy urzadzenie ma klucz") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kNowy    = "wersja = 2\nfunction loop() end\n";

    Rig rig{kBuiltin};
    rig.requireSignature("tajny-klucz");
    REQUIRE(rig.bringUp().has_value());

    char hex[2 * util::kSha256Size + 1];
    shaHexOf(kNowy, hex, sizeof(hex));

    char params[256];
    snprintf(params, sizeof(params), "{\"size\":%u,\"sha256\":\"%s\"}",
             static_cast<unsigned>(strlen(kNowy) + 1), hex);
    rig.request("begin", params);

    // Sam skrót mówi tylko, że obraz nie uległ uszkodzeniu w drodze.
    // Napastnik policzy go równie dobrze — dlatego odmowa pada od razu.
    CHECK(has(rig.link.text, "\"ok\":false"));
    CHECK(has(rig.link.text, "\"code\":\"unsigned\""));
    CHECK(!rig.store.receiving());
}

TEST("Podpis: obraz z cudzym podpisem nie dociera do interpretera") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kNowy    = "wersja = 2\nfunction loop() end\n";

    Rig rig{kBuiltin};
    rig.requireSignature("tajny-klucz");
    REQUIRE(rig.bringUp().has_value());

    char sha[2 * util::kSha256Size + 1];
    shaHexOf(kNowy, sha, sizeof(sha));
    char mac[2 * util::kSha256Size + 1];
    hmacHexOf("nie-ten-klucz", kNowy, mac, sizeof(mac));

    char params[400];
    snprintf(params, sizeof(params),
             "{\"size\":%u,\"sha256\":\"%s\",\"hmac\":\"%s\"}",
             static_cast<unsigned>(strlen(kNowy) + 1), sha, mac);
    rig.request("begin", params);
    CHECK(has(rig.link.text, "\"ok\":true"));   // podpis sprawdzamy przy commit

    char b64[1200];
    REQUIRE(util::base64Encode(
        CByteSpan{reinterpret_cast<const u8*>(kNowy), strlen(kNowy) + 1},
        b64, sizeof(b64)).has_value());
    char chunkParams[1300];
    snprintf(chunkParams, sizeof(chunkParams), "{\"seq\":0,\"data\":\"%s\"}", b64);
    rig.request("chunk", chunkParams);

    rig.request("commit");

    CHECK(has(rig.link.text, "\"ok\":false"));
    CHECK(has(rig.link.text, "\"code\":\"signature\""));
    // Stary skrypt działa dalej.
    CHECK(rig.engine.interp().doString("if wersja ~= 1 then error('podmieniony') end",
                                       "=check").has_value());
}

TEST("Podpis: obraz podpisany wlasciwym kluczem przechodzi") {
    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kNowy    = "wersja = 2\nfunction loop() end\n";

    Rig rig{kBuiltin};
    rig.requireSignature("tajny-klucz");
    REQUIRE(rig.bringUp().has_value());

    char sha[2 * util::kSha256Size + 1];
    shaHexOf(kNowy, sha, sizeof(sha));
    char mac[2 * util::kSha256Size + 1];
    hmacHexOf("tajny-klucz", kNowy, mac, sizeof(mac));

    char params[400];
    snprintf(params, sizeof(params),
             "{\"size\":%u,\"sha256\":\"%s\",\"hmac\":\"%s\"}",
             static_cast<unsigned>(strlen(kNowy) + 1), sha, mac);
    rig.request("begin", params);

    char b64[1200];
    REQUIRE(util::base64Encode(
        CByteSpan{reinterpret_cast<const u8*>(kNowy), strlen(kNowy) + 1},
        b64, sizeof(b64)).has_value());
    char chunkParams[1300];
    snprintf(chunkParams, sizeof(chunkParams), "{\"seq\":0,\"data\":\"%s\"}", b64);
    rig.request("chunk", chunkParams);

    rig.request("commit");

    CHECK(has(rig.link.text, "\"ok\":true"));
    CHECK(rig.delivery.inTrial());
    CHECK(rig.engine.interp().doString("if wersja ~= 2 then error('stara') end",
                                       "=check").has_value());
}

TEST("Trwalosc: potwierdzony obraz wraca po restarcie") {
    FsFixture fsx;
    ImageFile file;
    ImageFile::Config fcfg{};
    fcfg.fs   = &fsx.fs;
    fcfg.path = "/script.img";
    REQUIRE(file.configure(fcfg).has_value());

    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kNowy    = "wersja = 2\nfunction loop() end\n";

    // --- pierwsze uruchomienie: wgranie i potwierdzenie -------------------
    {
        Rig rig{kBuiltin, /*trialMs=*/1000};
        rig.persistTo(file);
        REQUIRE(rig.bringUp().has_value());

        rig.upload(kNowy, "=v2");
        rig.request("commit");
        REQUIRE(rig.delivery.inTrial());

        // Do pamięci trwałej trafia dopiero wersja, która przetrwała próbę.
        CHECK(!fsx.fs.exists("/script.img"));

        rig.delivery.step(App::uptimeMs() + 1001);
        CHECK(!rig.delivery.inTrial());
        CHECK(fsx.fs.exists("/script.img"));
    }

    // --- restart: nowe obiekty, ten sam system plików ---------------------
    {
        Rig rig{kBuiltin};
        rig.persistTo(file);
        REQUIRE(rig.bringUp().has_value());

        // Urządzenie wstaje z obrazem z sieci, a nie z tym wkompilowanym
        // w firmware — o to w całej trwałości chodzi.
        CHECK(rig.engine.interp().doString("if wersja ~= 2 then error('wrocil wbudowany') end",
                                           "=check").has_value());
        CHECK(rig.store.canRollback());
    }
}

TEST("Trwalosc: obraz w okresie probnym nie przezywa restartu") {
    FsFixture fsx;
    ImageFile file;
    ImageFile::Config fcfg{};
    fcfg.fs   = &fsx.fs;
    fcfg.path = "/script.img";
    REQUIRE(file.configure(fcfg).has_value());

    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kZly     = "wersja = 2\nfunction loop() error('padam') end\n";

    {
        Rig rig{kBuiltin, /*trialMs=*/1000};
        rig.persistTo(file);
        REQUIRE(rig.bringUp().has_value());

        rig.upload(kZly, "=zly");
        rig.request("commit");
        REQUIRE(rig.delivery.inTrial());
        // Restart w środku okresu próbnego — nic nie zdążyło się zapisać.
    }

    {
        Rig rig{kBuiltin};
        rig.persistTo(file);
        REQUIRE(rig.bringUp().has_value());

        // To jest sedno: gdyby zapis szedł przy `commit`, urządzenie wstawałoby
        // w kółko z wersją, która się wywraca. Zapis po potwierdzeniu znaczy,
        // że restart zawsze wraca do czegoś sprawdzonego.
        CHECK(rig.engine.interp().doString("if wersja ~= 1 then error('wrocil zly') end",
                                           "=check").has_value());
    }
}

TEST("Trwalosc: wycofanie do wbudowanego kasuje zapisany obraz") {
    FsFixture fsx;
    ImageFile file;
    ImageFile::Config fcfg{};
    fcfg.fs   = &fsx.fs;
    fcfg.path = "/script.img";
    REQUIRE(file.configure(fcfg).has_value());

    static const char* kBuiltin = "wersja = 1\nfunction loop() end\n";
    static const char* kZly     = "wersja = 2\nfunction loop() error('padam') end\n";

    Rig rig{kBuiltin, /*trialMs=*/1000};
    rig.persistTo(file);
    REQUIRE(rig.bringUp().has_value());

    rig.upload(kZly, "=zly");
    rig.request("commit");
    REQUIRE(rig.delivery.inTrial());

    for (int i = 0; i < 5; ++i) rig.script.step();
    rig.delivery.step(App::uptimeMs());

    CHECK_EQ(static_cast<int>(rig.delivery.stats().rollbacks), 1);
    // Wróciliśmy do obrazu wbudowanego, który jest w firmware — plik po nim
    // byłby tylko sposobem na odtworzenie po restarcie wersji, od której
    // właśnie odeszliśmy.
    CHECK(!fsx.fs.exists("/script.img"));
}
