/**
 * Hydra — testy podsystemu skryptów.
 *
 * Trzy warstwy sprawdzane osobno:
 *   1. alokator statycznej puli — w izolacji od Lua,
 *   2. interpreter i most C++ ↔ Lua,
 *   3. bindingi i moduł, na atrapowym backendzie HAL.
 *
 * Najważniejszy przypadek w całym pliku to „nieskończona pętla jest
 * wywłaszczana". Bez niego osadzanie skryptów w systemie czasu rzeczywistego
 * byłoby nieodpowiedzialne, a z nim jest cechą.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/script/Bindings.hpp"
#include "hydra/script/Heap.hpp"
#include "hydra/script/LuaEngine.hpp"
#include "hydra/script/ScriptModule.hpp"

using namespace hydra;
using namespace hydra::script;

namespace {

/**
 * Pula dla testów otwierających więcej niż jeden interpreter naraz.
 * Domyślną pulę może trzymać tylko jeden `Interp`, co samo w sobie jest
 * sprawdzane niżej.
 */
alignas(8) u8 gTestPool[64 * 1024];

/** Otwiera interpreter na własnej puli, żeby nie kolidować z domyślną. */
Status openOnTestPool(Interp& interp, const Interp::Libs& libs = Interp::Libs{}) {
    Interp::Config cfg{};
    cfg.libs      = libs;
    cfg.pool      = gTestPool;
    cfg.poolBytes = sizeof(gTestPool);
    return interp.open(cfg);
}

/** Zbiera wyjście skryptu, żeby dało się je sprawdzić asercją. */
char   gCaptured[96];
size_t gCapturedLen = 0;

void captureOutput() {
    gCaptured[0]  = '\0';
    gCapturedLen  = 0;
    setOutput([](const char* text, size_t len) {
        (void)len;
        const size_t n = strlen(text);
        if (gCapturedLen + n + 2 >= sizeof(gCaptured)) return;
        if (gCapturedLen > 0) gCaptured[gCapturedLen++] = '\n';
        memcpy(gCaptured + gCapturedLen, text, n);
        gCapturedLen += n;
        gCaptured[gCapturedLen] = '\0';
    });
}

}  // namespace

// ---------------------------------------------------------------------------
// Alokator
// ---------------------------------------------------------------------------

TEST("Script: sterta przydziela, zwalnia i scala z powrotem w jeden blok") {
    alignas(8) static u8 pool[4096];
    Heap heap;
    REQUIRE(heap.init(pool, sizeof(pool)).has_value());
    CHECK(heap.validate());

    void* a = heap.allocate(100);
    void* b = heap.allocate(200);
    void* c = heap.allocate(300);
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    CHECK(c != nullptr);
    CHECK(heap.validate());
    CHECK(heap.stats().liveBlocks == 3);

    // Zwolnienie środkowego zostawia dziurę, ale nie łączy sąsiadów.
    heap.release(b);
    CHECK(heap.validate());

    heap.release(a);
    heap.release(c);
    CHECK(heap.validate());

    const auto s = heap.stats();
    CHECK_EQ(static_cast<int>(s.used), 0);
    CHECK_EQ(static_cast<int>(s.freeBlocks), 1);
    CHECK_EQ(static_cast<int>(s.largestFree), static_cast<int>(s.capacity - Heap::kHeaderSize));
}

TEST("Script: sterta odmawia zamiast wyjsc poza pule") {
    alignas(8) static u8 pool[1024];
    Heap heap;
    REQUIRE(heap.init(pool, sizeof(pool)).has_value());

    CHECK(heap.allocate(64 * 1024) == nullptr);
    CHECK(heap.allocate(2000) == nullptr);
    CHECK_EQ(static_cast<int>(heap.stats().failures), 2);
    CHECK(heap.validate());

    // Po odmowie sterta musi dalej działać normalnie.
    void* p = heap.allocate(64);
    CHECK(p != nullptr);
    heap.release(p);
    CHECK(heap.validate());
}

TEST("Script: zmiana rozmiaru zachowuje zawartosc") {
    alignas(8) static u8 pool[8192];
    Heap heap;
    REQUIRE(heap.init(pool, sizeof(pool)).has_value());

    auto* p = static_cast<u8*>(heap.allocate(32));
    REQUIRE(p != nullptr);
    for (int i = 0; i < 32; ++i) p[i] = static_cast<u8>(i);

    // Blokujemy sąsiada, żeby powiększenie musiało przenieść dane.
    void* blocker = heap.allocate(64);
    auto* grown   = static_cast<u8*>(heap.reallocate(p, 32, 512));
    REQUIRE(grown != nullptr);
    for (int i = 0; i < 32; ++i) CHECK_EQ(static_cast<int>(grown[i]), i);

    auto* shrunk = static_cast<u8*>(heap.reallocate(grown, 512, 16));
    REQUIRE(shrunk != nullptr);
    for (int i = 0; i < 16; ++i) CHECK_EQ(static_cast<int>(shrunk[i]), i);

    heap.release(shrunk);
    heap.release(blocker);
    CHECK(heap.validate());
    CHECK_EQ(static_cast<int>(heap.stats().used), 0);
}

// ---------------------------------------------------------------------------
// Interpreter
// ---------------------------------------------------------------------------

TEST("Script: interpreter startuje i liczy") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());

    captureOutput();
    CHECK(lua.doString("local s = 0 for i = 1, 100 do s = s + i end print(s)").has_value());
    CHECK_STR(gCaptured, "5050");

    resetOutput();
    lua.close();
}

TEST("Script: interpreter nie siega po sterte systemowa") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());

    const auto fresh = lua.memory();
    CHECK(fresh.used > 0);
    CHECK(fresh.used < fresh.capacity);
    CHECK_EQ(static_cast<int>(fresh.failures), 0);

    CHECK(lua.doString("t = {} for i = 1, 200 do t[i] = ('x'):rep(i % 20) end").has_value());
    const auto loaded = lua.memory();
    CHECK(loaded.used > fresh.used);

    // Odśmiecanie musi realnie oddawać pamięć do naszej puli.
    CHECK(lua.doString("t = nil").has_value());
    const u32 afterGc = lua.collect();
    CHECK(afterGc < loaded.used);

    lua.close();
}

TEST("Script: blad skladni i blad wykonania sa zglaszane, nie przerywaja programu") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());

    auto syntax = lua.doString("to nie jest lua =");
    CHECK(!syntax);
    CHECK_EQ(syntax.error(), Err::BadArgument);
    CHECK(strlen(lua.error()) > 0);

    auto runtime = lua.doString("error('celowy blad')");
    CHECK(!runtime);
    CHECK(strstr(lua.error(), "celowy blad") != nullptr);

    // Po obu błędach interpreter musi być dalej sprawny.
    CHECK(lua.doString("x = 7").has_value());
    CHECK(lua.hasGlobal("x"));

    lua.close();
}

TEST("Script: druga instancja nie przejmuje domyslnej puli") {
    Interp first;
    REQUIRE(first.open().has_value());

    Interp second;
    auto   taken = second.open();
    CHECK(!taken);
    CHECK_EQ(taken.error(), Err::Busy);

    // Po zamknięciu pierwszego pula wraca do obiegu.
    first.close();
    CHECK(second.open().has_value());
    second.close();
}

// ---------------------------------------------------------------------------
// Funkcje natywne
// ---------------------------------------------------------------------------

namespace {

int nativeSum(Ctx& c) {
    i32 total = 0;
    for (int i = 1; i <= c.argCount(); ++i) {
        auto v = c.argInt(i);
        if (!v) return c.fail("argument %d nie jest liczba", i);
        total += *v;
    }
    c.pushInt(total);
    return 1;
}

int nativeMakeTable(Ctx& c) {
    c.pushTable();
    c.pushStr("hydra");
    c.setField("name");
    c.pushInt(c.optInt(1, 0) * 2);
    c.setField("doubled");
    return 1;
}

int nativeUserPointer(Ctx& c) {
    auto* value = static_cast<int*>(c.user());
    c.pushInt(value != nullptr ? *value : -1);
    return 1;
}

}  // namespace

TEST("Script: funkcje natywne przyjmuja argumenty i oddaja wyniki") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(lua.registerFn("sum", nativeSum).has_value());

    captureOutput();
    CHECK(lua.doString("print(sum(1, 2, 3, 4))").has_value());
    CHECK_STR(gCaptured, "10");
    resetOutput();

    lua.close();
}

TEST("Script: blad funkcji natywnej jest przechwytywalny przez pcall") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(lua.registerFn("sum", nativeSum).has_value());

    captureOutput();
    CHECK(lua.doString("local ok, err = pcall(sum, 'nie liczba') print(ok, err)").has_value());
    CHECK(strstr(gCaptured, "false") != nullptr);
    CHECK(strstr(gCaptured, "nie jest liczba") != nullptr);
    resetOutput();

    // Interpreter po przechwyconym błędzie działa dalej.
    CHECK(lua.doString("assert(sum(2, 2) == 4)").has_value());
    lua.close();
}

TEST("Script: biblioteka natywna trafia do zagniezdzonej tabeli") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());

    static int userValue = 99;
    const Reg  reg[]     = {{"make", nativeMakeTable}, {"who", nativeUserPointer}, {nullptr, nullptr}};
    REQUIRE(lua.registerLib("dev.tools", reg, &userValue).has_value());

    captureOutput();
    CHECK(lua.doString("local t = dev.tools.make(21) print(t.name, t.doubled, dev.tools.who())")
              .has_value());
    CHECK(strstr(gCaptured, "hydra") != nullptr);
    CHECK(strstr(gCaptured, "42") != nullptr);
    CHECK(strstr(gCaptured, "99") != nullptr);
    resetOutput();

    lua.close();
}

// ---------------------------------------------------------------------------
// Budżet instrukcji — sedno całego osadzenia
// ---------------------------------------------------------------------------

TEST("Script: nieskonczona petla jest wywlaszczana, nie zawiesza taska") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(lua.doString("n = 0\nfunction spin() while true do n = n + 1 end end").has_value());

    Job job;
    REQUIRE(job.start(lua, "spin").has_value());

    // Dziesięć porcji po tysiąc instrukcji. Gdyby wywłaszczanie nie działało,
    // ten test nigdy by się nie skończył — i to jest dokładnie ta awaria,
    // której mechanizm ma zapobiegać na urządzeniu.
    for (int i = 0; i < 10; ++i) {
        CHECK(job.resume(1000) == Job::State::Running);
    }
    CHECK_EQ(static_cast<int>(job.steps()), 10000);

    job.cancel();
    CHECK(job.state() == Job::State::Idle);

    // Skrypt naprawdę się wykonywał, a nie tylko był wznawiany.
    CHECK(lua.doString("assert(n > 0)").has_value());
    lua.close();
}

TEST("Script: zadanie mieszczace sie w budzecie konczy sie stanem Done") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(lua.doString("done = false\nfunction quick() for i = 1, 10 do end done = true end")
                .has_value());

    Job job;
    REQUIRE(job.start(lua, "quick").has_value());
    CHECK(job.resume(100000) == Job::State::Done);
    CHECK(lua.doString("assert(done)").has_value());

    lua.close();
}

TEST("Script: blad w zadaniu daje stan Failed z opisem") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(lua.doString("function boom() error('bum') end").has_value());

    Job job;
    REQUIRE(job.start(lua, "boom").has_value());
    CHECK(job.resume(10000) == Job::State::Failed);
    CHECK(strstr(lua.error(), "bum") != nullptr);

    lua.close();
}

TEST("Script: start zadania dla nieistniejacej funkcji zwraca NotFound") {
    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());

    Job  job;
    auto started = job.start(lua, "nie_ma_takiej");
    CHECK(!started);
    CHECK_EQ(started.error(), Err::NotFound);

    lua.close();
}

// ---------------------------------------------------------------------------
// Bindingi
// ---------------------------------------------------------------------------

namespace {

/** Świeży atrapowy backend HAL — ta sama droga, którą idą pozostałe testy. */
hal::mock::Backend& freshBackend() {
    hal::mock::backend().clear();
    hal::mock::install();
    return hal::mock::backend();
}

}  // namespace

TEST("Script: skrypt steruje pinami przez HAL") {
    auto& backend = freshBackend();
    EventBus::reset();

    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(installBindings(lua).has_value());

    CHECK(lua.doString("assert(hydra.gpio.mode(5, 'out')) assert(hydra.gpio.write(5, true))")
              .has_value());
    auto level = backend.gpio.read(5);
    REQUIRE(level.has_value());
    CHECK(*level);

    CHECK(lua.doString("assert(hydra.gpio.toggle(5))").has_value());
    level = backend.gpio.read(5);
    REQUIRE(level.has_value());
    CHECK(!*level);

    // Odczyt wejścia ustawionego po stronie atrapy.
    backend.gpio.setInputLevel(7, true);
    captureOutput();
    CHECK(lua.doString("hydra.gpio.mode(7, 'in') print(hydra.gpio.read(7))").has_value());
    CHECK_STR(gCaptured, "true");
    resetOutput();

    removeBindings(lua);
    lua.close();
}

TEST("Script: niepowodzenie sprzetu wraca jako nil i opis, nie jako blad") {
    freshBackend();
    EventBus::reset();

    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(installBindings(lua).has_value());

    // Atrapa odrzuca piny spoza swojego zakresu — skrypt ma dostać parę
    // wartości do obsłużenia, a nie wyjątek przerywający wykonanie.
    captureOutput();
    CHECK(lua.doString("local v, err = hydra.gpio.read(9999) print(v == nil, type(err))")
              .has_value());
    CHECK(strstr(gCaptured, "true") != nullptr);
    CHECK(strstr(gCaptured, "string") != nullptr);
    resetOutput();

    removeBindings(lua);
    lua.close();
}

TEST("Script: sygnaly z magistrali docieraja do handlerow skryptu") {
    freshBackend();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(installBindings(lua).has_value());

    REQUIRE(lua.doString("received = 0 lastValue = 0\n"
                         "hydra.event.on('alarm', function(value, data)\n"
                         "  received = received + 1\n"
                         "  lastValue = value + data\n"
                         "end)")
                .has_value());

    // Publikacja z C++ — tak, jak zrobiłby to dowolny inny moduł.
    EventBus::publish(ScriptSignal{nameId("alarm"), 21.5f, 3});

    const u32 handled = dispatchSignals(lua);
    CHECK_EQ(static_cast<int>(handled), 1);

    captureOutput();
    CHECK(lua.doString("print(received, lastValue)").has_value());
    CHECK(strstr(gCaptured, "1") != nullptr);
    CHECK(strstr(gCaptured, "24.5") != nullptr);
    resetOutput();

    removeBindings(lua);
    lua.close();
    EventBus::reset();
}

TEST("Script: emit ze skryptu trafia na magistrale") {
    freshBackend();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    Interp lua;
    REQUIRE(openOnTestPool(lua).has_value());
    REQUIRE(installBindings(lua).has_value());

    static int   seen  = 0;
    static float value = 0.0f;
    seen  = 0;
    value = 0.0f;
    auto sub = EventBus::subscribe<ScriptSignal>([](const ScriptSignal& s) {
        if (s.nameId == nameId("gotowe")) {
            ++seen;
            value = s.value;
        }
    });
    REQUIRE(sub.has_value());

    CHECK(lua.doString("hydra.event.emit('gotowe', 3.5, 1)").has_value());
    CHECK_EQ(seen, 1);
    CHECK(value > 3.4f && value < 3.6f);

    EventBus::unsubscribe(*sub);
    removeBindings(lua);
    lua.close();
    EventBus::reset();
}

// ---------------------------------------------------------------------------
// Moduł
// ---------------------------------------------------------------------------

TEST("Script: modul wola setup raz i loop w kazdym przebiegu") {
    freshBackend();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    static const char* kSource =
        "setups = 0 loops = 0\n"
        "function setup() setups = setups + 1 end\n"
        "function loop() loops = loops + 1 end\n";

    LuaEngine    engine;
    ScriptModule module;
    ScriptModule::Config cfg{};
    cfg.engine    = &engine;
    cfg.source    = kSource;
    cfg.pool      = nullptr;
    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    for (int i = 0; i < 5; ++i) module.step();

    captureOutput();
    CHECK(engine.interp().doString("print(setups, loops)").has_value());
    CHECK_STR(gCaptured, "1\t5");
    resetOutput();

    CHECK_EQ(static_cast<int>(module.stats().cycles), 5);
    CHECK_EQ(static_cast<int>(module.stats().loopRuns), 5);
    CHECK_EQ(static_cast<int>(module.stats().loopErrors), 0);

    module.stop();
    EventBus::reset();
}

TEST("Script: modul wylacza loop po serii bledow zamiast zalewac log") {
    freshBackend();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    static const char* kSource = "function loop() error('ciagle to samo') end\n";

    LuaEngine            engine;
    ScriptModule         module;
    ScriptModule::Config cfg{};
    cfg.engine               = &engine;
    cfg.source               = kSource;
    cfg.maxConsecutiveErrors = 3;
    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    for (int i = 0; i < 10; ++i) module.step();

    CHECK(module.loopStopped());
    // Po wyłączeniu `loop()` błędy przestają narastać.
    CHECK_EQ(static_cast<int>(module.stats().loopErrors), 3);

    module.stop();
    EventBus::reset();
}

TEST("Script: modul podmienia skrypt bez restartu") {
    freshBackend();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    static const char* kFirst  = "wersja = 1\nfunction loop() end\n";
    static const char* kSecond = "wersja = 2\nfunction loop() end\n";

    LuaEngine            engine;
    ScriptModule         module;
    ScriptModule::Config cfg{};
    cfg.engine = &engine;
    cfg.source = kFirst;
    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    captureOutput();
    CHECK(engine.interp().doString("print(wersja)").has_value());
    CHECK_STR(gCaptured, "1");
    resetOutput();

    REQUIRE(module.reload(kSecond).has_value());

    captureOutput();
    CHECK(engine.interp().doString("print(wersja)").has_value());
    CHECK_STR(gCaptured, "2");
    resetOutput();

    module.stop();
    EventBus::reset();
}

TEST("Script: modul wstaje mimo bledu w skrypcie") {
    freshBackend();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    static const char* kBroken = "function loop( -- brak domkniecia\n";

    LuaEngine            engine;
    ScriptModule         module;
    ScriptModule::Config cfg{};
    cfg.engine = &engine;
    cfg.source = kBroken;
    REQUIRE(module.configure(cfg).has_value());

    // Uszkodzony skrypt nie ma prawa zablokować startu urządzenia — moduł
    // wstaje, a poprawkę da się wgrać przez `script reload` w shellu.
    CHECK(module.init().has_value());
    CHECK(engine.interp().ready());

    module.step();
    CHECK_EQ(static_cast<int>(module.stats().loopRuns), 0);

    module.stop();
    EventBus::reset();
}
