#pragma once
/**
 * Hydra — most między LVGL a warstwą wyświetlania Hydry (rozdz. 6).
 *
 * LVGL renderuje do własnego bufora i oddaje go fragmentami przez wywołanie
 * zwrotne „flush": dostajemy obszar i wskaźnik na piksele. Zadaniem mostu jest
 * przenieść te piksele na powierzchnię Hydry i potwierdzić LVGL-owi, że bufor
 * jest wolny.
 *
 * Zysk z tego układu jest konkretny: LVGL zaczyna działać na **każdym** panelu,
 * dla którego istnieje adapter w gfx/adapters/ — Adafruit_GFX, TFT_eSPI,
 * LovyanGFX, U8g2, MinisGfx — bez pisania sterownika wyświetlacza dla LVGL.
 * Odwrotnie też: aplikacja może mieszać ekrany LVGL z widżetami Hydry,
 * bo obie warstwy rysują w to samo miejsce.
 *
 * Most nie zna LVGL. Nie włącza jego nagłówka i nie odwołuje się do jego
 * funkcji — dostaje surowe bajty i opis formatu.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/ui/IDisplayBackend.hpp"
#include "hydra/ui/lvgl/LvglTypes.hpp"

namespace hydra {
namespace ui {
namespace lvgl {

class LvglBridge {
public:
    struct Stats {
        u32 flushes    = 0;  ///< obsłużone wywołania zwrotne LVGL
        u32 pixels     = 0;  ///< przeniesione piksele
        u32 clipped    = 0;  ///< wywołania z obszarem częściowo poza ekranem
        u32 rejected   = 0;  ///< wywołania odrzucone (błędne dane)
    };

    Status init(IDisplayBackend& display, ColorFormat format);

    /**
     * Przenosi fragment obrazu z bufora LVGL na powierzchnię.
     *
     * `area` jest w układzie współrzędnych panelu i domknięty z obu stron,
     * dokładnie jak `lv_area_t`. Bufor zawiera wiersze tego obszaru, ciasno
     * upakowane — LVGL nie stosuje dopełnienia wierszy.
     */
    Status flushArea(Rect area, const u8* pixels);

    /**
     * Czy po ostatnim przeniesieniu zawartość trafiła już na panel.
     * LVGL woła flush wielokrotnie na klatkę i tylko ostatnie wywołanie
     * ma ustawioną flagę zakończenia.
     */
    Status present(Rect area);

    IDisplayBackend* display() const { return display_; }
    ColorFormat      format() const { return format_; }
    Stats            stats() const { return stats_; }
    void             resetStats() { stats_ = Stats{}; }

private:
    gfx::Color decode(const u8* pixel) const;

    IDisplayBackend* display_ = nullptr;
    ColorFormat      format_  = ColorFormat::Rgb565;
    Stats            stats_{};
};

}  // namespace lvgl
}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
