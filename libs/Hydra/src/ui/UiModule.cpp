/** Hydra — implementacja modułu interfejsu (rozdz. 6). */

#include "hydra/ui/UiModule.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("ui")

namespace hydra {
namespace ui {

Status UiModule::configure(const Config& cfg) {
    if (cfg.framePeriodMs == 0) return fail(Err::BadArgument);
    cfg_ = cfg;
    return ok();
}

Status UiModule::onInit() {
    HYDRA_CHECK(renderer_.init(display_, cfg_.renderer));
    HYDRA_CHECK(input_.begin());
    return ok();
}

Status UiModule::onStart() {
    Task::Cfg cfg;
    cfg.name       = "ui.render";
    cfg.prio       = cfg_.priority;
    cfg.core       = cfg_.core;
    cfg.stackWords = cfg_.stackWords;
    return task_.startPeriodic(cfg, cfg_.framePeriodMs, [this] { step(rtos::nowMs()); });
}

void UiModule::onStop() { task_.stopAndWait(); }

void UiModule::step(Millis now) {
    ++stats_.ticks;

    // Wejście przed rysowaniem: reakcja na dotyk trafia w tę samą klatkę,
    // a nie w następną. Przy 33 ms na klatkę różnica jest odczuwalna.
    stats_.inputEvents += input_.poll(now);

    if (auto r = renderer_.renderFrame(now); !r) {
        HYDRA_LOGW("klatka nieudana: %s", toString(r.error()));
    }
}

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
