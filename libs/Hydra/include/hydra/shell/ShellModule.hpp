#pragma once
/**
 * Hydra — moduł obsługi shella na porcie szeregowym (rozdz. 13).
 *
 * Odpytuje port w tasku o najniższym priorytecie: shell nie może opóźnić
 * ani pętli sterowania, ani sieci. Przy braku znaków kosztuje jedno
 * sprawdzenie dostępności na cykl.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/IModule.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/shell/Shell.hpp"

namespace hydra {
namespace shell {

class ShellModule : public ModuleBase {
public:
    struct Config {
        /** Numer portu w rejestrze HAL. */
        u8   uartIndex = 0;
        u32  pollMs    = 50;
        Prio priority  = Prio::Idle;
        Core core      = Core::Any;
        /** Rejestruje komendy rdzenia i warstwy sprzętowej przy inicjalizacji. */
        bool builtinCommands = true;
        /** Wypisuje zachętę po starcie. */
        bool banner = true;
    };

    ShellModule() : ModuleBase("shell") {}

    Status configure(const Config& cfg);
    Shell& shell() { return shell_; }

    /** Jeden przebieg odczytu portu. Wystawiony publicznie do testów. */
    void step();

    u32 bytesRead() const { return bytesRead_; }

protected:
    Status onInit() override;
    Status onStart() override;
    void   onStop() override;

private:
    Shell  shell_;
    Config cfg_{};
    Task   task_;
    u32    bytesRead_ = 0;
};

}  // namespace shell
}  // namespace hydra
