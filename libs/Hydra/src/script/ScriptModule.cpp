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

#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("script")

namespace hydra {
namespace script {

Status ScriptModule::configure(const Config& cfg) {
    if (state() == ModuleState::Running) return fail(Err::Busy);
    cfg_ = cfg;
    // Silnik zapamiętany już tutaj, a nie dopiero w `onInit()`: komendy shella
    // rejestruje się zwykle przed startem aplikacji i muszą mieć w co celować.
    engine_ = cfg.engine;
    return ok();
}

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status ScriptModule::onInit() {
    if (engine_ == nullptr) {
        HYDRA_LOGE("brak silnika skryptowego w konfiguracji");
        return fail(Err::BadArgument);
    }

    HYDRA_CHECK(engine_->open(cfg_.pool, cfg_.poolBytes));
    HYDRA_CHECK(engine_->installBindings(cfg_.bindings));

    if (cfg_.source != nullptr) {
        auto loaded = loadImage(cfg_.source, cfg_.sourceBytes, cfg_.chunkName);
        if (!loaded) {
            // Błąd w skrypcie nie ma prawa zablokować startu urządzenia.
            // Moduł wstaje bez skryptu, log mówi dlaczego, a `script reload`
            // w shellu pozwala poprawić rzecz bez przekompilowania wsadu.
            HYDRA_LOGE("skrypt nie wczytany: %s", engine_->error());
        }
    }

    const auto mem = engine_->memory();
    HYDRA_LOGI("silnik %s gotowy, pamiec %u/%u B", engine_->name(), mem.used, mem.capacity);
    return ok();
}

Status ScriptModule::loadImage(const void* image, size_t bytes, const char* name) {
    loaded_            = false;
    consecutiveErrors_ = 0;
    loopStopped_       = false;

    HYDRA_CHECK(engine_->load(image, bytes, name));
    loaded_ = true;

    if (cfg_.callSetup && engine_->hasFunction("setup")) {
        auto r = engine_->call("setup");
        if (!r) {
            HYDRA_LOGE("setup(): %s", engine_->error());
            return r;
        }
    }
    return ok();
}

CByteSpan ScriptModule::image() const {
    if (cfg_.source == nullptr) return CByteSpan{};
    if (cfg_.sourceBytes > 0) {
        return CByteSpan{static_cast<const u8*>(cfg_.source), cfg_.sourceBytes};
    }
    // Tekst: długość razem z zerem kończącym, żeby wynik dało się podać wprost
    // do `reload()` — silnik tekstowy wymaga terminatora i sprawdza go.
    const char* text = static_cast<const char*>(cfg_.source);
    return CByteSpan{cfg_.source ? static_cast<const u8*>(cfg_.source) : nullptr,
                     strlen(text) + 1};
}

Status ScriptModule::reload() {
    if (cfg_.source == nullptr) return fail(Err::NotFound);
    return reload(cfg_.source, cfg_.sourceBytes, cfg_.chunkName);
}

Status ScriptModule::reload(const char* source, const char* chunkName) {
    return reload(static_cast<const void*>(source), 0, chunkName);
}

Status ScriptModule::reload(const void* image, size_t bytes, const char* name) {
    if (image == nullptr) return fail(Err::BadArgument);
    if (engine_ == nullptr) return fail(Err::NotInitialized);

    engine_->jobCancel();
    engine_->removeBindings();
    engine_->close();

    HYDRA_CHECK(engine_->open(cfg_.pool, cfg_.poolBytes));
    HYDRA_CHECK(engine_->installBindings(cfg_.bindings));

    cfg_.source      = image;
    cfg_.sourceBytes = bytes;
    cfg_.chunkName   = name;
    stats_           = Stats{};
    return loadImage(image, bytes, name);
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
    if (engine_ != nullptr) {
        engine_->jobCancel();
        engine_->removeBindings();
        engine_->close();
    }
    loaded_ = false;
}

// ---------------------------------------------------------------------------
// Przebieg
// ---------------------------------------------------------------------------

void ScriptModule::step() {
    if (engine_ == nullptr || !engine_->ready()) return;
    ++stats_.cycles;

    // Najpierw zdarzenia: skrypt reagujący na sygnał ma to zrobić w tym samym
    // przebiegu, w którym sygnał dotarł, a nie po zakończeniu bieżącej porcji
    // `loop()`, która może zająć jeszcze kilka przebiegów.
    if (cfg_.bindings.event && cfg_.signalsPerCycle > 0) {
        stats_.signalsHandled += engine_->dispatchSignals(cfg_.signalsPerCycle);
    }

    if (!loaded_ || !cfg_.callLoop || loopStopped_) return;

    using JobState = IScriptEngine::JobState;

    if (engine_->jobState() != JobState::Running) {
        if (!engine_->hasFunction("loop")) return;
        auto started = engine_->jobBegin("loop");
        if (!started) return;
    }

    const auto before = engine_->jobSteps();
    const auto result = engine_->jobStep(cfg_.budget);
    stats_.instructions += engine_->jobSteps() - before;

    switch (result) {
        case JobState::Running:
            // Budżet wyczerpany w środku `loop()`. To nie jest błąd — to jest
            // dokładnie ten mechanizm, dla którego budżet istnieje.
            ++stats_.loopPreemptions;
            break;

        case JobState::Done:
            ++stats_.loopRuns;
            consecutiveErrors_ = 0;
            break;

        case JobState::Failed:
            ++stats_.loopErrors;
            HYDRA_LOGE("loop(): %s", engine_->error());
            if (cfg_.maxConsecutiveErrors > 0 &&
                ++consecutiveErrors_ >= cfg_.maxConsecutiveErrors) {
                loopStopped_ = true;
                HYDRA_LOGE("loop() wylaczona po %u bledach z rzedu", consecutiveErrors_);
                EventBus::publish(SysDegraded{nameId("script"), 1});
            }
            break;

        case JobState::Idle:
            break;
    }
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
