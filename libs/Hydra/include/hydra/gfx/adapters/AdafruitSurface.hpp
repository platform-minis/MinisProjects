#pragma once
/**
 * Hydra — powierzchnia nad urządzeniem Adafruit_GFX.
 *
 * Adafruit_GFX jest faktycznym standardem wyświetlaczy w świecie Arduino:
 * mówią nim ILI9341, ST7789, SSD1306, SH110X i dziesiątki innych sterowników.
 *
 * Adapter jest szablonem, nie klasą — i to jest tu istotne. Dzięki temu Hydra
 * nie włącza nagłówka Adafruit_GFX.h, więc reguła 2 z rozdz. 3 („nagłówki
 * Arduino wyłącznie w katalogach backendów") pozostaje nienaruszona, a sam
 * adapter da się przetestować na hoście atrapą urządzenia o tym samym API.
 * Cena: aplikacja musi włączyć nagłówek producenta przed tym plikiem —
 * i tak by to zrobiła, bo tworzy obiekt wyświetlacza.
 *
 *     #include <Adafruit_SSD1306.h>
 *     #include <hydra/gfx/adapters/AdafruitSurface.hpp>
 *
 *     Adafruit_SSD1306 oled(128, 64, &Wire);
 *     hydra::gfx::AdafruitSurface<Adafruit_SSD1306> screen(oled);
 *
 *     void setup() {
 *         oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
 *         // Panel buforowany wymaga jawnego transferu:
 *         screen.setFlush([&] { oled.display(); return hydra::ok(); });
 *     }
 */

#include "hydra/core/Delegate.hpp"
#include "hydra/gfx/ISurface.hpp"

namespace hydra {
namespace gfx {

template <typename Device>
class AdafruitSurface : public ISurface {
public:
    using FlushFn = Delegate<Status()>;

    explicit AdafruitSurface(Device& device) : dev_(device) {}

    /** Transfer bufora na panel; potrzebny dla OLED-ów, zbędny dla TFT. */
    void setFlush(FlushFn fn) { flush_ = fn; }

    Size size() const override {
        return Size(static_cast<i16>(dev_.width()), static_cast<i16>(dev_.height()));
    }
    PixelFormat pixelFormat() const override { return PixelFormat::Rgb565; }

    Status flush() override {
        if (flush_) HYDRA_CHECK(flush_());
        clearDirty();
        return ok();
    }

    void beginBatch() override { dev_.startWrite(); }
    void endBatch() override { dev_.endWrite(); }

    // Prymitywy, które Adafruit_GFX robi sam — zwykle z akceleracją sprzętową
    // albo przynajmniej bez wywołania na piksel.
    Status fillRect(Rect r, Color c) override {
        const Rect a = r.intersect(clip());
        if (a.empty()) return ok();
        dev_.fillRect(a.x, a.y, a.w, a.h, c.rgb565());
        markDirty(a);
        return ok();
    }

    Status hLine(i16 x, i16 y, i16 w, Color c) override {
        return fillRect(Rect(x, y, w, 1), c);
    }
    Status vLine(i16 x, i16 y, i16 h, Color c) override {
        return fillRect(Rect(x, y, 1, h), c);
    }

    Status fill(Color c) override {
        // fillScreen ignoruje przycinanie, więc korzystamy z niego tylko wtedy,
        // gdy obszar rysowania obejmuje cały panel.
        if (clip() != bounds()) return ISurface::fill(c);
        dev_.fillScreen(c.rgb565());
        markDirty(bounds());
        return ok();
    }

protected:
    Status writePixel(i16 x, i16 y, Color c) override {
        dev_.drawPixel(x, y, c.rgb565());
        return ok();
    }

private:
    Device& dev_;
    FlushFn flush_{};
};

}  // namespace gfx
}  // namespace hydra
