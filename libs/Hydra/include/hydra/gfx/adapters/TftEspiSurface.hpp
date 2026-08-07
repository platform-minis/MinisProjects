#pragma once
/**
 * Hydra — powierzchnia nad TFT_eSPI.
 *
 * TFT_eSPI jest najszybszą powszechnie używaną biblioteką TFT na ESP32:
 * pisze wprost do panelu przez DMA i nie utrzymuje bufora ramki. Stąd flush()
 * nie ma tu nic do roboty, a zysk wydajnościowy bierze się z ujęcia całej
 * klatki w startWrite()/endWrite() — jedna transakcja SPI zamiast setek.
 *
 * O szablonie zamiast klasy: patrz komentarz w AdafruitSurface.hpp.
 *
 *     #include <TFT_eSPI.h>
 *     #include <hydra/gfx/adapters/TftEspiSurface.hpp>
 *
 *     TFT_eSPI tft;
 *     hydra::gfx::TftEspiSurface<TFT_eSPI> screen(tft);
 *     void setup() { tft.init(); screen.begin(); }
 */

#include "hydra/gfx/ISurface.hpp"

namespace hydra {
namespace gfx {

template <typename Device>
class TftEspiSurface : public ISurface {
public:
    explicit TftEspiSurface(Device& device) : dev_(device) {}

    Size size() const override {
        return Size(static_cast<i16>(dev_.width()), static_cast<i16>(dev_.height()));
    }
    PixelFormat pixelFormat() const override { return PixelFormat::Rgb565; }

    void beginBatch() override { dev_.startWrite(); }
    void endBatch() override { dev_.endWrite(); }

    Status fillRect(Rect r, Color c) override {
        const Rect a = r.intersect(clip());
        if (a.empty()) return ok();
        dev_.fillRect(a.x, a.y, a.w, a.h, c.rgb565());
        markDirty(a);
        return ok();
    }

    Status hLine(i16 x, i16 y, i16 w, Color c) override {
        const Rect a = Rect(x, y, w, 1).intersect(clip());
        if (a.empty()) return ok();
        dev_.drawFastHLine(a.x, a.y, a.w, c.rgb565());
        markDirty(a);
        return ok();
    }

    Status vLine(i16 x, i16 y, i16 h, Color c) override {
        const Rect a = Rect(x, y, 1, h).intersect(clip());
        if (a.empty()) return ok();
        dev_.drawFastVLine(a.x, a.y, a.h, c.rgb565());
        markDirty(a);
        return ok();
    }

    Status fill(Color c) override {
        if (clip() != bounds()) return ISurface::fill(c);
        dev_.fillScreen(c.rgb565());
        markDirty(bounds());
        return ok();
    }

    /** Obraz RGB565 wysyłany jednym transferem DMA zamiast piksel po pikselu. */
    Status drawBitmapRgb565(i16 x, i16 y, const u16* pixels, i16 w, i16 h) override {
        if (!pixels) return fail(Err::BadArgument);
        // pushImage nie respektuje przycinania, więc przy zawężonym obszarze
        // wracamy do ścieżki programowej.
        if (!clip().intersect(Rect(x, y, w, h)).empty() &&
            clip().intersect(Rect(x, y, w, h)) != Rect(x, y, w, h)) {
            return ISurface::drawBitmapRgb565(x, y, pixels, w, h);
        }
        dev_.pushImage(x, y, w, h, const_cast<u16*>(pixels));
        markDirty(Rect(x, y, w, h));
        return ok();
    }

protected:
    Status writePixel(i16 x, i16 y, Color c) override {
        dev_.drawPixel(x, y, c.rgb565());
        return ok();
    }

private:
    Device& dev_;
};

}  // namespace gfx
}  // namespace hydra
