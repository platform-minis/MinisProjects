/**
 * Hydra — programowe implementacje prymitywów rysowania.
 *
 * Wszystko sprowadza się do writePixel(). Algorytmy dobrane pod procesory bez
 * jednostki zmiennoprzecinkowej: linia Bresenhama i okrąg metodą punktu
 * środkowego pracują wyłącznie na liczbach całkowitych, więc RP2040 rysuje
 * tak samo szybko jak ESP32 (rozdz. 15).
 */

#include "hydra/gfx/ISurface.hpp"

namespace hydra {
namespace gfx {
namespace {

i16 imin(i16 a, i16 b) { return a < b ? a : b; }
i16 imax(i16 a, i16 b) { return a > b ? a : b; }

void swapI16(i16& a, i16& b) {
    const i16 t = a;
    a = b;
    b = t;
}

/**
 * Dzielenie z zaokrągleniem w dół, także dla liczb ujemnych.
 *
 * Wbudowane dzielenie w C++ obcina w stronę zera, więc dla krawędzi o ujemnym
 * nachyleniu dawałoby wynik o jeden większy niż dla dodatniego. W wypełnianiu
 * trójkąta objawia się to szczelinami między wypełnieniem a obrysem po jednej
 * stronie figury — czyli dokładnie na skosach, gdzie widać je najbardziej.
 */
i32 floorDiv(i32 a, i32 b) {
    if (b == 0) return 0;
    const i32 q = a / b;
    return ((a % b != 0) && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/**
 * Podłoga i sufit z jednego dzielenia.
 *
 * Obrys Bresenhama zaokrągla do najbliższego piksela, więc trafia raz poniżej,
 * raz powyżej dokładnego położenia krawędzi. Wypełnienie musi objąć oba
 * przypadki, a liczenie tego dwoma dzieleniami byłoby marnotrawstwem na
 * rdzeniach bez sprzętowego dzielnika (Cortex-M0+).
 */
void divFloorCeil(i32 a, i32 b, i32& floorOut, i32& ceilOut) {
    if (b == 0) {
        floorOut = ceilOut = 0;
        return;
    }
    const i32 q = a / b;
    const i32 r = a % b;
    if (r == 0) {
        floorOut = ceilOut = q;
    } else if ((a < 0) != (b < 0)) {
        floorOut = q - 1;
        ceilOut  = q;
    } else {
        floorOut = q;
        ceilOut  = q + 1;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Piksel
// ---------------------------------------------------------------------------

Status ISurface::drawPixel(i16 x, i16 y, Color c) {
    // Poza obszarem przycinania i piksel całkowicie przezroczysty to nie błędy,
    // tylko normalny wynik rysowania — zwracamy sukces bez zapisu.
    if (c.invisible()) return ok();
    if (!clip().contains(x, y)) return ok();

    Color final = c;
    if (!c.opaque()) {
        auto under = readPixel(x, y);
        // Panel bez odczytu: kolor półprzezroczysty rysujemy jako pełny.
        // Gubienie go byłoby gorsze — element zniknąłby bez śladu.
        if (under) final = c.over(*under);
        else       final = c.withAlpha(255);
    }

    HYDRA_CHECK(writePixel(x, y, final));
    markDirty(Rect(x, y, 1, 1));
    return ok();
}

// ---------------------------------------------------------------------------
// Wypełnienia i linie proste
// ---------------------------------------------------------------------------

Status ISurface::fill(Color c) { return fillRect(bounds(), c); }

Status ISurface::hLine(i16 x, i16 y, i16 w, Color c) {
    if (w < 0) {
        x = static_cast<i16>(x + w + 1);
        w = static_cast<i16>(-w);
    }
    return fillRect(Rect(x, y, w, 1), c);
}

Status ISurface::vLine(i16 x, i16 y, i16 h, Color c) {
    if (h < 0) {
        y = static_cast<i16>(y + h + 1);
        h = static_cast<i16>(-h);
    }
    return fillRect(Rect(x, y, 1, h), c);
}

Status ISurface::fillRect(Rect r, Color c) {
    if (c.invisible()) return ok();

    // Przycięcie z góry: pętla chodzi wyłącznie po pikselach, które naprawdę
    // trafią na ekran, zamiast odrzucać je pojedynczo w drawPixel().
    const Rect area = r.intersect(clip());
    if (area.empty()) return ok();

    beginBatch();
    for (i16 yy = area.y; yy <= area.bottom(); ++yy) {
        for (i16 xx = area.x; xx <= area.right(); ++xx) {
            if (auto s = drawPixel(xx, yy, c); !s) {
                endBatch();
                return s;
            }
        }
    }
    endBatch();
    return ok();
}

Status ISurface::drawRect(Rect r, Color c) {
    if (r.empty()) return ok();
    HYDRA_CHECK(hLine(r.x, r.y, r.w, c));
    HYDRA_CHECK(hLine(r.x, r.bottom(), r.w, c));
    // Pionowe boki bez naroży — te narysowały już linie poziome.
    if (r.h > 2) {
        HYDRA_CHECK(vLine(r.x, static_cast<i16>(r.y + 1), static_cast<i16>(r.h - 2), c));
        HYDRA_CHECK(vLine(r.right(), static_cast<i16>(r.y + 1), static_cast<i16>(r.h - 2), c));
    }
    return ok();
}

Status ISurface::line(i16 x0, i16 y0, i16 x1, i16 y1, Color c) {
    // Przypadki proste trafiają do szybszych ścieżek, które backend
    // często ma zaimplementowane sprzętowo.
    if (y0 == y1) return hLine(imin(x0, x1), y0, static_cast<i16>(imax(x0, x1) - imin(x0, x1) + 1), c);
    if (x0 == x1) return vLine(x0, imin(y0, y1), static_cast<i16>(imax(y0, y1) - imin(y0, y1) + 1), c);

    // Bresenham na liczbach całkowitych.
    const bool steep = (y1 > y0 ? y1 - y0 : y0 - y1) > (x1 > x0 ? x1 - x0 : x0 - x1);
    if (steep) {
        swapI16(x0, y0);
        swapI16(x1, y1);
    }
    if (x0 > x1) {
        swapI16(x0, x1);
        swapI16(y0, y1);
    }

    const i16 dx    = static_cast<i16>(x1 - x0);
    const i16 dy    = static_cast<i16>(y1 > y0 ? y1 - y0 : y0 - y1);
    const i16 step  = static_cast<i16>(y0 < y1 ? 1 : -1);
    i16       err   = static_cast<i16>(dx / 2);
    i16       y     = y0;

    beginBatch();
    for (i16 x = x0; x <= x1; ++x) {
        const Status s = steep ? drawPixel(y, x, c) : drawPixel(x, y, c);
        if (!s) {
            endBatch();
            return s;
        }
        err = static_cast<i16>(err - dy);
        if (err < 0) {
            y   = static_cast<i16>(y + step);
            err = static_cast<i16>(err + dx);
        }
    }
    endBatch();
    return ok();
}

// ---------------------------------------------------------------------------
// Prostokąty zaokrąglone
// ---------------------------------------------------------------------------

namespace {

/**
 * Ćwiartki okręgu użyte w narożnikach. Maska bitowa wybiera ćwiartki:
 * 1 = prawa górna, 2 = lewa górna, 4 = prawa dolna, 8 = lewa dolna.
 */
Status circleQuadrants(ISurface& s, i16 cx, i16 cy, i16 r, u8 quadrants, Color c) {
    i16 f     = static_cast<i16>(1 - r);
    i16 ddF_x = 1;
    i16 ddF_y = static_cast<i16>(-2 * r);
    i16 x     = 0;
    i16 y     = r;

    while (x < y) {
        if (f >= 0) {
            --y;
            ddF_y = static_cast<i16>(ddF_y + 2);
            f     = static_cast<i16>(f + ddF_y);
        }
        ++x;
        ddF_x = static_cast<i16>(ddF_x + 2);
        f     = static_cast<i16>(f + ddF_x);

        if (quadrants & 0x1) {
            HYDRA_CHECK(s.drawPixel(static_cast<i16>(cx + x), static_cast<i16>(cy - y), c));
            HYDRA_CHECK(s.drawPixel(static_cast<i16>(cx + y), static_cast<i16>(cy - x), c));
        }
        if (quadrants & 0x2) {
            HYDRA_CHECK(s.drawPixel(static_cast<i16>(cx - x), static_cast<i16>(cy - y), c));
            HYDRA_CHECK(s.drawPixel(static_cast<i16>(cx - y), static_cast<i16>(cy - x), c));
        }
        if (quadrants & 0x4) {
            HYDRA_CHECK(s.drawPixel(static_cast<i16>(cx + x), static_cast<i16>(cy + y), c));
            HYDRA_CHECK(s.drawPixel(static_cast<i16>(cx + y), static_cast<i16>(cy + x), c));
        }
        if (quadrants & 0x8) {
            HYDRA_CHECK(s.drawPixel(static_cast<i16>(cx - x), static_cast<i16>(cy + y), c));
            HYDRA_CHECK(s.drawPixel(static_cast<i16>(cx - y), static_cast<i16>(cy + x), c));
        }
    }
    return ok();
}

/** Wypełnione połówki okręgu użyte w narożnikach zaokrąglonego prostokąta. */
Status circleHalvesFilled(ISurface& s, i16 cx, i16 cy, i16 r, u8 sides, i16 delta,
                          Color c) {
    i16 f     = static_cast<i16>(1 - r);
    i16 ddF_x = 1;
    i16 ddF_y = static_cast<i16>(-2 * r);
    i16 x     = 0;
    i16 y     = r;

    while (x < y) {
        if (f >= 0) {
            --y;
            ddF_y = static_cast<i16>(ddF_y + 2);
            f     = static_cast<i16>(f + ddF_y);
        }
        ++x;
        ddF_x = static_cast<i16>(ddF_x + 2);
        f     = static_cast<i16>(f + ddF_x);

        if (sides & 0x1) {  // prawa
            HYDRA_CHECK(s.vLine(static_cast<i16>(cx + x), static_cast<i16>(cy - y),
                                static_cast<i16>(2 * y + 1 + delta), c));
            HYDRA_CHECK(s.vLine(static_cast<i16>(cx + y), static_cast<i16>(cy - x),
                                static_cast<i16>(2 * x + 1 + delta), c));
        }
        if (sides & 0x2) {  // lewa
            HYDRA_CHECK(s.vLine(static_cast<i16>(cx - x), static_cast<i16>(cy - y),
                                static_cast<i16>(2 * y + 1 + delta), c));
            HYDRA_CHECK(s.vLine(static_cast<i16>(cx - y), static_cast<i16>(cy - x),
                                static_cast<i16>(2 * x + 1 + delta), c));
        }
    }
    return ok();
}

}  // namespace

Status ISurface::drawCircle(i16 cx, i16 cy, i16 radius, Color c) {
    if (radius < 0) return fail(Err::BadArgument);
    if (radius == 0) return drawPixel(cx, cy, c);

    beginBatch();
    // Cztery punkty na osiach — algorytm punktu środkowego ich nie generuje.
    Status s = drawPixel(cx, static_cast<i16>(cy + radius), c);
    if (s) s = drawPixel(cx, static_cast<i16>(cy - radius), c);
    if (s) s = drawPixel(static_cast<i16>(cx + radius), cy, c);
    if (s) s = drawPixel(static_cast<i16>(cx - radius), cy, c);
    if (s) s = circleQuadrants(*this, cx, cy, radius, 0xF, c);
    endBatch();
    return s;
}

Status ISurface::fillCircle(i16 cx, i16 cy, i16 radius, Color c) {
    if (radius < 0) return fail(Err::BadArgument);

    beginBatch();
    Status s = vLine(cx, static_cast<i16>(cy - radius), static_cast<i16>(2 * radius + 1), c);
    if (s) s = circleHalvesFilled(*this, cx, cy, radius, 0x3, 0, c);
    endBatch();
    return s;
}

Status ISurface::drawRoundRect(Rect r, i16 radius, Color c) {
    if (r.empty()) return ok();

    // Promień większy niż połowa krótszego boku dałby nachodzące na siebie
    // narożniki — przycinamy zamiast rysować śmieci.
    const i16 maxR = static_cast<i16>(imin(r.w, r.h) / 2);
    if (radius > maxR) radius = maxR;
    if (radius <= 0) return drawRect(r, c);

    beginBatch();
    Status s = hLine(static_cast<i16>(r.x + radius), r.y, static_cast<i16>(r.w - 2 * radius), c);
    if (s) s = hLine(static_cast<i16>(r.x + radius), r.bottom(), static_cast<i16>(r.w - 2 * radius), c);
    if (s) s = vLine(r.x, static_cast<i16>(r.y + radius), static_cast<i16>(r.h - 2 * radius), c);
    if (s) s = vLine(r.right(), static_cast<i16>(r.y + radius), static_cast<i16>(r.h - 2 * radius), c);

    if (s) s = circleQuadrants(*this, static_cast<i16>(r.right() - radius),
                               static_cast<i16>(r.y + radius), radius, 0x1, c);
    if (s) s = circleQuadrants(*this, static_cast<i16>(r.x + radius),
                               static_cast<i16>(r.y + radius), radius, 0x2, c);
    if (s) s = circleQuadrants(*this, static_cast<i16>(r.right() - radius),
                               static_cast<i16>(r.bottom() - radius), radius, 0x4, c);
    if (s) s = circleQuadrants(*this, static_cast<i16>(r.x + radius),
                               static_cast<i16>(r.bottom() - radius), radius, 0x8, c);
    endBatch();
    return s;
}

Status ISurface::fillRoundRect(Rect r, i16 radius, Color c) {
    if (r.empty()) return ok();

    const i16 maxR = static_cast<i16>(imin(r.w, r.h) / 2);
    if (radius > maxR) radius = maxR;
    if (radius <= 0) return fillRect(r, c);

    beginBatch();
    Status s = fillRect(Rect(static_cast<i16>(r.x + radius), r.y,
                             static_cast<i16>(r.w - 2 * radius), r.h), c);
    if (s) s = circleHalvesFilled(*this, static_cast<i16>(r.right() - radius),
                                  static_cast<i16>(r.y + radius), radius, 0x1,
                                  static_cast<i16>(r.h - 2 * radius - 1), c);
    if (s) s = circleHalvesFilled(*this, static_cast<i16>(r.x + radius),
                                  static_cast<i16>(r.y + radius), radius, 0x2,
                                  static_cast<i16>(r.h - 2 * radius - 1), c);
    endBatch();
    return s;
}

// ---------------------------------------------------------------------------
// Trójkąty
// ---------------------------------------------------------------------------

Status ISurface::drawTriangle(Point a, Point b, Point c, Color color) {
    beginBatch();
    Status s = line(a.x, a.y, b.x, b.y, color);
    if (s) s = line(b.x, b.y, c.x, c.y, color);
    if (s) s = line(c.x, c.y, a.x, a.y, color);
    endBatch();
    return s;
}

Status ISurface::fillTriangle(Point p0, Point p1, Point p2, Color color) {
    // Wypełnienie zachowawcze: dla każdego wiersza bierzemy pełny zakres,
    // jaki pokrywa w nim krawędź, a nie jeden punkt jej przecięcia z wierszem.
    //
    // Powód jest praktyczny. Obrys rysowany algorytmem Bresenhama na krawędzi
    // płaskiej kładzie w jednym wierszu ciąg pikseli, a klasyczne wypełnienie
    // skanowe liczy tylko jedno przecięcie — między jednym a drugim powstaje
    // szczelina szerokości piksela, widoczna wszędzie tam, gdzie figura
    // wypełniona sąsiaduje z obrysem w innym kolorze. Kosztem jednego dzielenia
    // na krawędź i wiersz więcej figura jest spójna, a zależność
    // „wypełnienie pokrywa własny obrys" staje się gwarancją, nie przypadkiem.
    //
    // Skutkiem ubocznym jest figura szersza o najwyżej jeden piksel na płaskich
    // skosach niż w Adafruit_GFX czy TFT_eSPI. To świadomy wybór.
    struct Edge {
        i16 x0, y0, x1, y1;
    };

    // Normalizacja: każda krawędź biegnie z góry na dół.
    auto edge = [](Point a, Point b) {
        return a.y <= b.y ? Edge{a.x, a.y, b.x, b.y} : Edge{b.x, b.y, a.x, a.y};
    };
    const Edge edges[3] = {edge(p0, p1), edge(p1, p2), edge(p2, p0)};

    const i16 minY = imin(p0.y, imin(p1.y, p2.y));
    const i16 maxY = imax(p0.y, imax(p1.y, p2.y));

    // Położenie krawędzi w wierszu y, z zapasem na obie strony zaokrąglenia.
    auto xAt = [](const Edge& e, i16 y, i32& lo, i32& hi) {
        const i32 dy = e.y1 - e.y0;
        if (dy == 0) {
            lo = hi = e.x0;
            return;
        }
        i32 f = 0, c = 0;
        divFloorCeil(static_cast<i32>(e.x1 - e.x0) * (y - e.y0), dy, f, c);
        lo = e.x0 + f;
        hi = e.x0 + c;
    };

    beginBatch();
    for (i16 y = minY; y <= maxY; ++y) {
        i32  lo    = 0;
        i32  hi    = 0;
        bool found = false;

        for (const Edge& e : edges) {
            if (y < e.y0 || y > e.y1) continue;

            i32 a, b;
            if (e.y0 == e.y1) {
                // Krawędź pozioma pokrywa cały swój odcinek w tym wierszu.
                a = e.x0 < e.x1 ? e.x0 : e.x1;
                b = e.x0 < e.x1 ? e.x1 : e.x0;
            } else {
                // Zakres od położenia w tym wierszu do położenia w następnym —
                // to właśnie on odpowiada ciągowi pikseli rysowanemu przez obrys.
                i32 lo0 = 0, hi0 = 0, lo1 = 0, hi1 = 0;
                xAt(e, y, lo0, hi0);
                if (y < e.y1) {
                    xAt(e, static_cast<i16>(y + 1), lo1, hi1);
                } else {
                    lo1 = hi1 = e.x1;
                }
                a = lo0 < lo1 ? lo0 : lo1;
                b = hi0 > hi1 ? hi0 : hi1;
            }

            if (!found) {
                lo    = a;
                hi    = b;
                found = true;
            } else {
                if (a < lo) lo = a;
                if (b > hi) hi = b;
            }
        }

        if (!found) continue;
        if (auto s = hLine(static_cast<i16>(lo), y, static_cast<i16>(hi - lo + 1), color);
            !s) {
            endBatch();
            return s;
        }
    }

    endBatch();
    return ok();
}

// ---------------------------------------------------------------------------
// Bitmapy
// ---------------------------------------------------------------------------

Status ISurface::drawBitmap1(i16 x, i16 y, const u8* bitmap, i16 w, i16 h, Color fg) {
    if (!bitmap) return fail(Err::BadArgument);

    const i16 bytesPerRow = static_cast<i16>((w + 7) / 8);
    beginBatch();
    for (i16 row = 0; row < h; ++row) {
        for (i16 col = 0; col < w; ++col) {
            const u8 byte = bitmap[row * bytesPerRow + (col / 8)];
            if (byte & (0x80 >> (col % 8))) {
                if (auto s = drawPixel(static_cast<i16>(x + col), static_cast<i16>(y + row), fg);
                    !s) {
                    endBatch();
                    return s;
                }
            }
        }
    }
    endBatch();
    return ok();
}

Status ISurface::drawBitmap1(i16 x, i16 y, const u8* bitmap, i16 w, i16 h, Color fg,
                             Color bg) {
    if (!bitmap) return fail(Err::BadArgument);

    const i16 bytesPerRow = static_cast<i16>((w + 7) / 8);
    beginBatch();
    for (i16 row = 0; row < h; ++row) {
        for (i16 col = 0; col < w; ++col) {
            const u8    byte = bitmap[row * bytesPerRow + (col / 8)];
            const Color c    = (byte & (0x80 >> (col % 8))) ? fg : bg;
            if (auto s = drawPixel(static_cast<i16>(x + col), static_cast<i16>(y + row), c); !s) {
                endBatch();
                return s;
            }
        }
    }
    endBatch();
    return ok();
}

Status ISurface::drawBitmapRgb565(i16 x, i16 y, const u16* pixels, i16 w, i16 h) {
    if (!pixels) return fail(Err::BadArgument);

    beginBatch();
    for (i16 row = 0; row < h; ++row) {
        for (i16 col = 0; col < w; ++col) {
            const Color c = Color::fromRgb565(pixels[row * w + col]);
            if (auto s = drawPixel(static_cast<i16>(x + col), static_cast<i16>(y + row), c); !s) {
                endBatch();
                return s;
            }
        }
    }
    endBatch();
    return ok();
}

// ---------------------------------------------------------------------------
// Tekst
// ---------------------------------------------------------------------------

Status ISurface::drawChar(i16 x, i16 y, char ch, Color fg, const Font& font, u8 scale) {
    if (!font.valid() || scale == 0) return fail(Err::BadArgument);

    const u8* glyph = font.glyph(ch);
    if (!glyph) return ok();  // znak spoza zakresu czcionki po prostu się nie rysuje

    beginBatch();
    for (u8 row = 0; row < font.height; ++row) {
        for (u8 col = 0; col < font.width; ++col) {
            if (!font.pixel(glyph, col, row)) continue;

            const i16 px = static_cast<i16>(x + col * scale);
            const i16 py = static_cast<i16>(y + row * scale);
            const Status s = (scale == 1) ? drawPixel(px, py, fg)
                                          : fillRect(Rect(px, py, scale, scale), fg);
            if (!s) {
                endBatch();
                return s;
            }
        }
    }
    endBatch();
    return ok();
}

Status ISurface::drawText(i16 x, i16 y, const char* text, Color fg, const Font& font,
                          u8 scale) {
    if (!text) return fail(Err::BadArgument);
    if (!font.valid() || scale == 0) return fail(Err::BadArgument);

    i16 cx = x;
    i16 cy = y;

    beginBatch();
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            cx = x;
            cy = static_cast<i16>(cy + font.height * scale);
            continue;
        }
        if (auto s = drawChar(cx, cy, *p, fg, font, scale); !s) {
            endBatch();
            return s;
        }
        cx = static_cast<i16>(cx + font.advance * scale);
    }
    endBatch();
    return ok();
}

Status ISurface::drawText(i16 x, i16 y, const char* text, Color fg, Color bg,
                          const Font& font, u8 scale) {
    if (!text) return fail(Err::BadArgument);
    if (!font.valid() || scale == 0) return fail(Err::BadArgument);

    // Tło malujemy raz pod cały napis — piksel po pikselu byłoby wielokrotnie
    // wolniejsze na panelach z transferem po SPI.
    beginBatch();
    const i16 w = textWidth(font, text, scale);
    const i16 h = textHeight(font, scale);
    Status s = fillRect(Rect(x, y, w, h), bg);
    if (s) s = drawText(x, y, text, fg, font, scale);
    endBatch();
    return s;
}

}  // namespace gfx
}  // namespace hydra
