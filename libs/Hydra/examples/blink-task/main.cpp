/**
 * Hydra — przykład: blink-task.
 *
 * Kryterium ukończenia etapu M1 (rozdz. 14): ten sam plik kompiluje się bez
 * zmian i zachowuje identycznie na ESP32-S3, ESP32-C3, RP2040, RP2350 i STM32.
 *
 * Pokazuje pełny szkielet aplikacji Hydry:
 *   - moduł z cyklem życia init/start/stop,
 *   - task okresowy z egzekwowanym okresem,
 *   - dostęp do sprzętu przez HAL, bez ani jednego wywołania Arduino,
 *   - nasłuch zdarzeń systemowych na magistrali.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
// Deklaracje setup() i loop(). Potrzebne, bo STM32duino umieszcza je w bloku
// extern "C" — bez tej deklaracji definicje poniżej dostają wiązanie C++
// i konsolidator ich nie znajduje. Na ESP32 i RP2040 deklaracje są zwykłe,
// więc włączenie niczego nie zmienia.
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("blink")

using namespace hydra;

namespace {

class BlinkModule : public ModuleBase {
public:
    BlinkModule() : ModuleBase("blink"), led_(hal::board::led) {}

protected:
    Status onInit() override {
        if (hal::board::led == hal::kNoPin) {
            // Płytka bez zwykłej diody — moduł nadal działa, tylko miga w logu.
            HYDRA_LOGW("płytka '%s' nie ma diody na GPIO — pozostaje log",
                       hal::board::name);
            return ok();
        }
        return led_.begin(hal::board::ledActiveLow);
    }

    Status onStart() override {
        Task::Cfg cfg;
        cfg.name = "blink.tick";
        cfg.prio = Prio::Low;
        cfg.core = Core::Core0;
        return task_.startPeriodic(cfg, 500, [this] { tick(); });
    }

    void onStop() override { task_.stopAndWait(); }

private:
    void tick() {
        on_ = !on_;
        if (hal::board::led != hal::kNoPin) {
            led_.set(hal::board::ledActiveLow ? !on_ : on_);
        }
        HYDRA_LOGI("dioda %s (iteracja %lu)", on_ ? "włączona" : "wyłączona",
                   static_cast<unsigned long>(task_.stats().iterations));
    }

    hal::OutputPin led_;
    Task           task_;
    bool           on_ = false;
};

BlinkModule  gBlink;
UartLogSink  gConsole;

}  // namespace

void setup() {
    App::config()
        .name("blink-demo")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .housekeepingMs(5000)
        .add(gBlink);

    // Podgląd zdarzeń rdzenia — pokazuje, że moduły komunikują się magistralą,
    // a nie bezpośrednimi wywołaniami.
    EventBus::subscribe<SysHeartbeat>([](const SysHeartbeat& e) {
        HYDRA_LOGI("puls: uptime %lus, wolna sterta %lu B, tasków %u",
                   static_cast<unsigned long>(e.uptimeMs / 1000),
                   static_cast<unsigned long>(e.freeHeapBytes),
                   static_cast<unsigned>(e.taskCount));
    });

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Nieużywane — cała praca dzieje się w taskach. Na STM32 ta funkcja nigdy
    // nie zostanie wywołana, bo App::begin() oddaje kontrolę schedulerowi.
}
