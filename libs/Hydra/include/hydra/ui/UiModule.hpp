#pragma once
/**
 * Hydra — moduł interfejsu użytkownika (rozdz. 6, 4.2).
 *
 * Tworzy task ui.render i spina w nim trzy rzeczy: odczyt wejścia, wykonanie
 * poleceń z kolejki i przerysowanie zmienionego obszaru. Task ma niski
 * priorytet i siedzi na rdzeniu 0 — razem z siecią, z dala od pętli sterowania
 * (rozdz. 10). Interfejs, który się zatnie, nie może opóźnić regulatora.
 *
 * Okres domyślny to 33 ms, czyli około 30 klatek na sekundę. Przy włączonym
 * pomijaniu klatek bez zmian nieruchomy ekran nie kosztuje praktycznie nic,
 * więc gęstsze odpytywanie podnosi jedynie reakcję na dotyk.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/IModule.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/ui/IInput.hpp"
#include "hydra/ui/Renderer.hpp"

namespace hydra {
namespace ui {

class UiModule : public ModuleBase {
public:
    struct Config {
        /** Okres taska ui.render; 33 ms to około 30 klatek na sekundę. */
        u32              framePeriodMs = 33;
        Prio             priority      = Prio::Low;
        Core             core          = Core::Core0;
        u32              stackWords    = HYDRA_DEFAULT_STACK;
        Renderer::Config renderer{};
    };

    struct Stats {
        u32 ticks       = 0;
        u32 inputEvents = 0;
    };

    explicit UiModule(IDisplayBackend& display) : ModuleBase("ui"), display_(display) {}

    Status configure(const Config& cfg);

    /** Urządzenia wejściowe. Podłączać przed App::begin(). */
    void attachPointer(IPointerDevice& d) { input_.attachPointer(d); }
    void attachEncoder(IEncoderDevice& d) { input_.attachEncoder(d); }
    void attachButtons(IButtonDevice& d) { input_.attachButtons(d); }

    Renderer&    renderer() { return renderer_; }
    RenderQueue& queue() { return renderer_.queue(); }
    InputRouter& input() { return input_; }

    /**
     * Jeden przebieg pętli interfejsu. Normalnie woła go task ui.render;
     * wystawiony publicznie, żeby testy mogły sterować czasem.
     */
    void step(Millis now);

    Stats stats() const { return stats_; }

protected:
    Status onInit() override;
    Status onStart() override;
    void   onStop() override;

private:
    IDisplayBackend& display_;
    Renderer         renderer_;
    InputRouter      input_;
    Config           cfg_{};
    Task             task_;
    Stats            stats_{};
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
