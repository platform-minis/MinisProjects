#pragma once
/**
 * Hydra — pętla renderowania (rozdz. 6).
 *
 * Renderer robi trzy rzeczy w każdej klatce: wykonuje zakolejkowane polecenia,
 * przerysowuje obszar oznaczony jako nieaktualny i wysyła go na panel.
 *
 * Dwie decyzje decydują o kosztach:
 *
 * **Klatka bez zmian nie kosztuje nic.** Jeśli nikt niczego nie unieważnił,
 * renderer nie rysuje i nie transferuje. Interfejs pokazujący nieruchomy ekran
 * statusu zużywa tyle procesora, co zatrzymany task — a to jest stan, w którym
 * urządzenie IoT spędza większość życia.
 *
 * **Przy dwóch buforach obszar do przerysowania obejmuje także poprzednią
 * klatkę.** Bufor, do którego renderer właśnie rysuje, pamięta stan sprzed
 * dwóch klatek, więc odświeżenie samego bieżącego obszaru zostawia w nim
 * resztki poprzedniego. To najczęstsze źródło „duchów" w podwójnie buforowanych
 * interfejsach i jedyne miejsce, w którym liczba buforów przecieka do logiki.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Delegate.hpp"
#include "hydra/ui/IDisplayBackend.hpp"
#include "hydra/ui/RenderQueue.hpp"

namespace hydra {
namespace ui {

class Renderer {
public:
    /**
     * Funkcja rysująca zawartość. Dostaje powierzchnię i obszar, który wymaga
     * odświeżenia — może narysować wszystko i zdać się na przycinanie albo
     * pominąć elementy spoza obszaru.
     */
    using DrawFn = Delegate<void(gfx::ISurface&, Rect)>;

    struct Config {
        /** Pomijanie klatek bez zmian. Wyłączyć tylko przy animacji ciągłej. */
        bool skipIdleFrames = true;
        /** Publikacja zdarzenia po każdej narysowanej klatce. */
        bool publishFrameEvents = false;
    };

    struct Stats {
        u32 frames        = 0;  ///< klatki narysowane
        u32 skipped       = 0;  ///< klatki pominięte, bo nic się nie zmieniło
        u32 commands      = 0;  ///< wykonane polecenia z kolejki
        u32 maxRenderUs   = 0;
        u32 maxPresentUs  = 0;
        u32 lastRenderUs  = 0;
        u32 lastPresentUs = 0;
    };

    Status init(IDisplayBackend& backend, const Config& cfg);
    Status init(IDisplayBackend& backend);

    void setDraw(DrawFn fn) { draw_ = fn; }

    /** Oznacza całą powierzchnię jako wymagającą przerysowania. */
    void invalidate();
    /** Oznacza fragment. Kolejne wywołania sumują się w jeden obszar. */
    void invalidate(Rect area);

    /** Czy w następnej klatce jest co rysować. */
    bool needsRedraw() const { return !pending_.empty(); }

    /** Kolejka poleceń — jedyna droga zmiany interfejsu spoza ui.render. */
    RenderQueue& queue() { return queue_; }

    /** Jedna klatka. Wołana z taska ui.render. */
    Status renderFrame(Millis now);

    IDisplayBackend* backend() const { return backend_; }
    Stats            stats() const { return stats_; }
    void             resetStats() { stats_ = Stats{}; }

private:
    IDisplayBackend* backend_ = nullptr;
    Config           cfg_{};
    DrawFn           draw_{};
    RenderQueue      queue_;

    /** Obszar do przerysowania w najbliższej klatce. */
    Rect pending_{};
    /** Obszar odświeżony w poprzedniej klatce — potrzebny przy dwóch buforach. */
    Rect previous_{};

    Stats stats_{};
    u32   frameNumber_ = 0;
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
