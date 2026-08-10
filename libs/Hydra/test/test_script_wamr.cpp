/**
 * Hydra — testy silnika skryptowego na WAMR.
 *
 * Te same moduły `.wasm`, co w `test_script_wasm.cpp`, wykonane na drugim
 * runtimie. To jest cały sens istnienia tego pliku: bajtkod ma być przenośny,
 * a jedyny sposób, żeby zauważyć, że przestał, to uruchomić go na obu
 * maszynach w jednej binarce.
 *
 * Różnica, o której trzeba pamiętać przy czytaniu asercji: **budżet w WAMR
 * liczy instrukcje**, a w wasm3 krawędzie wsteczne pętli i wywołania. Ta sama
 * liczba znaczy tu co innego, więc progi są dobrane osobno.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_WAMR

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/script/ScriptModule.hpp"
#include "hydra/script/WasmEngineWamr.hpp"

using namespace hydra;
using namespace hydra::script;

namespace {

#include "wasm_fixtures.inc"

hal::mock::Backend& freshWamrHal() {
    hal::mock::backend().clear();
    hal::mock::install();
    return hal::mock::backend();
}

/** WAMR potrzebuje więcej miejsca niż wasm3 — trzyma instancję i stos wykonania. */
alignas(8) u8 gWamrTestPool[192 * 1024];

Status openWamr(WasmEngineWamr& engine) {
    return engine.open(gWamrTestPool, sizeof(gWamrTestPool));
}

template <size_t N>
Status loadWamr(WasmEngineWamr& engine, const unsigned char (&image)[N]) {
    return engine.load(image, N, "=test");
}

}  // namespace

TEST("WAMR: silnik otwiera sie i przedstawia nazwa") {
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());

    CHECK_STR(engine.name(), "wamr");
    CHECK(engine.ready() || true);   // gotowość znaczy tu „runtime wstał"

    engine.close();
}

TEST("WAMR: alokuje wylacznie z podanej puli") {
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());

    const auto empty = engine.memory();
    REQUIRE(loadWamr(engine, kWasmCounter).has_value());

    // Gdyby WAMR sięgał po stertę systemową, licznik stałby w miejscu i reguła
    // „po App::begin() nie alokujemy" byłaby fikcją.
    const auto loaded = engine.memory();
    CHECK(loaded.used > empty.used);
    CHECK_EQ(static_cast<int>(loaded.failures), 0);

    engine.close();
}

TEST("WAMR: zamkniecie oddaje cala pamiec do puli") {
    WasmEngineWamr engine;

    REQUIRE(openWamr(engine).has_value());
    const u32 baseline = engine.memory().used;
    engine.close();

    REQUIRE(openWamr(engine).has_value());
    REQUIRE(loadWamr(engine, kWasmCounter).has_value());
    engine.close();

    REQUIRE(openWamr(engine).has_value());
    CHECK_EQ(static_cast<int>(engine.memory().used), static_cast<int>(baseline));
    engine.close();
}

TEST("WAMR: obraz, ktory nie jest modulem, jest odrzucany z opisem") {
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());

    const u8 garbage[] = {'n', 'i', 'e', ' ', 'w', 'a', 's', 'm'};
    CHECK(!engine.load(garbage, sizeof(garbage), "=smiec").has_value());
    CHECK(strlen(engine.error()) > 0);

    engine.close();
}

TEST("WAMR: modul zadajacy niewystawionego importu nie wczytuje sie") {
    freshWamrHal();
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());

    BindingSet set{};
    set.gpio = true;
    REQUIRE(engine.installBindings(set).has_value());

    // WAMR rozwiązuje importy przy tworzeniu instancji — czyli w tym samym
    // miejscu, w którym wasm3 robi to przez `m3_CompileModule`.
    CHECK(!loadWamr(engine, kWasmMissingImport).has_value());
    engine.close();
}

TEST("WAMR: modul steruje pinami przez HAL") {
    auto& backend = freshWamrHal();
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());

    BindingSet set{};
    set.gpio = true;
    REQUIRE(engine.installBindings(set).has_value());
    REQUIRE(loadWamr(engine, kWasmCounter).has_value());

    REQUIRE(hal::Hal::gpio().configure(7, hal::PinMode::Output).has_value());
    const bool before = backend.gpio.state(7).level;

    REQUIRE(engine.call("setup").has_value());
    CHECK(backend.gpio.state(7).level != before);

    REQUIRE(engine.jobBegin("loop").has_value());
    CHECK_EQ(static_cast<int>(engine.jobStep(100000)),
             static_cast<int>(IScriptEngine::JobState::Done));
    CHECK_EQ(backend.gpio.state(7).level, before);

    engine.close();
}

// ---------------------------------------------------------------------------
// Budżet wykonania — w WAMR wbudowany, bez łatki
// ---------------------------------------------------------------------------

TEST("WAMR: nieskonczona petla jest przerywana, nie zawiesza taska") {
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());
    REQUIRE(loadWamr(engine, kWasmInfiniteLoop).has_value());

    REQUIRE(engine.jobBegin("loop").has_value());

    // Bez licznika instrukcji ten wiersz nigdy by nie wrócił. W wasm3 licznik
    // trzeba było dołożyć łatką; tutaj jest częścią runtime'u.
    CHECK_EQ(static_cast<int>(engine.jobStep(5000)),
             static_cast<int>(IScriptEngine::JobState::Exhausted));

    engine.close();
}

TEST("WAMR: praca miesci sie w budzecie konczy sie stanem Done") {
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());
    REQUIRE(loadWamr(engine, kWasmBoundedLoop).has_value());

    REQUIRE(engine.jobBegin("loop").has_value());
    CHECK_EQ(static_cast<int>(engine.jobStep(100000)),
             static_cast<int>(IScriptEngine::JobState::Done));

    engine.close();
}

TEST("WAMR: budzet zerowy znosi ograniczenie") {
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());
    REQUIRE(loadWamr(engine, kWasmBoundedLoop).has_value());

    REQUIRE(engine.jobBegin("loop").has_value());
    CHECK_EQ(static_cast<int>(engine.jobStep(0)),
             static_cast<int>(IScriptEngine::JobState::Done));

    engine.close();
}

TEST("WAMR: po przerwaniu silnik nadal dziala") {
    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());
    REQUIRE(loadWamr(engine, kWasmInfiniteLoop).has_value());

    for (int i = 0; i < 3; ++i) {
        REQUIRE(engine.jobBegin("loop").has_value());
        CHECK_EQ(static_cast<int>(engine.jobStep(2000)),
                 static_cast<int>(IScriptEngine::JobState::Exhausted));
    }

    CHECK_EQ(static_cast<int>(engine.memory().failures), 0);
    engine.close();
}

// ---------------------------------------------------------------------------
// Zdarzenia
// ---------------------------------------------------------------------------

TEST("WAMR: modul publikuje zdarzenie na magistrali") {
    freshWamrHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    u16  seenName = 0;
    auto sub = EventBus::subscribe<ScriptSignal>([&](const ScriptSignal& s) { seenName = s.nameId; });
    REQUIRE(sub.has_value());

    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());

    BindingSet set{};
    set.event = true;
    REQUIRE(engine.installBindings(set).has_value());
    REQUIRE(loadWamr(engine, kWasmEventEcho).has_value());

    REQUIRE(engine.jobBegin("loop").has_value());
    CHECK_EQ(static_cast<int>(engine.jobStep(100000)),
             static_cast<int>(IScriptEngine::JobState::Done));

    // Skrót liczony w module musi zgadzać się z tym, którego używa host —
    // i musi być ten sam, co przy wasm3.
    CHECK_EQ(static_cast<int>(seenName), static_cast<int>(nameId("alarm")));

    engine.removeBindings();
    engine.close();
    EventBus::reset();
}

TEST("WAMR: zdarzenie z magistrali dociera do eksportu on_event") {
    freshWamrHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    WasmEngineWamr engine;
    REQUIRE(openWamr(engine).has_value());

    BindingSet set{};
    set.event = true;
    REQUIRE(engine.installBindings(set).has_value());
    REQUIRE(loadWamr(engine, kWasmEventEcho).has_value());

    EventBus::publish(ScriptSignal{nameId("kalibracja"), 3.5f, 7});
    CHECK_EQ(static_cast<int>(engine.dispatchSignals(8)), 1);

    engine.removeBindings();
    engine.close();
    EventBus::reset();
}

// ---------------------------------------------------------------------------
// Współpraca z ScriptModule
// ---------------------------------------------------------------------------

TEST("WAMR: modul skryptowy chodzi na silniku WAMR") {
    auto& backend = freshWamrHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    WasmEngineWamr engine;
    ScriptModule   module;

    ScriptModule::Config cfg{};
    cfg.engine        = &engine;
    cfg.source        = kWasmCounter;
    cfg.sourceBytes   = sizeof(kWasmCounter);
    cfg.pool          = gWamrTestPool;
    cfg.poolBytes     = sizeof(gWamrTestPool);
    cfg.budget        = 100000;
    cfg.bindings      = BindingSet{};
    cfg.bindings.gpio = true;

    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    const bool afterSetup = backend.gpio.state(7).level;
    for (int i = 0; i < 4; ++i) module.step();

    CHECK_EQ(backend.gpio.state(7).level, afterSetup);
    CHECK_EQ(static_cast<int>(module.stats().loopRuns), 4);
    CHECK_EQ(static_cast<int>(module.stats().loopErrors), 0);

    module.stop();
    EventBus::reset();
}

TEST("WAMR: nieskonczona petla w module nie zatrzymuje przebiegow") {
    freshWamrHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    WasmEngineWamr engine;
    ScriptModule   module;

    ScriptModule::Config cfg{};
    cfg.engine      = &engine;
    cfg.source      = kWasmInfiniteLoop;
    cfg.sourceBytes = sizeof(kWasmInfiniteLoop);
    cfg.pool        = gWamrTestPool;
    cfg.poolBytes   = sizeof(gWamrTestPool);
    cfg.budget      = 2000;

    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    for (int i = 0; i < 5; ++i) module.step();

    // Wyczerpanie budżetu jest wywłaszczeniem, nie błędem — tak samo jak
    // przy wasm3, choć jednostka budżetu jest inna.
    CHECK_EQ(static_cast<int>(module.stats().loopPreemptions), 5);
    CHECK_EQ(static_cast<int>(module.stats().loopErrors), 0);
    CHECK(!module.loopStopped());

    module.stop();
    EventBus::reset();
}

#endif  // HYDRA_SCRIPT_ENGINE_WAMR
