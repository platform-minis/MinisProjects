#pragma once
/**
 * Hydra — powierzchnia nad U8g2.
 *
 * U8g2 obsługuje niemal każdy monochromatyczny wyświetlacz, jaki da się kupić:
 * SSD1306, SH1106, ST7565, e-papier, panele LCD z kontrolerem znakowym.
 * Jest jednobitowa, więc kolor sprowadza się do decyzji „zapalić czy zgasić" —
 * adapter progowuje jasność wg Rec.601, dzięki czemu ten sam kod rysujący
 * w kolorze daje czytelny obraz także tutaj.
 *
 * O szablonie zamiast klasy: patrz komentarz w AdafruitSurface.hpp.
 *
 *     #include <U8g2lib.h>
 *     #include <hydra/gfx/adapters/U8g2Surface.hpp>
 *
 *     U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0);
 *     hydra::gfx::U8g2Surface<decltype(oled)> screen(oled);
 *
 *     void setup() { oled.begin(); screen.begin(); }
 *     // flush() woła sendBuffer() — U8g2 w trybie pełnego bufora wymaga tego jawnie.
 */

#include "hydra/gfx/ISurface.hpp"

namespace hydra {
namespace gfx {

template <typename Device>
class U8g2Surface : public ISurface {
public:
    explicit U8g2Surface(Device& device) : dev_(device) {}

    Size size() const override {
        return Size(static_cast<i16>(dev_.getDisplayWidth()),
                    static_cast<i16>(dev_.getDisplayHeight()));
    }
    PixelFormat pixelFormat() const override { return PixelFormat::Mono1; }

    Status flush() override {
        dev_.sendBuffer();
        clearDirty();
        return ok();
    }

    Status fill(Color c) override {
        if (clip() != bounds()) return ISurface::fill(c);
        // clearBuffer() zawsze gasi; zapalenie całości wymaga prostokąta.
        dev_.clearBuffer();
        if (c.mono()) {
            dev_.setDrawColor(1);
            dev_.drawBox(0, 0, dev_.getDisplayWidth(), dev_.getDisplayHeight());
        }
        markDirty(bounds());
        return ok();
    }

    Status fillRect(Rect r, Color c) override {
        const Rect a = r.intersect(clip());
        if (a.empty()) return ok();
        // U8g2 nie zna koloru — rysuje kolorem ustawionym wcześniej.
        dev_.setDrawColor(c.mono() ? 1 : 0);
        dev_.drawBox(a.x, a.y, a.w, a.h);
        dev_.setDrawColor(1);
        markDirty(a);
        return ok();
    }

protected:
    Status writePixel(i16 x, i16 y, Color c) override {
        dev_.setDrawColor(c.mono() ? 1 : 0);
        dev_.drawPixel(x, y);
        dev_.setDrawColor(1);
        return ok();
    }

private:
    Device& dev_;
};

}  // namespace gfx
}  // namespace hydra
