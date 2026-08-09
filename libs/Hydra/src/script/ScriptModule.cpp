/**
 * Hydra — moduł skryptowy.
 *
 * Cała nietrywialna logika siedzi w `step()`: obsłużyć sygnały z magistrali,
 * a potem popchnąć `loop()` o jeden budżet instrukcji do przodu. Reszta to
 * cykl życia i sprzątanie.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/ScriptModule.hpp"

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("script")

namespace hydra {
namespace script {

Status ScriptModule::configure(const Config& cfg) {
    if (state() == ModuleState::Running) return fail(Err::Busy);
    cfg_ = cfg;
    return ok();
}

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status ScriptModule::onInit() {
    Interp::Config icfg{};
    icfg.libs      = cfg_.libs;
    icfg.pool      = cfg_.pool;
    icfg.poolBytes = cfg_.poolBytes;
    HYDRA_CHECK(interp_.open(icfg));
    HYDRA_CHECK(installBindings(interp_, cfg_.bindings));

    if (cfg_.source != nullptr) {
        auto loaded = loadSource(cfg_.source, cfg_.chunkName);
        if (!loaded) {
            // Błąd w skrypcie nie ma prawa zablokować startu urządzenia.
            // Moduł wstaje bez skryptu, log mówi dlaczego, a `lua reload`
            // w shellu pozwala poprawić rzecz bez przekompilowania wsadu.
            HYDRA_LOGE("skrypt nie wczytany: %s", interp_.error());
        }
    }

    const auto mem = interp_.memory();
    HYDRA_LOGI("interpreter gotowy, pamiec %u/%u B", mem.used, mem.capacity);
    return ok();
}

Status ScriptModule::loadSource(const char* source, const char* chunkName) {
    loaded_            = false;
    consecutiveErrors_ = 0;
    loopStopped_       = false;

    HYDRA_CHECK(interp_.doString(source, chunkName));
    loaded_ = true;

    if (cfg_.callSetup && interp_.hasFunction("setup")) {
        auto r = interp_.callGlobal("setup");
        if (!r) {
            HYDRA_LOGE("setup(): %s", interp_.error());
            return r;
        }
    }
    return ok();
}

Status ScriptModule::reload() {
    if (cfg_.source == nullptr) return fail(Err::NotFound);
    return reload(cfg_.source, cfg_.chunkName);
}

Status ScriptModule::reload(const char* source, const char* chunkName) {
    if (source == nullptr) return fail(Err::BadArgument);
    job_.cancel();
    removeBindings(interp_);
    interp_.close();

    Interp::Config icfg{};
    icfg.libs      = cfg_.libs;
    icfg.pool      = cfg_.pool;
    icfg.poolBytes = cfg_.poolBytes;
    HYDRA_CHECK(interp_.open(icfg));
    HYDRA_CHECK(installBindings(interp_, cfg_.bindings));

    cfg_.source    = source;
    cfg_.chunkName = chunkName;
    stats_         = Stats{};
    return loadSource(source, chunkName);
}

Status ScriptModule::onStart() {
    Task::Cfg tcfg{};
    tcfg.name       = "script.run";
    tcfg.prio       = cfg_.priority;
    tcfg.core       = cfg_.core;
    tcfg.stackWords = cfg_.stackWords;

    return task_.startPeriodic(tcfg, cfg_.periodMs, [this] { step(); });
}

void ScriptModule::onStop() {
    task_.stopAndWait();
    job_.cancel();
    removeBindings(interp_);
    interp_.close();
    loaded_ = false;
}

// ---------------------------------------------------------------------------
// Przebieg
// ---------------------------------------------------------------------------

void ScriptModule::step() {
    if (!interp_.ready()) return;
    ++stats_.cycles;

    // Najpierw zdarzenia: skrypt reagujący na sygnał ma to zrobić w tym samym
    // przebiegu, w którym sygnał dotarł, a nie po zakończeniu bieżącej porcji
    // `loop()`, która może zająć jeszcze kilka przebiegów.
    if (cfg_.bindings.event && cfg_.signalsPerCycle > 0) {
        stats_.signalsHandled += dispatchSignals(interp_, cfg_.signalsPerCycle);
    }

    if (!loaded_ || !cfg_.callLoop || loopStopped_) return;

    if (job_.state() != Job::State::Running) {
        if (!interp_.hasFunction("loop")) return;
        auto started = job_.start(interp_, "loop");
        if (!started) return;
    }

    const auto before = job_.steps();
    const auto result = job_.resume(cfg_.budget);
    stats_.instructions += job_.steps() - before;

    switch (result) {
        case Job::State::Running:
            // Budżet wyczerpany w środku `loop()`. To nie jest błąd — to jest
            // dokładnie ten mechanizm, dla którego budżet istnieje.
            ++stats_.loopPreemptions;
            break;

        case Job::State::Done:
            ++stats_.loopRuns;
            consecutiveErrors_ = 0;
            break;

        case Job::State::Failed:
            ++stats_.loopErrors;
            HYDRA_LOGE("loop(): %s", interp_.error());
            if (cfg_.maxConsecutiveErrors > 0 &&
                ++consecutiveErrors_ >= cfg_.maxConsecutiveErrors) {
                loopStopped_ = true;
                HYDRA_LOGE("loop() wylaczona po %u bledach z rzedu", consecutiveErrors_);
                EventBus::publish(SysDegraded{nameId("script"), 1});
            }
            break;

        case Job::State::Idle:
            break;
    }
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
