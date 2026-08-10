/**
 * Hydra — testy silnika skryptowego WebAssembly.
 *
 * Najważniejszy przypadek w tym pliku to „pętla bez wyjścia nie zawiesza
 * urządzenia". Bez niego osadzanie WebAssembly w systemie czasu rzeczywistego
 * byłoby nieodpowiedzialne — dokładnie tak samo, jak przy Lua, tylko że tam
 * mechanizm daje sam interpreter, a tu trzeba go było dołożyć łatką (patrz
 * `src/wasm3/VENDOR.md`).
 *
 * Moduły `.wasm` są wpisane jako bajty, wygenerowane przez
 * `tools/gen_wasm_fixtures.py`. Testy nie mają prawa wymagać toolchaina.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_WASM

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/script/ScriptModule.hpp"
#include "hydra/script/WasmEngine.hpp"

using namespace hydra;
using namespace hydra::script;

namespace {

#include "wasm_fixtures.inc"

/** Świeży atrapowy backend HAL — importy modułu sięgają po GPIO. */
hal::mock::Backend& freshMockHal() {
    hal::mock::backend().clear();
    hal::mock::install();
    return hal::mock::backend();
}

/** Silnik otwarty na własnej puli, żeby testy nie biły się o domyślną. */
alignas(8) u8 gWasmTestPool[96 * 1024];

Status openEngine(WasmEngine& engine) {
    return engine.open(gWasmTestPool, sizeof(gWasmTestPool));
}

template <size_t N>
Status loadModule(WasmEngine& engine, const unsigned char (&image)[N], const char* name = "=test") {
    return engine.load(image, N, name);
}

}  // namespace

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

TEST("WASM: silnik otwiera sie i przedstawia nazwa") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    CHECK_STR(engine.name(), "wasm3");
    CHECK(engine.ready());

    engine.close();
    CHECK(!engine.ready());
}

TEST("WASM: silnik alokuje wylacznie z podanej puli") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    const auto empty = engine.memory();
    CHECK_EQ(static_cast<int>(empty.failures), 0);

    REQUIRE(loadModule(engine, kWasmCounter).has_value());

    // Wczytanie modułu musi być widoczne w naszej puli — gdyby wasm3 sięgał
    // po stertę systemową, licznik stałby w miejscu i cała reguła „po
    // App::begin() nie alokujemy" byłaby fikcją.
    const auto loaded = engine.memory();
    CHECK(loaded.used > empty.used);
    CHECK(loaded.used < loaded.capacity);
    CHECK_EQ(static_cast<int>(loaded.failures), 0);

    engine.close();
}

TEST("WASM: zamkniecie oddaje cala pamiec do puli") {
    WasmEngine engine;

    // Punkt odniesienia: pusty silnik zaraz po otwarciu. Nie zero — środowisko
    // i runtime wasm3 też siedzą w tej puli.
    REQUIRE(openEngine(engine).has_value());
    const u32 baseline = engine.memory().used;
    engine.close();

    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmCounter).has_value());
    engine.close();

    // Po zamknięciu pula musi wrócić do punktu wyjścia, inaczej każdy
    // `reload()` zjadałby kawałek — a to jest dokładnie ten scenariusz, dla
    // którego nie użyliśmy wbudowanego d_m3FixedHeap wasm3.
    REQUIRE(openEngine(engine).has_value());
    CHECK_EQ(static_cast<int>(engine.memory().used), static_cast<int>(baseline));
    engine.close();
}

TEST("WASM: kolejne wczytania nie kumuluja zuzycia pamieci") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmCounter).has_value());
    const u32 afterFirst = engine.memory().used;

    // Pięć podmian z rzędu — tyle, ile zrobi zdalna aktualizacja skryptu
    // w ciągu jednej sesji.
    for (int i = 0; i < 5; ++i) {
        engine.close();
        REQUIRE(openEngine(engine).has_value());
        REQUIRE(loadModule(engine, kWasmCounter).has_value());
    }

    CHECK_EQ(static_cast<int>(engine.memory().used), static_cast<int>(afterFirst));
    engine.close();
}

// ---------------------------------------------------------------------------
// Wczytywanie modułu
// ---------------------------------------------------------------------------

TEST("WASM: obraz, ktory nie jest modulem, jest odrzucany z opisem") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    const u8 garbage[] = {'n', 'i', 'e', ' ', 'w', 'a', 's', 'm'};
    auto     r = engine.load(garbage, sizeof(garbage), "=smiec");

    CHECK(!r);
    CHECK(strlen(engine.error()) > 0);
    engine.close();
}

TEST("WASM: pusty obraz jest bledem argumentu") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    CHECK(!engine.load(nullptr, 0, "=puste").has_value());
    engine.close();
}

TEST("WASM: modul zadajacy niewystawionego importu nie wczytuje sie") {
    freshMockHal();
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    BindingSet set{};
    set.gpio = true;
    REQUIRE(engine.installBindings(set).has_value());

    // Grupa i2c nie jest w WASM wystawiona. Moduł, który jej żąda, ma się nie
    // wczytać — połowicznie działający moduł byłby gorszy niż jasny błąd.
    CHECK(!loadModule(engine, kWasmMissingImport).has_value());
    engine.close();
}

TEST("WASM: hasFunction widzi eksporty modulu") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmCounter).has_value());

    CHECK(engine.hasFunction("setup"));
    CHECK(engine.hasFunction("loop"));
    CHECK(!engine.hasFunction("czegoTakiegoNieMa"));

    engine.close();
}

TEST("WASM: modul bez loop() jest poprawny") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmNoLoop).has_value());

    CHECK(engine.hasFunction("setup"));
    CHECK(!engine.hasFunction("loop"));
    CHECK(engine.call("setup").has_value());

    engine.close();
}

// ---------------------------------------------------------------------------
// Importy — droga z modułu do sprzętu
// ---------------------------------------------------------------------------

TEST("WASM: modul steruje pinami przez HAL") {
    auto& backend = freshMockHal();
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    BindingSet set{};
    set.gpio = true;
    REQUIRE(engine.installBindings(set).has_value());
    REQUIRE(loadModule(engine, kWasmCounter).has_value());

    // `toggle` czyta stan przed zapisem, więc pin musi być wyjściem.
    REQUIRE(hal::Hal::gpio().configure(7, hal::PinMode::Output).has_value());
    const bool before = backend.gpio.state(7).level;

    REQUIRE(engine.call("setup").has_value());
    CHECK(backend.gpio.state(7).level != before);

    // Każdy przebieg przełącza pin, więc atrapa widzi dokładnie tyle zmian,
    // ile było wywołań — to jest dowód, że droga modul → import → HAL działa.
    REQUIRE(engine.jobBegin("loop").has_value());
    CHECK_EQ(static_cast<int>(engine.jobStep(1000)),
             static_cast<int>(IScriptEngine::JobState::Done));
    CHECK_EQ(backend.gpio.state(7).level, before);

    engine.close();
}

TEST("WASM: grupa wylaczona w BindingSet nie jest linkowana") {
    freshMockHal();
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    BindingSet set{};
    set.gpio = false;   // moduł żąda gpio_toggle, a my go nie dajemy
    REQUIRE(engine.installBindings(set).has_value());

    CHECK(!loadModule(engine, kWasmCounter).has_value());
    engine.close();
}

// ---------------------------------------------------------------------------
// Budżet wykonania — sedno całej łatki
// ---------------------------------------------------------------------------

TEST("WASM: nieskonczona petla jest przerywana, nie zawiesza taska") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmInfiniteLoop).has_value());

    REQUIRE(engine.jobBegin("loop").has_value());

    // Gdyby łatka budżetu nie działała, ten wiersz nigdy by nie wrócił.
    const auto state = engine.jobStep(500);

    CHECK_EQ(static_cast<int>(state), static_cast<int>(IScriptEngine::JobState::Exhausted));
    // Wyczerpanie budżetu nie jest błędem i nie zostawia po sobie komunikatu
    // o awarii — moduł oddał procesor, a nie zawiódł.
    CHECK_EQ(static_cast<int>(engine.jobState()),
             static_cast<int>(IScriptEngine::JobState::Exhausted));

    engine.close();
}

TEST("WASM: praca miesci sie w budzecie konczy sie stanem Done") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmBoundedLoop).has_value());

    REQUIRE(engine.jobBegin("loop").has_value());
    CHECK_EQ(static_cast<int>(engine.jobStep(1000)),
             static_cast<int>(IScriptEngine::JobState::Done));

    // Dziesięć obrotów pętli to dziesięć krawędzi wstecznych — budżet zszedł
    // o tyle, a nie o liczbę instrukcji.
    CHECK(engine.jobSteps() > 0);
    CHECK(engine.jobSteps() < 100);

    engine.close();
}

TEST("WASM: budzet zerowy znosi ograniczenie") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmBoundedLoop).has_value());

    REQUIRE(engine.jobBegin("loop").has_value());
    CHECK_EQ(static_cast<int>(engine.jobStep(0)),
             static_cast<int>(IScriptEngine::JobState::Done));
    CHECK_EQ(static_cast<int>(engine.jobSteps()), 0);

    engine.close();
}

TEST("WASM: po przerwaniu silnik nadal dziala") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmInfiniteLoop).has_value());

    for (int i = 0; i < 3; ++i) {
        REQUIRE(engine.jobBegin("loop").has_value());
        CHECK_EQ(static_cast<int>(engine.jobStep(200)),
                 static_cast<int>(IScriptEngine::JobState::Exhausted));
    }

    // Trzy przerwania z rzędu nie mogą zostawić silnika w stanie, z którego
    // nie da się wyjść — pula ma być czysta, a błędów ma nie być.
    CHECK_EQ(static_cast<int>(engine.memory().failures), 0);
    engine.close();
}

TEST("WASM: jobBegin dla nieistniejacej funkcji zwraca NotFound") {
    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());
    REQUIRE(loadModule(engine, kWasmNoLoop).has_value());

    auto r = engine.jobBegin("loop");
    CHECK(!r);
    CHECK_EQ(r.error(), Err::NotFound);

    engine.close();
}

// ---------------------------------------------------------------------------
// Współpraca z ScriptModule
// ---------------------------------------------------------------------------

TEST("WASM: modul skryptowy chodzi na silniku WebAssembly") {
    auto& backend = freshMockHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    WasmEngine   engine;
    ScriptModule module;

    ScriptModule::Config cfg{};
    cfg.engine       = &engine;
    cfg.source       = kWasmCounter;
    cfg.sourceBytes  = sizeof(kWasmCounter);
    cfg.pool         = gWasmTestPool;
    cfg.poolBytes    = sizeof(gWasmTestPool);
    cfg.bindings     = BindingSet{};
    cfg.bindings.gpio = true;

    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    const bool afterSetup = backend.gpio.state(7).level;
    for (int i = 0; i < 4; ++i) module.step();

    // Cztery przebiegi, cztery przełączenia — czyli stan wraca do wyjściowego.
    CHECK_EQ(backend.gpio.state(7).level, afterSetup);
    CHECK_EQ(static_cast<int>(module.stats().loopRuns), 4);
    CHECK_EQ(static_cast<int>(module.stats().loopErrors), 0);

    module.stop();
    EventBus::reset();
}

TEST("WASM: nieskonczona petla w module nie zatrzymuje przebiegow") {
    freshMockHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    WasmEngine   engine;
    ScriptModule module;

    ScriptModule::Config cfg{};
    cfg.engine      = &engine;
    cfg.source      = kWasmInfiniteLoop;
    cfg.sourceBytes = sizeof(kWasmInfiniteLoop);
    cfg.pool        = gWasmTestPool;
    cfg.poolBytes   = sizeof(gWasmTestPool);
    cfg.budget      = 300;

    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    for (int i = 0; i < 5; ++i) module.step();

    // Wyczerpanie budżetu jest wywłaszczeniem, nie błędem: `loop()` nie może
    // zostać wyłączona przez maxConsecutiveErrors, bo moduł niczego nie zepsuł.
    CHECK_EQ(static_cast<int>(module.stats().loopPreemptions), 5);
    CHECK_EQ(static_cast<int>(module.stats().loopErrors), 0);
    CHECK(!module.loopStopped());

    module.stop();
    EventBus::reset();
}

// ---------------------------------------------------------------------------
// Zdarzenia — droga w obie strony
// ---------------------------------------------------------------------------

TEST("WASM: modul publikuje zdarzenie na magistrali") {
    freshMockHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    u16   seenName  = 0;
    float seenValue = 0.0f;
    auto  sub = EventBus::subscribe<ScriptSignal>([&](const ScriptSignal& s) {
        seenName  = s.nameId;
        seenValue = s.value;
    });
    REQUIRE(sub.has_value());

    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    BindingSet set{};
    set.event = true;
    REQUIRE(engine.installBindings(set).has_value());
    REQUIRE(loadModule(engine, kWasmEventEcho).has_value());

    REQUIRE(engine.jobBegin("loop").has_value());
    CHECK_EQ(static_cast<int>(engine.jobStep(1000)),
             static_cast<int>(IScriptEngine::JobState::Done));

    // Skrót liczony w module musi zgadzać się z tym, którego używa host —
    // inaczej sygnał dotarłby pod cudzą nazwą.
    CHECK_EQ(static_cast<int>(seenName), static_cast<int>(nameId("alarm")));
    CHECK(seenValue > 21.0f && seenValue < 22.0f);

    engine.removeBindings();
    engine.close();
    EventBus::reset();
}

TEST("WASM: zdarzenie z magistrali dociera do eksportu on_event") {
    freshMockHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    BindingSet set{};
    set.event = true;
    REQUIRE(engine.installBindings(set).has_value());
    REQUIRE(loadModule(engine, kWasmEventEcho).has_value());

    EventBus::publish(ScriptSignal{nameId("kalibracja"), 3.5f, 7});

    // Sygnał czeka w pierścieniu do czasu, aż zdejmie go task skryptu —
    // callback magistrali nie ma prawa wołać modułu w kontekście nadawcy.
    CHECK_EQ(static_cast<int>(engine.dispatchSignals(8)), 1);

    engine.removeBindings();
    engine.close();
    EventBus::reset();
}

TEST("WASM: modul bez on_event nie zapycha pierscienia sygnalow") {
    freshMockHal();
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    WasmEngine engine;
    REQUIRE(openEngine(engine).has_value());

    BindingSet set{};
    set.event = true;
    REQUIRE(engine.installBindings(set).has_value());
    // kWasmCounter nie eksportuje on_event.
    set.gpio = true;
    REQUIRE(engine.installBindings(set).has_value());
    REQUIRE(loadModule(engine, kWasmCounter).has_value());

    for (int i = 0; i < 3; ++i) EventBus::publish(ScriptSignal{nameId("cos"), 1.0f, 0});

    // Sygnały muszą zostać zdjęte mimo braku handlera, inaczej pierścień
    // zapchałby się i zaczął gubić zdarzenia adresowane do kogoś innego.
    CHECK_EQ(static_cast<int>(engine.dispatchSignals(8)), 3);
    CHECK_EQ(static_cast<int>(engine.dispatchSignals(8)), 0);

    engine.removeBindings();
    engine.close();
    EventBus::reset();
}

#endif  // HYDRA_SCRIPT_ENGINE_WASM
