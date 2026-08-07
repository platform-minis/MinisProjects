#pragma once
/**
 * Hydra — geometria warstwy graficznej.
 *
 * Współrzędne są liczbami ze znakiem: rysowanie częściowo poza ekranem to
 * normalna sytuacja (przewijana lista, animacja wjeżdżająca z boku), a nie
 * błąd. Typy bez znaku wymuszałyby w każdym takim miejscu rzutowania i były
 * źródłem klasycznych pomyłek przy zawijaniu do zera.
 */

#include "hydra/core/Types.hpp"

namespace hydra {
namespace gfx {

struct Point {
    i16 x = 0;
    i16 y = 0;

    constexpr Point() = default;
    constexpr Point(i16 px, i16 py) : x(px), y(py) {}

    constexpr Point operator+(Point o) const {
        return Point(static_cast<i16>(x + o.x), static_cast<i16>(y + o.y));
    }
    constexpr Point operator-(Point o) const {
        return Point(static_cast<i16>(x - o.x), static_cast<i16>(y - o.y));
    }
    constexpr bool operator==(Point o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(Point o) const { return !(*this == o); }
};

struct Size {
    i16 w = 0;
    i16 h = 0;

    constexpr Size() = default;
    constexpr Size(i16 width, i16 height) : w(width), h(height) {}

    constexpr bool empty() const { return w <= 0 || h <= 0; }
    constexpr i32  area() const { return static_cast<i32>(w) * h; }
    constexpr bool operator==(Size o) const { return w == o.w && h == o.h; }
};

/** Prostokąt opisany lewym górnym rogiem i rozmiarem. */
struct Rect {
    i16 x = 0;
    i16 y = 0;
    i16 w = 0;
    i16 h = 0;

    constexpr Rect() = default;
    constexpr Rect(i16 rx, i16 ry, i16 rw, i16 rh) : x(rx), y(ry), w(rw), h(rh) {}
    constexpr Rect(Point p, Size s) : x(p.x), y(p.y), w(s.w), h(s.h) {}

    /** Prostokąt z dwóch narożników; kolejność punktów nie ma znaczenia. */
    static constexpr Rect fromCorners(i16 x0, i16 y0, i16 x1, i16 y1) {
        return Rect(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
                    static_cast<i16>((x0 < x1 ? x1 - x0 : x0 - x1) + 1),
                    static_cast<i16>((y0 < y1 ? y1 - y0 : y0 - y1) + 1));
    }

    constexpr bool empty() const { return w <= 0 || h <= 0; }
    constexpr i16  right() const { return static_cast<i16>(x + w - 1); }
    constexpr i16  bottom() const { return static_cast<i16>(y + h - 1); }
    constexpr Point origin() const { return Point(x, y); }
    constexpr Size  size() const { return Size(w, h); }

    constexpr bool contains(i16 px, i16 py) const {
        return !empty() && px >= x && py >= y && px < x + w && py < y + h;
    }
    constexpr bool contains(Point p) const { return contains(p.x, p.y); }

    /** Część wspólna. Pusty wynik oznacza brak przecięcia. */
    constexpr Rect intersect(Rect o) const {
        const i16 nx = x > o.x ? x : o.x;
        const i16 ny = y > o.y ? y : o.y;
        const i16 nr = right() < o.right() ? right() : o.right();
        const i16 nb = bottom() < o.bottom() ? bottom() : o.bottom();
        return (nr < nx || nb < ny) ? Rect()
                                    : Rect(nx, ny, static_cast<i16>(nr - nx + 1),
                                           static_cast<i16>(nb - ny + 1));
    }

    /** Najmniejszy prostokąt obejmujący oba. Pusty argument jest pomijany. */
    constexpr Rect unite(Rect o) const {
        return empty()   ? o
             : o.empty() ? *this
                         : fromCorners(x < o.x ? x : o.x, y < o.y ? y : o.y,
                                       right() > o.right() ? right() : o.right(),
                                       bottom() > o.bottom() ? bottom() : o.bottom());
    }

    /** Powiększenie (dodatnie) albo zmniejszenie (ujemne) o zadany margines. */
    constexpr Rect inflated(i16 by) const {
        return Rect(static_cast<i16>(x - by), static_cast<i16>(y - by),
                    static_cast<i16>(w + 2 * by), static_cast<i16>(h + 2 * by));
    }

    constexpr Rect translated(i16 dx, i16 dy) const {
        return Rect(static_cast<i16>(x + dx), static_cast<i16>(y + dy), w, h);
    }

    constexpr bool intersects(Rect o) const { return !intersect(o).empty(); }

    constexpr bool operator==(Rect o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
    constexpr bool operator!=(Rect o) const { return !(*this == o); }
};

}  // namespace gfx
}  // namespace hydra
