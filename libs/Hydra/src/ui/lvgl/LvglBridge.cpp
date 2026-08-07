/** Hydra — implementacja mostu do LVGL (rozdz. 6). */

#include "hydra/ui/lvgl/LvglBridge.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("ui.lvgl")

namespace hydra {
namespace ui {
namespace lvgl {

Status LvglBridge::init(IDisplayBackend& display, ColorFormat format) {
    display_ = &display;
    format_  = format;
    stats_   = Stats{};

    HYDRA_LOGI("LVGL rysuje w formacie %s na panelu '%s' %dx%d", toString(format),
               display.name(), display.size().w, display.size().h);
    return ok();
}

gfx::Color LvglBridge::decode(const u8* pixel) const {
    switch (format_) {
        case ColorFormat::Rgb565:
            // LVGL trzyma RGB565 w kolejności bajtów procesora, czyli na
            // wszystkich platformach docelowych młodszy bajt pierwszy.
            return gfx::Color::fromRgb565(
                static_cast<u16>(pixel[0] | (static_cast<u16>(pixel[1]) << 8)));

        case ColorFormat::Rgb565Swapped:
            return gfx::Color::fromRgb565(
                static_cast<u16>((static_cast<u16>(pixel[0]) << 8) | pixel[1]));

        case ColorFormat::Rgb888:
            // LVGL układa składowe jako B, G, R — odwrotnie niż nazwa formatu.
            return gfx::Color(pixel[2], pixel[1], pixel[0]);

        case ColorFormat::Argb8888:
            return gfx::Color(pixel[2], pixel[1], pixel[0], pixel[3]);

        case ColorFormat::Mono1:
            break;
    }
    return gfx::colors::black;
}

Status LvglBridge::flushArea(Rect area, const u8* pixels) {
    if (!display_) return fail(Err::NotInitialized);
    if (!pixels || area.empty()) {
        ++stats_.rejected;
        return fail(Err::BadArgument);
    }

    gfx::ISurface& surface = display_->surface();
    const Rect     visible = area.intersect(surface.bounds());
    if (visible.empty()) {
        // Obszar w całości poza ekranem to nie błąd: LVGL potrafi zgłosić
        // fragment wychodzący poza panel przy obrocie albo animacji.
        ++stats_.clipped;
        return ok();
    }
    if (visible != area) ++stats_.clipped;

    // Przycinanie ustawiamy raz, zamiast sprawdzać każdy piksel osobno.
    const Rect previousClip = surface.clip();
    surface.setClip(visible);
    surface.beginBatch();

    if (format_ == ColorFormat::Mono1) {
        // Bufor jednobitowy: wiersze wyrównane do pełnego bajtu, bit najstarszy
        // z lewej — układ zgodny z bitmapami i pamięcią obrazu e-papieru.
        const i16 stride = static_cast<i16>((area.w + 7) / 8);
        for (i16 row = 0; row < area.h; ++row) {
            for (i16 col = 0; col < area.w; ++col) {
                const u8 byte = pixels[row * stride + (col / 8)];
                const bool on = (byte & (0x80 >> (col % 8))) != 0;
                surface.drawPixel(static_cast<i16>(area.x + col),
                                  static_cast<i16>(area.y + row),
                                  on ? gfx::colors::white : gfx::colors::black);
            }
        }
    } else {
        const u8  bpp    = bytesPerPixel(format_);
        const i32 stride = static_cast<i32>(area.w) * bpp;
        for (i16 row = 0; row < area.h; ++row) {
            const u8* line = pixels + static_cast<i32>(row) * stride;
            for (i16 col = 0; col < area.w; ++col) {
                surface.drawPixel(static_cast<i16>(area.x + col),
                                  static_cast<i16>(area.y + row),
                                  decode(line + static_cast<i32>(col) * bpp));
            }
        }
    }

    surface.endBatch();
    surface.setClip(previousClip);

    ++stats_.flushes;
    stats_.pixels += static_cast<u32>(visible.w) * visible.h;
    return ok();
}

Status LvglBridge::present(Rect area) {
    if (!display_) return fail(Err::NotInitialized);
    return display_->present(area.empty() ? display_->surface().bounds() : area);
}

}  // namespace lvgl
}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
