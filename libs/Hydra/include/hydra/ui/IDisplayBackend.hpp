#pragma once
/**
 * Hydra — panel wyświetlacza (rozdz. 6).
 *
 * Warstwa gfx odpowiada za rysowanie, ta — za panel: transfer obrazu,
 * orientację, podświetlenie i to, czy sprzęt potrafi odświeżyć sam zmieniony
 * fragment. Podział jest istotny, bo te dwie rzeczy zmieniają się niezależnie:
 * ten sam kontroler ST7789 bywa podłączony przez TFT_eSPI, LovyanGFX albo
 * natywny sterownik z DMA, a ten sam sposób transferu obsługuje różne panele.
 *
 * Dwa parametry decydują o całej strategii renderowania:
 *
 *   supportsPartial() — czy wolno wysłać sam zmieniony prostokąt. E-papier
 *     i większość TFT to potrafią; niektóre panele wymagają pełnej ramki.
 *   bufferCount()     — jeden bufor czy dwa. Przy dwóch renderer musi
 *     odświeżyć także obszar zmieniony w poprzedniej klatce, bo bufor, do
 *     którego właśnie rysuje, pamięta stan sprzed dwóch klatek. To najczęstsze
 *     źródło „duchów" w podwójnie buforowanych interfejsach.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Expected.hpp"
#include "hydra/gfx/ISurface.hpp"
#include "hydra/ui/UiTypes.hpp"

namespace hydra {
namespace ui {

class IDisplayBackend {
public:
    virtual ~IDisplayBackend() = default;

    /** Nazwa do logów i diagnostyki: "st7789", "ssd1306", "mock". */
    virtual const char* name() const = 0;

    virtual Status begin() { return ok(); }

    /** Powierzchnia, na której rysuje renderer w bieżącej klatce. */
    virtual gfx::ISurface& surface() = 0;

    Size size() const { return const_cast<IDisplayBackend*>(this)->surface().size(); }

    /**
     * Wysyła na panel wskazany obszar. Prostokąt pusty oznacza całą powierzchnię.
     * Backendy bez odświeżania częściowego mogą argument zignorować.
     */
    virtual Status present(Rect area) = 0;

    /** Czy wolno wysyłać sam zmieniony fragment obrazu. */
    virtual bool supportsPartial() const { return true; }

    /** Liczba buforów obrazu: 1 albo 2. */
    virtual u8 bufferCount() const { return 1; }

    /** Podświetlenie w procentach; brak sterowania to Err::NotSupported. */
    virtual Status setBacklight(u8 percent) {
        HYDRA_UNUSED(percent);
        return fail(Err::NotSupported);
    }

    virtual Status setRotation(gfx::Rotation r) {
        HYDRA_UNUSED(r);
        return fail(Err::NotSupported);
    }
};

/**
 * Najprostszy backend: dowolna powierzchnia gfx jako panel.
 *
 * To dzięki niemu każdy adapter z gfx/adapters/ — TFT_eSPI, Adafruit_GFX,
 * LovyanGFX, U8g2, MinisGfx — staje się od razu pełnoprawnym wyświetlaczem
 * modułu UI, bez pisania osobnego sterownika.
 */
class SurfaceDisplay : public IDisplayBackend {
public:
    explicit SurfaceDisplay(gfx::ISurface& surface, const char* name = "surface")
        : surface_(surface), name_(name) {}

    const char*    name() const override { return name_; }
    gfx::ISurface& surface() override { return surface_; }

    Status begin() override { return surface_.begin(); }

    Status present(Rect area) override {
        HYDRA_UNUSED(area);
        // Powierzchnia sama wie, jak przenieść zawartość na panel; obszar
        // pomijamy, bo ISurface::flush() nie ma pojęcia częściowego transferu.
        return surface_.flush();
    }

    /** Bez wiedzy o panelu zakładamy najprostszy przypadek: pełna ramka. */
    bool supportsPartial() const override { return false; }

private:
    gfx::ISurface& surface_;
    const char*    name_;
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
