/**
 * hello-blink — mruganie diodą z taska okresowego.
 *
 * Najkrótszy kompletny program na Hydrze. Pokazuje trzy rzeczy, bez których
 * nie powstaje żadne urządzenie: moduł jako jednostkę cyklu życia, task
 * o zadanym okresie i pin nazwany w pliku płytki, a nie liczbą w kodzie.
 *
 * Mruganie w `loop()` byłoby krótsze — i nie dałoby się do niczego dołożyć.
 * Task ma własny priorytet i pomiar terminu, więc dodanie obok drugiego
 * zadania niczego tu nie zmienia.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
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
    BlinkModule() : ModuleBase("blink") {}

protected:
    Status onInit() override {
        if (hal::board::led == hal::kNoPin) {
            // Płytka bez diody to nie awaria — moduł po prostu nie ma czego
            // robić i mówi o tym raz, zamiast mrugać w pustkę.
            HYDRA_LOGW("płytka %s nie ma diody", hal::board::name);
            return ok();
        }
        return hal::Hal::gpio().configure(hal::board::led, hal::PinMode::Output);
    }

    Status onStart() override {
        if (hal::board::led == hal::kNoPin) return ok();

        Task::Cfg cfg;
        cfg.name = "blink.tick";
        cfg.prio = Prio::Low;
        return task_.startPeriodic(cfg, 500, [this] { toggle(); });
    }

    void onStop() override { task_.stopAndWait(); }

private:
    void toggle() {
        on_ = !on_;
        hal::Hal::gpio().write(hal::board::led, hal::board::ledActiveLow ? !on_ : on_);
    }

    Task task_;
    bool on_ = false;
};

BlinkModule gBlink;
UartLogSink gConsole;

}  // namespace

void setup() {
    App::config()
        .name("hello-blink")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gBlink);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }
    HYDRA_LOGI("płytka %s, dioda na %d", hal::board::name, static_cast<int>(hal::board::led));
}

void loop() {
    // Nieużywane: praca dzieje się w taskach.
}
