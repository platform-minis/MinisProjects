/**
 * Szablon projektu na frameworku Hydra.
 *
 * Zawiera to, czego potrzebuje każde urządzenie, i nic ponadto: własny moduł
 * z taskiem okresowym, logowanie na port szeregowy oraz shell diagnostyczny.
 * Moduły opcjonalne włącza się flagą w platformio.ini i dopisuje tutaj —
 * miejsca są oznaczone.
 *
 * Po wgraniu wpisz w monitorze `help`, żeby zobaczyć dostępne komendy.
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
#include "hydra/shell/ShellModule.hpp"

HYDRA_LOG_MODULE("app")

using namespace hydra;

namespace {

/**
 * Moduł aplikacji. Każdy podsystem urządzenia to osobny moduł — dzięki temu
 * kolejność uruchamiania jest deterministyczna, a pojedynczy podsystem da się
 * zrestartować bez restartu całości.
 */
class AppModule : public ModuleBase {
public:
    AppModule() : ModuleBase("app") {}

protected:
    Status onInit() override {
        // Konfiguracja i alokacje. Bez tasków i bez pracy w tle.
        return ok();
    }

    Status onStart() override {
        Task::Cfg cfg;
        cfg.name = "app.tick";
        cfg.prio = Prio::Normal;
        return task_.startPeriodic(cfg, 1000, [this] { tick(); });
    }

    void onStop() override { task_.stopAndWait(); }

private:
    void tick() {
        ++counter_;
        HYDRA_LOGI("tyknięcie %lu", static_cast<unsigned long>(counter_));
    }

    Task task_;
    u32  counter_ = 0;
};

AppModule           gApp;
shell::ShellModule  gShell;
UartLogSink         gConsole;

}  // namespace

void setup() {
    App::config()
        .name("moje-urzadzenie")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gApp)
        .add(gShell);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }

    // Podgląd zdarzeń rdzenia. Moduły komunikują się magistralą, a nie
    // bezpośrednimi wywołaniami — subskrypcja to jedyne, czego potrzeba.
    EventBus::subscribe<SysHeartbeat>([](const SysHeartbeat& e) {
        HYDRA_LOGD("puls: %lus, wolna sterta %lu B",
                   static_cast<unsigned long>(e.uptimeMs / 1000),
                   static_cast<unsigned long>(e.freeHeapBytes));
    });

    HYDRA_LOGI("gotowe — wpisz 'help'");
}

void loop() {
    // Nieużywane: cała praca dzieje się w taskach. Na STM32 ta funkcja
    // nigdy nie zostanie wywołana.
}
