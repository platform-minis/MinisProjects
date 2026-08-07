#pragma once
/**
 * Hydra — powierzchnia nad LovyanGFX.
 *
 * LovyanGFX obsługuje bardzo szeroki zbiór paneli SPI i równoległych i jest
 * szybka. Przyjmuje kolor jako 24-bitowy RGB888 w uint32_t i sama konwertuje
 * go do głębi panelu — dlatego adapter nie robi konwersji do RGB565 i nie
 * traci informacji na wyświetlaczach o większej głębi.
 *
 * O szablonie zamiast klasy: patrz komentarz w AdafruitSurface.hpp.
 *
 *     #include <LovyanGFX.hpp>
 *     #include <hydra/gfx/adapters/LovyanSurface.hpp>
 *
 *     LGFX lcd;
 *     hydra::gfx::LovyanSurface<LGFX> screen(lcd);
 */

#include "hydra/gfx/ISurface.hpp"

namespace hydra {
namespace gfx {

template <typename Device>
class LovyanSurface : public ISurface {
public:
    explicit LovyanSurface(Device& device) : dev_(device) {}

    Size size() const override {
        return Size(static_cast<i16>(dev_.width()), static_cast<i16>(dev_.height()));
    }
    /** LovyanGFX przyjmuje pełne RGB888 niezależnie od głębi panelu. */
    PixelFormat pixelFormat() const override { return PixelFormat::Rgb888; }

    void beginBatch() override { dev_.startWrite(); }
    void endBatch() override { dev_.endWrite(); }

    Status fillRect(Rect r, Color c) override {
        const Rect a = r.intersect(clip());
        if (a.empty()) return ok();
        dev_.fillRect(a.x, a.y, a.w, a.h, c.rgb888());
        markDirty(a);
        return ok();
    }

    Status hLine(i16 x, i16 y, i16 w, Color c) override {
        const Rect a = Rect(x, y, w, 1).intersect(clip());
        if (a.empty()) return ok();
        dev_.drawFastHLine(a.x, a.y, a.w, c.rgb888());
        markDirty(a);
        return ok();
    }

    Status vLine(i16 x, i16 y, i16 h, Color c) override {
        const Rect a = Rect(x, y, 1, h).intersect(clip());
        if (a.empty()) return ok();
        dev_.drawFastVLine(a.x, a.y, a.h, c.rgb888());
        markDirty(a);
        return ok();
    }

    Status fill(Color c) override {
        if (clip() != bounds()) return ISurface::fill(c);
        dev_.fillScreen(c.rgb888());
        markDirty(bounds());
        return ok();
    }

    Status drawCircle(i16 cx, i16 cy, i16 r, Color c) override {
        if (r < 0) return fail(Err::BadArgument);
        dev_.drawCircle(cx, cy, r, c.rgb888());
        markDirty(Rect(static_cast<i16>(cx - r), static_cast<i16>(cy - r),
                       static_cast<i16>(2 * r + 1), static_cast<i16>(2 * r + 1))
                      .intersect(clip()));
        return ok();
    }

    Status fillCircle(i16 cx, i16 cy, i16 r, Color c) override {
        if (r < 0) return fail(Err::BadArgument);
        dev_.fillCircle(cx, cy, r, c.rgb888());
        markDirty(Rect(static_cast<i16>(cx - r), static_cast<i16>(cy - r),
                       static_cast<i16>(2 * r + 1), static_cast<i16>(2 * r + 1))
                      .intersect(clip()));
        return ok();
    }

protected:
    Status writePixel(i16 x, i16 y, Color c) override {
        dev_.drawPixel(x, y, c.rgb888());
        return ok();
    }

private:
    Device& dev_;
};

}  // namespace gfx
}  // namespace hydra
