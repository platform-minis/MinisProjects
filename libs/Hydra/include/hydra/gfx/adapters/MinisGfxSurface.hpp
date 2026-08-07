#pragma once
/**
 * Hydra — powierzchnia nad MinisGfx (libs/MinisLib).
 *
 * Most do istniejącej warstwy graficznej projektów Minis. Dzięki niemu backendy
 * napisane już dla MinisGfx — Adafruit_GFX, LovyanGFX, GxEPD2 (e-papier) oraz
 * Qt — działają w Hydrze bez przenoszenia ani jednej linii kodu, a MinisLib
 * pozostaje ich jedynym źródłem. Nowego backendu nie trzeba pisać dwa razy.
 *
 * Adapter jest szablonem po typie urządzenia i typie koloru, więc Hydra nie
 * włącza MinisGfx.h ani nie zależy od MinisLib. Aplikacja podaje oba typy:
 *
 *     #include <MinisGfx.h>
 *     #include <MinisGfxGxEPD2.h>
 *     #include <hydra/gfx/adapters/MinisGfxSurface.hpp>
 *
 *     MinisGfxGxEPD2 epaper(display);
 *     hydra::gfx::MinisGfxSurface<MinisGfx, MGColor> screen(epaper);
 *
 * Uwaga na różnicę kontraktów: MinisGfx nie zna przycinania ani kodów błędów.
 * Przycinanie realizuje więc ta warstwa (przed przekazaniem współrzędnych),
 * a operacje kończą się sukcesem, o ile MinisGfx w ogóle je przyjął.
 */

#include "hydra/gfx/ISurface.hpp"

namespace hydra {
namespace gfx {

template <typename Device, typename VendorColor>
class MinisGfxSurface : public ISurface {
public:
    explicit MinisGfxSurface(Device& device) : dev_(device) {}

    Size size() const override {
        return Size(static_cast<i16>(dev_.width()), static_cast<i16>(dev_.height()));
    }
    /** MinisGfx przenosi kolor jako RGBA8888, tak samo jak Hydra. */
    PixelFormat pixelFormat() const override { return PixelFormat::Rgba8888; }

    Status begin() override {
        dev_.begin();
        return ok();
    }

    Status flush() override {
        dev_.display();
        clearDirty();
        return ok();
    }

    void beginBatch() override { dev_.startWrite(); }
    void endBatch() override { dev_.endWrite(); }

    Status fillRect(Rect r, Color c) override {
        const Rect a = r.intersect(clip());
        if (a.empty()) return ok();
        dev_.fillRect(a.x, a.y, a.w, a.h, convert(c));
        markDirty(a);
        return ok();
    }

    Status fill(Color c) override {
        if (clip() != bounds()) return ISurface::fill(c);
        dev_.fillScreen(convert(c));
        markDirty(bounds());
        return ok();
    }

    Status drawCircle(i16 cx, i16 cy, i16 r, Color c) override {
        if (r < 0) return fail(Err::BadArgument);
        dev_.drawCircle(cx, cy, r, convert(c));
        markDirty(circleBounds(cx, cy, r));
        return ok();
    }

    Status fillCircle(i16 cx, i16 cy, i16 r, Color c) override {
        if (r < 0) return fail(Err::BadArgument);
        dev_.fillCircle(cx, cy, r, convert(c));
        markDirty(circleBounds(cx, cy, r));
        return ok();
    }

protected:
    Status writePixel(i16 x, i16 y, Color c) override {
        dev_.drawPixel(x, y, convert(c));
        return ok();
    }

private:
    static VendorColor convert(Color c) { return VendorColor(c.r, c.g, c.b, c.a); }

    Rect circleBounds(i16 cx, i16 cy, i16 r) const {
        return Rect(static_cast<i16>(cx - r), static_cast<i16>(cy - r),
                    static_cast<i16>(2 * r + 1), static_cast<i16>(2 * r + 1))
            .intersect(clip());
    }

    Device& dev_;
};

}  // namespace gfx
}  // namespace hydra
