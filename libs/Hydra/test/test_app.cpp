/** Testy cyklu życia aplikacji i modułów (rozdz. 4.1). */

#include "hydra_test.hpp"

#include <stdio.h>
#include <string.h>

#include <atomic>

#include "hydra/core/App.hpp"
#include "hydra/core/Version.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/core/IModule.hpp"

using namespace hydra;

namespace {

/** Zapis kolejności wywołań cyklu życia — wspólny dla wszystkich atrap. */
char gTrace[128];

void trace(const char* what) {
    const size_t len = strlen(gTrace);
    snprintf(gTrace + len, sizeof(gTrace) - len, "%s;", what);
}

class FakeModule : public ModuleBase {
public:
    FakeModule(const char* name, Err initErr = Err::None)
        : ModuleBase(name), initErr_(initErr) {}

    int inits = 0, starts = 0, stops = 0;

protected:
    Status onInit() override {
        ++inits;
        trace(name());
        trace("init");
        if (initErr_ != Err::None) return fail(initErr_);
        return ok();
    }
    Status onStart() override {
        ++starts;
        trace(name());
        trace("start");
        return ok();
    }
    void onStop() override {
        ++stops;
        trace(name());
        trace("stop");
    }

private:
    Err initErr_;
};

void resetAll() {
    App::reset();
    gTrace[0] = '\0';
}

}  // namespace

TEST("App: moduły startują w kolejności rejestracji") {
    resetAll();

    FakeModule a("mod.a");
    FakeModule b("mod.b");

    App::config().name("test-dev").housekeepingMs(20).heartbeat(false).logLevel(LogLevel::Off);
    App::config().add(a).add(b);

    REQUIRE(App::begin().has_value());
    CHECK(App::running());
    CHECK_STR(App::deviceName(), "test-dev");
    CHECK_EQ(static_cast<int>(App::moduleCount()), 2);

    CHECK_STR(gTrace, "mod.a;init;mod.b;init;mod.a;start;mod.b;start;");
    CHECK(a.state() == ModuleState::Running);
    CHECK(b.state() == ModuleState::Running);

    resetAll();
}

TEST("App: stop zatrzymuje moduły w kolejności odwrotnej") {
    resetAll();

    FakeModule a("mod.a");
    FakeModule b("mod.b");
    App::config().name("t").housekeepingMs(20).heartbeat(false).logLevel(LogLevel::Off);
    App::config().add(a).add(b);
    REQUIRE(App::begin().has_value());

    gTrace[0] = '\0';
    App::stop();

    CHECK_STR(gTrace, "mod.b;stop;mod.a;stop;");
    CHECK(!App::running());
    CHECK(a.state() == ModuleState::Stopped);

    resetAll();
}

TEST("App: nieudany init cofa moduły już zainicjalizowane") {
    resetAll();

    FakeModule good("mod.good");
    FakeModule bad("mod.bad", Err::IoError);
    App::config().name("t").housekeepingMs(20).heartbeat(false).logLevel(LogLevel::Off);
    App::config().add(good).add(bad);

    auto r = App::begin();
    CHECK(!r.has_value());
    CHECK(r.error() == Err::IoError);
    CHECK(!App::running());

    // Moduł, który zdążył się zainicjalizować, dostał stop() — urządzenie nie
    // zostaje w stanie połowicznie wystartowanym.
    CHECK_EQ(good.stops, 1);
    CHECK_EQ(good.starts, 0);
    CHECK(bad.state() == ModuleState::Failed);

    resetAll();
}

TEST("App: zmiana stanu modułu trafia na magistralę") {
    resetAll();

    int  changes = 0;
    u16  lastId  = 0;
    auto sub = EventBus::subscribe<ModuleStateChanged>([&](const ModuleStateChanged& e) {
        ++changes;
        lastId = e.moduleId;
    });
    REQUIRE(sub.has_value());

    FakeModule a("mod.a");
    App::config().name("t").housekeepingMs(20).heartbeat(false).logLevel(LogLevel::Off);
    App::config().add(a);
    REQUIRE(App::begin().has_value());

    // Initialized + Running
    CHECK(changes >= 2);
    CHECK_EQ(lastId, nameId("mod.a"));

    resetAll();
}

TEST("App: core.house publikuje puls i drenuje kolejkę ISR") {
    resetAll();

    App::config().name("t").housekeepingMs(20).heartbeat(true).logLevel(LogLevel::Off);
    REQUIRE(App::begin().has_value());

    // Zapisywane z taska core.house, czytane z wątku testu.
    std::atomic<int> beats{0};
    std::atomic<u32> uptime{0};
    auto sub = EventBus::subscribe<SysHeartbeat>([&](const SysHeartbeat& e) {
        ++beats;
        uptime = e.uptimeMs;
    });
    REQUIRE(sub.has_value());

    rtos::delayMs(90);
    CHECK(beats.load() >= 2);
    CHECK(uptime.load() > 0);

    resetAll();
}

TEST("App: SysStarted niesie liczbę modułów") {
    resetAll();

    int  started = 0;
    u8   count   = 0;
    auto sub = EventBus::subscribe<SysStarted>([&](const SysStarted& e) {
        ++started;
        count = e.moduleCount;
    });
    REQUIRE(sub.has_value());

    FakeModule a("mod.a");
    FakeModule b("mod.b");
    App::config().name("t").housekeepingMs(50).heartbeat(false).logLevel(LogLevel::Off);
    App::config().add(a).add(b);
    REQUIRE(App::begin().has_value());

    CHECK_EQ(started, 1);
    CHECK_EQ(static_cast<int>(count), 2);

    resetAll();
}

TEST("App: findModule odnajduje moduł po nazwie") {
    resetAll();

    FakeModule a("mod.a");
    App::config().name("t").housekeepingMs(50).heartbeat(false).logLevel(LogLevel::Off);
    App::config().add(a);
    REQUIRE(App::begin().has_value());

    CHECK(App::findModule("mod.a") == &a);
    CHECK(App::findModule("nie ma") == nullptr);
    CHECK(App::module(0) == &a);
    CHECK(App::module(9) == nullptr);

    resetAll();
}

TEST("App: rejestracja tego samego modułu jest idempotentna") {
    resetAll();

    FakeModule a("mod.a");
    App::config().name("t").heartbeat(false).logLevel(LogLevel::Off);
    App::config().add(a).add(a).add(a);
    CHECK_EQ(static_cast<int>(App::config().deviceName()[0]), static_cast<int>('t'));
    CHECK_EQ(static_cast<int>(App::moduleCount()), 1);

    resetAll();
}

TEST("App: watchdog karmiony tylko przy pozytywnym teście zdrowia") {
    resetAll();

    int  feeds   = 0;
    bool healthy = true;

    // Celowo bez App::begin(): housekeeping() wołamy wprost, żeby wynik nie
    // zależał od tego, czy task core.house zdążył wykonać własną iterację.
    // Powiązanie taska z tą funkcją sprawdza osobny test pulsu.
    App::config().name("t").heartbeat(false).logLevel(LogLevel::Off);
    App::config()
        .watchdog(Delegate<void()>([&] { ++feeds; }))
        .healthCheck(Delegate<bool()>([&] { return healthy; }));

    App::housekeeping();
    CHECK_EQ(feeds, 1);

    healthy = false;
    App::housekeeping();
    CHECK_EQ(feeds, 1);  // brak karmienia → reset sprzętowy w realnym urządzeniu

    healthy = true;
    App::housekeeping();
    CHECK_EQ(feeds, 2);

    resetAll();
}

TEST("App: powtórny begin() jest odrzucany") {
    resetAll();

    App::config().name("t").housekeepingMs(50).heartbeat(false).logLevel(LogLevel::Off);
    REQUIRE(App::begin().has_value());

    auto second = App::begin();
    CHECK(!second.has_value());
    CHECK(second.error() == Err::AlreadyExists);

    resetAll();
}

TEST("Wersja: napis i liczba pochodzą z tych samych składowych") {
    // Dwa niezależne zapisy tej samej wersji rozjeżdżają się przy pierwszym
    // wydaniu, o którym ktoś zapomni, a rozbieżność widać dopiero w logu
    // urządzenia w terenie.
    char expected[16];
    snprintf(expected, sizeof(expected), "%d.%d.%d", HYDRA_VERSION_MAJOR,
             HYDRA_VERSION_MINOR, HYDRA_VERSION_PATCH);
    CHECK_STR(version(), expected);

    CHECK_EQ(HYDRA_VERSION_NUM, HYDRA_VERSION_MAJOR * 10000 +
                                    HYDRA_VERSION_MINOR * 100 + HYDRA_VERSION_PATCH);
    CHECK(HYDRA_VERSION_MAJOR >= 1);
}
