/** Hydra — implementacja motywów (rozdz. 6). */

#include "hydra/ui/Theme.hpp"

#if HYDRA_ENABLE_UI

namespace hydra {
namespace ui {

using gfx::Color;

Theme Theme::dark(u8 scale) {
    Theme t;
    t.background = Color(16, 18, 22);
    t.surface    = Color(30, 34, 40);
    t.text       = Color(232, 234, 238);
    t.textMuted  = Color(140, 146, 156);
    t.accent     = Color(70, 150, 230);
    t.ok         = Color(70, 190, 110);
    t.warning    = Color(235, 175, 45);
    t.danger     = Color(225, 75, 70);
    t.border     = Color(60, 66, 76);
    t.font       = &gfx::font8x8();
    t.scale      = scale ? scale : 1;
    t.padding    = static_cast<i16>(4 * t.scale);
    t.radius     = static_cast<i16>(4 * t.scale);
    return t;
}

Theme Theme::light(u8 scale) {
    Theme t;
    t.background = Color(248, 249, 251);
    t.surface    = Color(255, 255, 255);
    t.text       = Color(24, 28, 34);
    t.textMuted  = Color(110, 118, 130);
    t.accent     = Color(40, 110, 200);
    t.ok         = Color(40, 150, 80);
    t.warning    = Color(200, 140, 20);
    t.danger     = Color(200, 50, 50);
    t.border     = Color(210, 214, 220);
    t.font       = &gfx::font8x8();
    t.scale      = scale ? scale : 1;
    t.padding    = static_cast<i16>(4 * t.scale);
    t.radius     = static_cast<i16>(4 * t.scale);
    return t;
}

Theme Theme::mono(u8 scale) {
    Theme t;
    // Wszystko sprowadza się do dwóch barw. Widżety odróżniają stany
    // kształtem i wypełnieniem, bo kolorem nie mają jak.
    t.background = Color(0, 0, 0);
    t.surface    = Color(0, 0, 0);
    t.text       = Color(255, 255, 255);
    t.textMuted  = Color(255, 255, 255);
    t.accent     = Color(255, 255, 255);
    t.ok         = Color(255, 255, 255);
    t.warning    = Color(255, 255, 255);
    t.danger     = Color(255, 255, 255);
    t.border     = Color(255, 255, 255);
    t.font       = &gfx::font8x8();
    t.scale      = scale ? scale : 1;
    // Ciaśniejsze odstępy: panele jednobitowe są zwykle małe, a każdy piksel
    // odstępu to piksel zabrany treści.
    t.padding    = static_cast<i16>(2 * t.scale);
    t.radius     = 0;
    t.monochrome = true;
    return t;
}

i16 Theme::lineHeight() const {
    return font ? gfx::textHeight(*font, scale) : 0;
}

i16 Theme::rowHeightOrDefault() const {
    if (rowHeight > 0) return rowHeight;
    return static_cast<i16>(lineHeight() + 2 * padding);
}

i16 Theme::textWidth(const char* text) const {
    return font ? gfx::textWidth(*font, text, scale) : 0;
}

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
