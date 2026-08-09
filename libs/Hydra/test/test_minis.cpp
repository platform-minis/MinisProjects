/**
 * Testy modułu IoT MyCastle.
 *
 * Cztery warstwy, cztery grupy przypadków: JSON, ramkowanie szeregowe,
 * trasowanie i protokół. Ostatnia grupa jest tu najważniejsza — sprawdza
 * bajt w bajt to, co zobaczy serwer, bo rozjazd z MinisLib objawiłby się
 * dopiero jako urządzenie, którego panel nie umie narysować.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/minis/MinisModule.hpp"
#include "hydra/minis/SerialCodec.hpp"
#include "hydra/util/Json.hpp"

using namespace hydra;
using namespace hydra::minis;

namespace {

/** Czy w tekście występuje podciąg — do sprawdzania złożonego JSON-a. */
bool has(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != nullptr;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

TEST("JSON: zapis obiektu z zagnieżdżoną tablicą") {
    u8 buf[256];
    json::JsonWriter out{ByteSpan{buf, sizeof(buf)}};
    out.beginObject();
    out.key("id").value("abc");
    out.key("ok").value(true);
    out.key("metrics").beginArray();
    out.beginObject().key("key").value("temp").key("value").value(21.5f).endObject();
    out.beginObject().key("key").value("motion").key("value").value(false).endObject();
    out.endArray();
    out.endObject();

    CHECK(out.ok());
    CHECK_STR(out.text(),
              "{\"id\":\"abc\",\"ok\":true,\"metrics\":"
              "[{\"key\":\"temp\",\"value\":21.5},{\"key\":\"motion\",\"value\":false}]}");
}

TEST("JSON: przepełnienie bufora jest widoczne, a nie ciche") {
    // Obcięty dokument serwer odrzuca w całości. Gdyby pisarz milczał,
    // objawem byłaby telemetria, która „czasem nie dochodzi".
    u8 buf[16];
    json::JsonWriter out{ByteSpan{buf, sizeof(buf)}};
    out.beginObject().key("bardzo-dluga-nazwa").value("i-dluga-wartosc").endObject();

    CHECK(!out.ok());
    CHECK(out.needed() > sizeof(buf));
}

TEST("JSON: ucieczki i znaki sterujące") {
    u8 buf[128];
    json::JsonWriter out{ByteSpan{buf, sizeof(buf)}};
    out.beginObject().key("t").value("a\"b\\c\nd\x01").endObject();

    CHECK(out.ok());
    CHECK_STR(out.text(), "{\"t\":\"a\\\"b\\\\c\\nd\\u0001\"}");
}

TEST("JSON: NaN wychodzi jako null zamiast psuć cały dokument") {
    u8 buf[64];
    json::JsonWriter out{ByteSpan{buf, sizeof(buf)}};
    out.beginObject().key("v").value(0.0f / 0.0f).key("ok").value(true).endObject();

    CHECK(out.ok());
    CHECK_STR(out.text(), "{\"v\":null,\"ok\":true}");
}

TEST("JSON: odczyt pól i typów") {
    const char* text =
        "{\"id\":\"cmd-1\",\"name\":\"relay\",\"n\":-42,\"f\":3.5,"
        "\"b\":true,\"payload\":{\"value\":true},\"arr\":[1,2,3]}";
    json::JsonView doc{text};

    char id[16] = {};
    CHECK(doc.get("id").asString(id, sizeof(id)));
    CHECK_STR(id, "cmd-1");

    i32 n = 0;
    CHECK(doc.get("n").asInt(n));
    CHECK_EQ(n, -42);

    float f = 0.0f;
    CHECK(doc.get("f").asFloat(f));
    CHECK(f > 3.4f && f < 3.6f);

    bool b = false;
    CHECK(doc.get("payload").get("value").asBool(b));
    CHECK(b);

    CHECK_EQ(static_cast<int>(doc.get("arr").size()), 3);
    i32 second = 0;
    CHECK(doc.get("arr").at(1).asInt(second));
    CHECK_EQ(second, 2);

    CHECK(!doc.get("czegoTakiegoNieMa").valid());
}

TEST("JSON: nawias w napisie nie kończy obiektu") {
    // Zliczanie klamer bez rozumienia napisów urywało dokument w środku —
    // objawem był brak pola, które w tekście jest widoczne gołym okiem.
    json::JsonView doc{"{\"a\":\"}{\",\"b\":7}"};
    i32 b = 0;
    CHECK(doc.get("b").asInt(b));
    CHECK_EQ(b, 7);
}

// ---------------------------------------------------------------------------
// Ramkowanie szeregowe
// ---------------------------------------------------------------------------

TEST("COBS: obieg tam i z powrotem, także z zerami w danych") {
    const u8 data[] = {0x01, 0x00, 0x02, 0x00, 0x00, 0xFF, 0x03};
    u8 encoded[32];
    u8 decoded[32];

    auto n = cobsEncode(CByteSpan{data, sizeof(data)}, ByteSpan{encoded, sizeof(encoded)});
    REQUIRE(n.has_value());
    // Cały sens COBS: w postaci zakodowanej nie ma ani jednego zera, więc
    // zero jest jednoznaczną granicą ramki.
    for (size_t i = 0; i < *n; ++i) CHECK(encoded[i] != 0);

    auto m = cobsDecode(CByteSpan{encoded, *n}, ByteSpan{decoded, sizeof(decoded)});
    REQUIRE(m.has_value());
    CHECK_EQ(static_cast<int>(*m), static_cast<int>(sizeof(data)));
    CHECK_EQ(memcmp(decoded, data, sizeof(data)), 0);
}

TEST("COBS: blok dłuższy niż 254 bajty") {
    // Granica formatu — przy 254 niezerowych bajtach licznik się przepełnia
    // i trzeba rozpocząć nowy blok.
    u8 data[600];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = static_cast<u8>((i % 255) + 1);

    u8 encoded[700];
    u8 decoded[700];
    auto n = cobsEncode(CByteSpan{data, sizeof(data)}, ByteSpan{encoded, sizeof(encoded)});
    REQUIRE(n.has_value());
    auto m = cobsDecode(CByteSpan{encoded, *n}, ByteSpan{decoded, sizeof(decoded)});
    REQUIRE(m.has_value());
    CHECK_EQ(static_cast<int>(*m), static_cast<int>(sizeof(data)));
    CHECK_EQ(memcmp(decoded, data, sizeof(data)), 0);
}

TEST("ramka szeregowa: obieg z tożsamością i typem rozszerzenia") {
    const char* payload = "{\"metrics\":[]}";

    SerialFrame in;
    in.kind  = MsgKind::ExtResponse;
    in.src   = 3;
    in.dst   = kGatewayNode;
    in.flags = kFlagIdent | kFlagExt;
    in.addr.set("user1", "dev-iot3");
    strcpy(in.extType, "vkbd");
    in.payload = CByteSpan{reinterpret_cast<const u8*>(payload), strlen(payload)};

    u8 wire[512];
    auto n = encodeSerial(in, ByteSpan{wire, sizeof(wire)});
    REQUIRE(n.has_value());
    CHECK_EQ(static_cast<int>(wire[*n - 1]), 0);   // granica ramki

    SerialFrame out;
    REQUIRE(decodeSerial(ByteSpan{wire, *n - 1}, out).has_value());
    CHECK_EQ(static_cast<int>(out.kind), static_cast<int>(MsgKind::ExtResponse));
    CHECK_EQ(static_cast<int>(out.src), 3);
    CHECK_STR(out.addr.user, "user1");
    CHECK_STR(out.addr.device, "dev-iot3");
    CHECK_STR(out.extType, "vkbd");
    CHECK_EQ(static_cast<int>(out.payload.size()), static_cast<int>(strlen(payload)));
    CHECK_EQ(memcmp(out.payload.data(), payload, strlen(payload)), 0);
}

TEST("ramka szeregowa: przekłamany bit jest wyłapany przez CRC") {
    // Na RS-485 przy 500 m to zdarzenie oczekiwane. Bez sumy kontrolnej
    // uszkodzony ładunek doszedłby do parsera JSON i został zgłoszony jako
    // „niepoprawny dokument" — komunikat wskazujący na nadawcę, nie na kabel.
    SerialFrame in;
    in.kind = MsgKind::Telemetry;
    in.src  = 5;
    const char* payload = "{\"a\":1}";
    in.payload = CByteSpan{reinterpret_cast<const u8*>(payload), strlen(payload)};

    u8 wire[128];
    auto n = encodeSerial(in, ByteSpan{wire, sizeof(wire)});
    REQUIRE(n.has_value());

    wire[4] = static_cast<u8>(wire[4] ^ 0x01);

    SerialFrame out;
    CHECK(!decodeSerial(ByteSpan{wire, *n - 1}, out).has_value());
}

TEST("ramka szeregowa: śmieci na magistrali nie udają ramki") {
    const u8 junk[] = {0x05, 0xAA, 0xBB};
    SerialFrame out;
    u8 copy[sizeof(junk)];
    memcpy(copy, junk, sizeof(junk));
    CHECK(!decodeSerial(ByteSpan{copy, sizeof(copy)}, out).has_value());
}

// ---------------------------------------------------------------------------
// Tematy
// ---------------------------------------------------------------------------

TEST("tematy: budowa i rozbiór wszystkich rodzajów") {
    const DeviceAddr addr = DeviceAddr::of("user1", "dev-iot1");
    char topic[kTopicMax];

    CHECK(buildTopic(topic, sizeof(topic), addr, MsgKind::Telemetry));
    CHECK_STR(topic, "minis/user1/dev-iot1/telemetry");

    CHECK(buildTopic(topic, sizeof(topic), addr, MsgKind::CommandAck));
    CHECK_STR(topic, "minis/user1/dev-iot1/command/ack");

    CHECK(buildTopic(topic, sizeof(topic), addr, MsgKind::ExtRequest, "vmouse"));
    CHECK_STR(topic, "minis/user1/dev-iot1/ext/vmouse/req");

    DeviceAddr parsed;
    MsgKind kind = MsgKind::Unknown;
    char ext[kExtTypeMax] = {};
    CHECK(parseTopic("minis/user1/dev-iot1/ext/vmouse/req", parsed, kind, ext, sizeof(ext)));
    CHECK_STR(parsed.device, "dev-iot1");
    CHECK_EQ(static_cast<int>(kind), static_cast<int>(MsgKind::ExtRequest));
    CHECK_STR(ext, "vmouse");

    CHECK(parseTopic("minis/user1/dev-iot1/command/ack", parsed, kind, ext, sizeof(ext)));
    CHECK_EQ(static_cast<int>(kind), static_cast<int>(MsgKind::CommandAck));

    CHECK(!parseTopic("homeassistant/sensor/x", parsed, kind, ext, sizeof(ext)));
}

// ---------------------------------------------------------------------------
// Trasowanie
// ---------------------------------------------------------------------------

/** Łącze zapisujące, co przez nie przeszło. Zastępuje MQTT i magistralę. */
class FakeLink : public ILink {
public:
    FakeLink(const char* label, bool uplink) : label_(label), uplink_(uplink) {}

    const char* name() const override { return label_; }
    Status begin() override { return ok(); }
    bool   up() const override { return up_; }
    size_t mtu() const override { return mtu_; }
    bool   isUplink() const override { return uplink_; }

    Status send(const Frame& frame) override {
        if (!up_) return fail(Err::NotInitialized);
        last = frame;
        ++sent;
        return ok();
    }
    void poll(Millis) override {}

    Status observe(const DeviceAddr& addr) override {
        observed = addr;
        ++observeCalls;
        return ok();
    }

    /** Wstrzykuje ramkę tak, jakby przyszła z medium. */
    void inject(Frame frame) { deliver(frame); }

    void setUp(bool value) { up_ = value; }
    void setMtu(size_t value) { mtu_ = value; }

    Frame      last{};
    DeviceAddr observed{};
    u32        sent = 0;
    u32        observeCalls = 0;

private:
    const char* label_;
    bool        uplink_;
    bool        up_  = true;
    size_t      mtu_ = 512;
};

Frame makeFrame(const char* user, const char* device, MsgKind kind,
                const char* payload) {
    Frame f;
    f.addr.set(user, device);
    f.kind = kind;
    f.payload = CByteSpan{reinterpret_cast<const u8*>(payload), strlen(payload)};
    return f;
}

TEST("router: ramka do nas trafia lokalnie, nie na łącze") {
    Router router;
    FakeLink mqtt{"mqtt", true};
    REQUIRE(router.addLink(mqtt).has_value());
    router.setLocal(DeviceAddr::of("user1", "dev-a"));

    int delivered = 0;
    router.setLocalHandler([&delivered](const Frame&) { ++delivered; });

    mqtt.inject(makeFrame("user1", "dev-a", MsgKind::Command, "{}"));

    CHECK_EQ(delivered, 1);
    CHECK_EQ(static_cast<int>(mqtt.sent), 0);
}

TEST("router: bramka uczy się węzła z ruchu w górę i prosi o subskrypcję") {
    // Sedno trybu bramki: po pierwszym `hello` z magistrali komenda z serwera
    // trafia do węzła sama, bez wpisu w konfiguracji.
    Router router;
    FakeLink mqtt{"mqtt", true};
    FakeLink bus{"rs485", false};
    REQUIRE(router.addLink(mqtt).has_value());
    REQUIRE(router.addLink(bus).has_value());
    router.setLocal(DeviceAddr::of("user1", "gw"));

    bus.inject(makeFrame("user1", "dev-node3", MsgKind::Hello, "{}"));

    CHECK_EQ(static_cast<int>(router.routeCount()), 1);
    CHECK_EQ(static_cast<int>(mqtt.sent), 1);                 // hello poszło w górę
    CHECK_EQ(static_cast<int>(mqtt.observeCalls), 1);         // i poprosiliśmy o nasłuch
    CHECK_STR(mqtt.observed.device, "dev-node3");

    // Teraz komenda z serwera do tego węzła musi zejść na magistralę.
    mqtt.inject(makeFrame("user1", "dev-node3", MsgKind::Command, "{\"id\":\"1\"}"));
    CHECK_EQ(static_cast<int>(bus.sent), 1);
    CHECK_STR(bus.last.addr.device, "dev-node3");
    CHECK_EQ(static_cast<int>(bus.last.hops), 1);
}

TEST("router: nauka nigdy z łącza prowadzącego do serwera") {
    // Z łącza do serwera przychodzi ruch wszystkich urządzeń świata. Nauka
    // z niego dałaby trasę „każdy jest w internecie" i pętlę przy pierwszej
    // odpowiedzi.
    Router router;
    FakeLink mqtt{"mqtt", true};
    FakeLink bus{"rs485", false};
    REQUIRE(router.addLink(mqtt).has_value());
    REQUIRE(router.addLink(bus).has_value());
    router.setLocal(DeviceAddr::of("user1", "gw"));

    mqtt.inject(makeFrame("user1", "dev-obcy", MsgKind::Telemetry, "{}"));
    CHECK_EQ(static_cast<int>(router.routeCount()), 0);
}

TEST("router: ramka nie wraca na łącze, z którego przyszła") {
    Router router;
    FakeLink mqtt{"mqtt", true};
    REQUIRE(router.addLink(mqtt).has_value());
    router.setLocal(DeviceAddr::of("user1", "gw"));

    // Nieznany adresat, jedyne łącze to to, z którego ramka przyszła.
    mqtt.inject(makeFrame("user1", "dev-nieznany", MsgKind::Command, "{}"));

    CHECK_EQ(static_cast<int>(mqtt.sent), 0);
    CHECK_EQ(static_cast<int>(router.stats().dropped), 1);
}

TEST("router: limit przeskoków ubija ramkę krążącą między bramkami") {
    Router router;
    FakeLink mqtt{"mqtt", true};
    FakeLink bus{"rs485", false};
    REQUIRE(router.addLink(mqtt).has_value());
    REQUIRE(router.addLink(bus).has_value());

    Router::Config cfg;
    cfg.maxHops = 2;
    router.configure(cfg);

    Frame f = makeFrame("user1", "dev-x", MsgKind::Telemetry, "{}");
    f.hops = 3;
    bus.inject(f);

    CHECK_EQ(static_cast<int>(mqtt.sent), 0);
    CHECK_EQ(static_cast<int>(router.stats().dropped), 1);
}

TEST("router: trasa statyczna wygrywa z obserwacją") {
    Router router;
    FakeLink mqtt{"mqtt", true};
    FakeLink busA{"rs485-a", false};
    FakeLink busB{"rs485-b", false};
    REQUIRE(router.addLink(mqtt).has_value());
    auto a = router.addLink(busA);
    REQUIRE(a.has_value());
    REQUIRE(router.addLink(busB).has_value());
    router.setLocal(DeviceAddr::of("user1", "gw"));

    const DeviceAddr node = DeviceAddr::of("user1", "dev-node3");
    REQUIRE(router.addRoute(node, *a).has_value());

    // Echo tego samego węzła z drugiej magistrali nie ma prawa przestawić
    // trasy wpisanej świadomie.
    busB.inject(makeFrame("user1", "dev-node3", MsgKind::Hello, "{}"));
    CHECK_EQ(static_cast<int>(router.routeFor(node)), static_cast<int>(*a));
}

TEST("router: ładunek większy niż MTU łącza jest odrzucany, nie obcinany") {
    Router router;
    FakeLink mqtt{"mqtt", true};
    mqtt.setMtu(8);
    REQUIRE(router.addLink(mqtt).has_value());

    const Frame f = makeFrame("user1", "dev-a", MsgKind::Telemetry,
                              "{\"to\":\"jest\",\"za\":\"dlugie\"}");
    CHECK(!router.send(f, 0).has_value());
    CHECK_EQ(static_cast<int>(mqtt.sent), 0);
}

TEST("router: brak łącza do serwera to stan offline") {
    Router router;
    FakeLink mqtt{"mqtt", true};
    REQUIRE(router.addLink(mqtt).has_value());
    CHECK(router.online());
    mqtt.setUp(false);
    CHECK(!router.online());
}

// ---------------------------------------------------------------------------
// Protokół — to, co naprawdę zobaczy serwer
// ---------------------------------------------------------------------------

/** Moduł z jednym atrapowym łączem; `link.last` to ostatnia nadana ramka. */
struct Fixture {
    MinisIotModule module;
    FakeLink       link{"mqtt", true};

    explicit Fixture(bool autoRegister = false) {
        MinisIotModule::Config cfg;
        cfg.self = DeviceAddr::of("user1", "dev-iot1");
        cfg.autoRegister = autoRegister;
        module.configure(cfg);
        module.addLink(link);
    }

    /** Ostatni ładunek jako tekst z zerem kończącym. */
    const char* payload() {
        static char buf[1024];
        const size_t n = link.last.payload.size() < sizeof(buf) - 1
                             ? link.last.payload.size() : sizeof(buf) - 1;
        memcpy(buf, link.last.payload.data(), n);
        buf[n] = 0;
        return buf;
    }
};

TEST("protokół: telemetria ma kształt oczekiwany przez serwer") {
    Fixture fx;
    const Metric metrics[] = {
        Metric::of("temperature", 21.5f, "°C"),
        Metric::of("motion", true),
    };
    REQUIRE(fx.module.sendTelemetry(metrics, 2).has_value());

    CHECK_EQ(static_cast<int>(fx.link.last.kind), static_cast<int>(MsgKind::Telemetry));
    CHECK_STR(fx.payload(),
              "{\"metrics\":[{\"key\":\"temperature\",\"value\":21.5,\"unit\":\"°C\"},"
              "{\"key\":\"motion\",\"value\":true}]}");
}

TEST("protokół: hello ogłasza encje z ich typami") {
    Fixture fx;
    Entity temp  = Entity::sensor("temp", "Temperatura", "temperature", "°C");
    Entity relay = Entity::toggle("relay", "Przekaźnik",
                                  [](json::JsonView) { return true; });
    REQUIRE(fx.module.addEntity(temp).has_value());
    REQUIRE(fx.module.addEntity(relay).has_value());
    REQUIRE(fx.module.addExtension("vkbd", {}).has_value());

    REQUIRE(fx.module.sendHello().has_value());

    const char* body = fx.payload();
    CHECK(has(body, "\"entities\":["));
    CHECK(has(body, "\"id\":\"temp\",\"type\":\"sensor\""));
    CHECK(has(body, "\"device_class\":\"temperature\""));
    CHECK(has(body, "\"id\":\"relay\",\"type\":\"switch\""));
    CHECK(has(body, "\"extensions\":[{\"type\":\"vkbd\",\"enabled\":true}]"));
}

TEST("protokół: encja o powtórzonym identyfikatorze jest odrzucana") {
    // Identyfikator jest zarazem nazwą komendy zapisu — duplikat oznaczałby
    // encję, która nigdy nie dostaje sterowania, i objaw „przycisk nie działa".
    Fixture fx;
    Entity a = Entity::sensor("temp", "A");
    Entity b = Entity::sensor("temp", "B");
    REQUIRE(fx.module.addEntity(a).has_value());
    CHECK(!fx.module.addEntity(b).has_value());
}

TEST("protokół: komenda trafia w encję i sama się potwierdza") {
    Fixture fx;
    bool state = false;
    Entity relay = Entity::toggle("relay", "Przekaźnik",
                                  [&state](json::JsonView value) {
                                      return value.asBool(state);
                                  });
    REQUIRE(fx.module.addEntity(relay).has_value());

    fx.link.inject(makeFrame("user1", "dev-iot1", MsgKind::Command,
                             "{\"id\":\"cmd-7\",\"name\":\"relay\","
                             "\"payload\":{\"value\":true}}"));

    CHECK(state);
    CHECK_EQ(static_cast<int>(fx.link.last.kind), static_cast<int>(MsgKind::CommandAck));
    CHECK_STR(fx.payload(), "{\"id\":\"cmd-7\",\"status\":\"ACKNOWLEDGED\"}");
}

TEST("protokół: encja odmawiająca daje FAILED z powodem") {
    Fixture fx;
    Entity relay = Entity::toggle("relay", "Przekaźnik",
                                  [](json::JsonView) { return false; });
    REQUIRE(fx.module.addEntity(relay).has_value());

    fx.link.inject(makeFrame("user1", "dev-iot1", MsgKind::Command,
                             "{\"id\":\"cmd-8\",\"name\":\"relay\","
                             "\"payload\":{\"value\":true}}"));

    CHECK(has(fx.payload(), "\"status\":\"FAILED\""));
    CHECK(has(fx.payload(), "\"reason\""));
}

TEST("protokół: komenda bez obsługi jest odrzucana, a nie przemilczana") {
    // Panel czeka na potwierdzenie; cisza pokazuje komendę jako wiszącą aż do
    // przekroczenia czasu, czyli wygląda jak zawieszone urządzenie.
    Fixture fx;
    fx.link.inject(makeFrame("user1", "dev-iot1", MsgKind::Command,
                             "{\"id\":\"cmd-9\",\"name\":\"nieznana\"}"));

    CHECK_EQ(static_cast<int>(fx.link.last.kind), static_cast<int>(MsgKind::CommandAck));
    CHECK(has(fx.payload(), "\"status\":\"FAILED\""));
}

TEST("protokół: żądanie rozszerzenia dochodzi do handlera, odpowiedź na właściwy temat") {
    Fixture fx;
    char seenOp[16] = {};
    REQUIRE(fx.module.addExtension("vkbd",
        [&fx, &seenOp](const char* id, const char* op, json::JsonView) {
            strncpy(seenOp, op, sizeof(seenOp) - 1);
            (void)fx.module.extRespond("vkbd", id, true, "{\"typed\":3}");
        }).has_value());

    Frame req = makeFrame("user1", "dev-iot1", MsgKind::ExtRequest,
                          "{\"id\":\"r1\",\"op\":\"type_text\",\"params\":{\"text\":\"abc\"}}");
    strcpy(req.extType, "vkbd");
    fx.link.inject(req);

    CHECK_STR(seenOp, "type_text");
    CHECK_EQ(static_cast<int>(fx.link.last.kind), static_cast<int>(MsgKind::ExtResponse));
    CHECK_STR(fx.link.last.extType, "vkbd");
    // `data` jako obiekt, nie napis — inaczej serwer dostaje tekst zamiast danych.
    CHECK_STR(fx.payload(), "{\"id\":\"r1\",\"ok\":true,\"data\":{\"typed\":3}}");
}

TEST("protokół: nieznane rozszerzenie dostaje odpowiedź z błędem") {
    Fixture fx;
    Frame req = makeFrame("user1", "dev-iot1", MsgKind::ExtRequest,
                          "{\"id\":\"r2\",\"op\":\"click\"}");
    strcpy(req.extType, "vmouse");
    fx.link.inject(req);

    CHECK(has(fx.payload(), "\"ok\":false"));
    CHECK(has(fx.payload(), "\"code\":\"unsupported\""));
}

TEST("protokół: zgłoszenie do listy urządzeń niesie numer seryjny") {
    Fixture fx;
    REQUIRE(fx.module.sendRegisterRequest("Czujnik korytarz").has_value());
    CHECK_STR(fx.payload(),
              "{\"kind\":\"firmware\",\"label\":\"Czujnik korytarz\",\"sn\":\"dev-iot1\"}");
}

TEST("protokół: telemetria nieszcząca się w buforze jest zgłaszana, nie obcinana") {
    Fixture fx;
    Metric many[40];
    for (auto& m : many) m = Metric::of("bardzo-dlugi-klucz-pomiaru", 1.25f, "jednostka");

    CHECK(!fx.module.sendTelemetry(many, 40).has_value());
    CHECK_EQ(static_cast<int>(fx.module.stats().truncated), 1);
    CHECK_EQ(static_cast<int>(fx.link.sent), 0);
}

}  // namespace
