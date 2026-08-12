/**
 * Hydra — implementacja cyklu życia aplikacji (rozdz. 4.1).
 *
 * Kolejność w begin() jest częścią kontraktu i nie wolno jej zmieniać:
 *   logi → magistrala zdarzeń → init() modułów → start() modułów →
 *   task core.house → SysStarted → (STM32) start schedulera.
 *
 * Nieudany init() lub start() cofa to, co już zostało uruchomione — urządzenie
 * nie zostaje w stanie połowicznie wystartowanym.
 */

#include "hydra/core/App.hpp"

#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/core/Version.hpp"

HYDRA_LOG_MODULE("core.app")

namespace hydra {
namespace {

struct AppState {
    App::Config cfg;
    Task        house;
    Millis      startedMs      = 0;
    bool        running        = false;
    u32         seenRingDrops  = 0;
};

AppState& st() {
    static AppState s;
    return s;
}

/** Najgorszy zapas stosu spośród tasków Hydry — 0, gdy platforma nie mierzy. */
u32 worstStackFree() {
    u32 worst = 0;
    bool any  = false;
    for (u8 i = 0; i < Task::registered(); ++i) {
        Task* t = Task::at(i);
        if (!t) continue;
        const u32 free = t->stats().stackFreeBytes;
        if (free == 0) continue;
        if (!any || free < worst) {
            worst = free;
            any   = true;
        }
    }
    return worst;
}

}  // namespace

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

App::Config& App::Config::add(IModule& m) {
    if (moduleCount_ >= HYDRA_MAX_MODULES) {
        HYDRA_LOGE("za dużo modułów (limit %d) — pominięto '%s'", HYDRA_MAX_MODULES, m.name());
        return *this;
    }
    for (u8 i = 0; i < moduleCount_; ++i) {
        if (modules_[i] == &m) return *this;  // rejestracja jest idempotentna
    }
    modules_[moduleCount_++] = &m;
    return *this;
}

App::Config& App::config() { return st().cfg; }

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status App::begin() {
    AppState& s = st();
    if (s.running) return fail(Err::AlreadyExists);

    Config& c = s.cfg;

    // 1. Logi — muszą działać, zanim cokolwiek innego zgłosi błąd.
    Log::init(c.logLevel_, c.logMode_);
    if (c.logSink_) Log::addSink(*c.logSink_);

    HYDRA_LOGI("Hydra %s / %s / urządzenie '%s'", HYDRA_VERSION_STR, HYDRA_PLATFORM_NAME,
               c.name_);

    // 2. Magistrala zdarzeń — moduły publikują już w init().
    if (auto r = EventBus::init(); !r) {
        HYDRA_LOGE("EventBus::init: %s", toString(r.error()));
        return r;
    }

    // 3. init() modułów w kolejności rejestracji.
    for (u8 i = 0; i < c.moduleCount_; ++i) {
        IModule* m = c.modules_[i];
        if (auto r = m->init(); !r) {
            HYDRA_LOGE("init modułu '%s': %s", m->name(), toString(r.error()));
            for (i8 j = static_cast<i8>(i) - 1; j >= 0; --j) c.modules_[j]->stop();
            return r;
        }
        HYDRA_LOGD("moduł '%s' zainicjalizowany", m->name());
    }

    // 4. start() — po tym kroku moduły mają własne taski.
    for (u8 i = 0; i < c.moduleCount_; ++i) {
        IModule* m = c.modules_[i];
        if (auto r = m->start(); !r) {
            HYDRA_LOGE("start modułu '%s': %s", m->name(), toString(r.error()));
            for (i8 j = static_cast<i8>(i); j >= 0; --j) c.modules_[j]->stop();
            return r;
        }
        HYDRA_LOGI("moduł '%s' uruchomiony", m->name());
    }

    // 5. core.house — najniższy priorytet, dowolny rdzeń (rozdz. 4.2).
    //
    // Okres 0 znaczy „bez własnego taska": aplikacja ma własną pętlę i woła
    // App::housekeeping() sama. Tak działa cel przeglądarkowy — w emscriptenie
    // każdy wątek to Web Worker, a te wymagają SharedArrayBuffer, czyli
    // nagłówków COOP/COEP na serwerze. Te z kolei odcinają stronie wszystkie
    // zasoby cross-origin, więc jeden task porządkowy potrafi narzucić
    // warunki hostingu całej aplikacji, w której osadzony jest podgląd.
    if (c.houseMs_ > 0) {
        Task::Cfg hc;
        hc.name       = "core.house";
        hc.prio       = Prio::Idle;
        hc.core       = Core::Any;
        hc.stackWords = HYDRA_DEFAULT_STACK;
        if (auto r = s.house.startPeriodic(hc, c.houseMs_, [] { App::housekeeping(); }); !r) {
            HYDRA_LOGE("core.house: %s", toString(r.error()));
            for (i8 j = static_cast<i8>(c.moduleCount_) - 1; j >= 0; --j) c.modules_[j]->stop();
            return r;
        }
    }

    s.startedMs = rtos::nowMs();
    s.running   = true;

    EventBus::publish(SysStarted{s.startedMs, ResetReason::Unknown, c.moduleCount_});
    HYDRA_LOGI("start zakończony w %lums, modułów: %u",
               static_cast<unsigned long>(s.startedMs), static_cast<unsigned>(c.moduleCount_));

    // 6. Na stm32duino scheduler startuje dopiero tutaj i nigdy nie oddaje kontroli.
    if (HYDRA_MANUAL_SCHEDULER) {
        rtos::startScheduler();
        HYDRA_LOGE("scheduler zakończył pracę — brak pamięci na taski?");
        return fail(Err::OutOfMemory);
    }
    return ok();
}

void App::stop() {
    AppState& s = st();
    if (!s.running) return;

    if (s.cfg.houseMs_ > 0) s.house.stopAndWait(2 * s.cfg.houseMs_ + 100);

    for (i8 i = static_cast<i8>(s.cfg.moduleCount_) - 1; i >= 0; --i) {
        s.cfg.modules_[i]->stop();
        HYDRA_LOGI("moduł '%s' zatrzymany", s.cfg.modules_[i]->name());
    }
    s.running = false;
}

void App::housekeeping() {
    AppState& s = st();

    // Zdarzenia odłożone z przerwań rozgłaszamy w kontekście taska (rozdz. 4.3).
    EventBus::drainIsr();

    // Wypchnięcie logów zbuforowanych w trybie Deferred.
    Log::drain();

    // Watchdog karmiony wyłącznie przy pozytywnym teście zdrowia (rozdz. 13).
    if (s.cfg.wdtFeed_) {
        const bool healthy = s.cfg.health_ ? s.cfg.health_() : true;
        if (healthy) s.cfg.wdtFeed_();
    }

    const auto logStats = Log::stats();
    if (logStats.ringDropped > s.seenRingDrops) {
        EventBus::publish(LogOverflow{logStats.ringDropped - s.seenRingDrops});
        s.seenRingDrops = logStats.ringDropped;
    }

    if (s.cfg.heartbeat_) {
        EventBus::publish(SysHeartbeat{uptimeMs(), rtos::freeHeapBytes(), worstStackFree(),
                                       static_cast<u16>(Task::registered())});
    }
}

bool        App::running()     { return st().running; }
const char* App::deviceName()  { return st().cfg.name_; }
u8          App::moduleCount() { return st().cfg.moduleCount_; }

Millis App::uptimeMs() {
    return rtos::nowMs();
}

IModule* App::module(u8 index) {
    AppState& s = st();
    return index < s.cfg.moduleCount_ ? s.cfg.modules_[index] : nullptr;
}

IModule* App::findModule(const char* name) {
    if (!name) return nullptr;
    AppState& s = st();
    for (u8 i = 0; i < s.cfg.moduleCount_; ++i) {
        if (strcmp(s.cfg.modules_[i]->name(), name) == 0) return s.cfg.modules_[i];
    }
    return nullptr;
}

void App::reset() {
    stop();
    AppState& s   = st();
    s.cfg         = Config{};
    s.startedMs   = 0;
    s.seenRingDrops = 0;
    EventBus::reset();
    EventBus::shutdown();
    Log::reset();
}

}  // namespace hydra
