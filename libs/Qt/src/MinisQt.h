#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MinisQt — a small, header-only, Qt-inspired UI toolkit.
//
// API names mirror Qt 1:1 (QWidget, QPushButton, QSlider, QLabel, QProgressBar,
// QCheckBox, QVBoxLayout/QHBoxLayout, QPainter, QColor, QRect, Signal). Widgets
// are drawn in retained mode into an RGBA8888 framebuffer.
//
// Rendering / input backend:
//   • In the MyCastle WASM simulator (__EMSCRIPTEN__) the framebuffer is blitted
//     to the browser <canvas> via minis_canvas_present(), and pointer input is
//     drained from minis_canvas_poll() — both provided by the Arduino WASM mock.
//   • On real hardware, provide strong definitions of those two C functions to
//     push the framebuffer to your display and feed touch events. The default
//     weak stubs simply do nothing, so a sketch still links and runs.
//
// Typical use from an Arduino sketch:
//   QApplication app(320, 240);
//   QVBoxLayout* lay = new QVBoxLayout();
//   QPushButton* btn = new QPushButton("Tap me", app.root());
//   btn->clicked.connect([]{ Serial.println("clicked"); });
//   lay->addWidget(btn);
//   app.root()->setLayout(lay);
//   // in loop(): app.tick();  delay(33);
// ─────────────────────────────────────────────────────────────────────────────

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>
#include <functional>

// ── Rendering / input backend ────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__
extern "C" {
  void minis_canvas_present(const uint32_t* pixels, int width, int height);
  int  minis_canvas_poll(int* type, int* x, int* y);
}
#else
// Weak no-op defaults — override in your project to drive a real display.
extern "C" {
  __attribute__((weak)) void minis_canvas_present(const uint32_t*, int, int) {}
  __attribute__((weak)) int  minis_canvas_poll(int*, int*, int*) { return 0; }
}
#endif

// ── Qt enums ──────────────────────────────────────────────────────────────────
namespace Qt {
  enum AlignmentFlag {
    AlignLeft    = 0x0001,
    AlignRight   = 0x0002,
    AlignHCenter = 0x0004,
    AlignTop     = 0x0020,
    AlignBottom  = 0x0040,
    AlignVCenter = 0x0080,
    AlignCenter  = AlignHCenter | AlignVCenter,
  };
  enum Orientation { Horizontal = 1, Vertical = 2 };
}

// ── QColor ────────────────────────────────────────────────────────────────────
class QColor {
public:
  uint8_t r = 0, g = 0, b = 0, a = 255;

  QColor() {}
  QColor(int red, int green, int blue, int alpha = 255)
    : r((uint8_t)red), g((uint8_t)green), b((uint8_t)blue), a((uint8_t)alpha) {}

  static QColor fromRgb(int r, int g, int b, int a = 255) { return QColor(r, g, b, a); }

  // Parse "#rgb" or "#rrggbb" (alpha defaults to opaque).
  static QColor fromString(const char* s) {
    if (!s || s[0] != '#') return QColor();
    auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
    };
    int len = 0; while (s[len]) len++;
    if (len == 4) return QColor(hex(s[1]) * 17, hex(s[2]) * 17, hex(s[3]) * 17);
    if (len >= 7) return QColor(hex(s[1]) * 16 + hex(s[2]),
                                hex(s[3]) * 16 + hex(s[4]),
                                hex(s[5]) * 16 + hex(s[6]));
    return QColor();
  }

  // Packed as 0xAABBGGRR so the in-memory byte order is R,G,B,A — what the
  // browser ImageData (and most RGBA displays) expect.
  uint32_t rgba() const {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
  }

  QColor lighter(int pct = 130) const {
    auto f = [pct](uint8_t c) { int v = c * pct / 100; return (uint8_t)(v > 255 ? 255 : v); };
    return QColor(f(r), f(g), f(b), a);
  }
  QColor darker(int pct = 130) const {
    auto f = [pct](uint8_t c) { return (uint8_t)(c * 100 / pct); };
    return QColor(f(r), f(g), f(b), a);
  }
};

namespace Qt {
  inline const QColor white(255, 255, 255);
  inline const QColor black(0, 0, 0);
  inline const QColor red(220, 50, 50);
  inline const QColor green(60, 180, 90);
  inline const QColor blue(60, 120, 200);
  inline const QColor gray(128, 128, 128);
  inline const QColor darkGray(64, 64, 64);
  inline const QColor lightGray(200, 200, 200);
  inline const QColor transparent(0, 0, 0, 0);
}

// ── Geometry ──────────────────────────────────────────────────────────────────
class QPoint {
  int _x = 0, _y = 0;
public:
  QPoint() {}
  QPoint(int x, int y) : _x(x), _y(y) {}
  int x() const { return _x; }
  int y() const { return _y; }
  void setX(int v) { _x = v; }
  void setY(int v) { _y = v; }
};

class QSize {
  int _w = 0, _h = 0;
public:
  QSize() {}
  QSize(int w, int h) : _w(w), _h(h) {}
  int width() const { return _w; }
  int height() const { return _h; }
  void setWidth(int v) { _w = v; }
  void setHeight(int v) { _h = v; }
};

class QRect {
  int _x = 0, _y = 0, _w = 0, _h = 0;
public:
  QRect() {}
  QRect(int x, int y, int w, int h) : _x(x), _y(y), _w(w), _h(h) {}
  static QRect of(int x, int y, int w, int h) { return QRect(x, y, w, h); }

  int x() const { return _x; }
  int y() const { return _y; }
  int width() const { return _w; }
  int height() const { return _h; }
  int left() const { return _x; }
  int top() const { return _y; }
  int right() const { return _x + _w - 1; }
  int bottom() const { return _y + _h - 1; }
  QPoint center() const { return QPoint(_x + _w / 2, _y + _h / 2); }
  QSize size() const { return QSize(_w, _h); }

  void setRect(int x, int y, int w, int h) { _x = x; _y = y; _w = w; _h = h; }
  bool contains(const QPoint& p) const {
    return p.x() >= _x && p.x() < _x + _w && p.y() >= _y && p.y() < _y + _h;
  }
  QRect adjusted(int dx1, int dy1, int dx2, int dy2) const {
    return QRect(_x + dx1, _y + dy1, _w - dx1 + dx2, _h - dy1 + dy2);
  }
};

// ── QFont ─────────────────────────────────────────────────────────────────────
class QFont {
  int _size = 16;
  bool _bold = false;
public:
  QFont() {}
  explicit QFont(int pixelSize, bool bold = false) : _size(pixelSize), _bold(bold) {}
  int pixelSize() const { return _size; }
  void setPixelSize(int s) { _size = s; }
  bool bold() const { return _bold; }
  void setBold(bool b) { _bold = b; }
  // Built-in glyphs are 8x8; integer-scale them to approximate a pixel size.
  int scale() const { int s = (_size + 4) / 8; return s < 1 ? 1 : s; }
};

// ── 8x8 bitmap font (printable ASCII 0x20..0x7F) ──────────────────────────────
// Public-domain font8x8_basic (dhepper). bit 0 = leftmost column of each row.
inline const uint8_t QT_FONT8X8[96][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
  {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // !
  {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // "
  {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // #
  {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // $
  {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // %
  {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // &
  {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '
  {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // (
  {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // )
  {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // *
  {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // +
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // ,
  {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // -
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // .
  {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // /
  {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 0
  {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 1
  {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 2
  {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 3
  {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 4
  {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 5
  {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 6
  {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 7
  {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 8
  {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 9
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // :
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // ;
  {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // <
  {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // =
  {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // >
  {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // ?
  {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // @
  {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // A
  {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // B
  {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // C
  {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // D
  {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // E
  {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // F
  {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // G
  {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // H
  {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // I
  {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // J
  {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // K
  {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // L
  {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // M
  {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // N
  {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // O
  {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // P
  {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // Q
  {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // R
  {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // S
  {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // T
  {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // U
  {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // V
  {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W
  {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // X
  {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // Y
  {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // Z
  {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // [
  {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // backslash
  {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // ]
  {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // ^
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // _
  {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // `
  {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // a
  {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // b
  {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // c
  {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // d
  {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // e
  {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // f
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // g
  {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // h
  {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // i
  {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // j
  {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // k
  {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // l
  {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // m
  {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // n
  {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // o
  {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // p
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // q
  {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // r
  {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // s
  {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // t
  {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // u
  {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // v
  {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // w
  {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // x
  {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // y
  {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // z
  {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // {
  {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // |
  {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // }
  {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // ~
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x7F
};

// ── QPainter ──────────────────────────────────────────────────────────────────
class QPainter {
  uint32_t* _buf; int _W, _H;
  int _ox = 0, _oy = 0;             // absolute origin for the widget being painted
  QColor _pen = Qt::white;
  QColor _brush = Qt::transparent;
  QFont _font;
public:
  QPainter(uint32_t* buf, int W, int H) : _buf(buf), _W(W), _H(H) {}

  void setOrigin(int x, int y) { _ox = x; _oy = y; }
  void setPen(const QColor& c) { _pen = c; }
  void setBrush(const QColor& c) { _brush = c; }
  void setFont(const QFont& f) { _font = f; }
  QFont font() const { return _font; }

  void setPixel(int x, int y, const QColor& c) {
    int px = x + _ox, py = y + _oy;
    if (px < 0 || py < 0 || px >= _W || py >= _H || c.a == 0) return;
    uint32_t* dst = &_buf[py * _W + px];
    if (c.a == 255) { *dst = c.rgba(); return; }
    uint32_t d = *dst;
    uint8_t dr = d & 0xFF, dg = (d >> 8) & 0xFF, db = (d >> 16) & 0xFF;
    int a = c.a;
    uint8_t nr = (uint8_t)((c.r * a + dr * (255 - a)) / 255);
    uint8_t ng = (uint8_t)((c.g * a + dg * (255 - a)) / 255);
    uint8_t nb = (uint8_t)((c.b * a + db * (255 - a)) / 255);
    *dst = (uint32_t)nr | ((uint32_t)ng << 8) | ((uint32_t)nb << 16) | (0xFFu << 24);
  }

  void fillRect(const QRect& r, const QColor& c) {
    for (int j = 0; j < r.height(); j++)
      for (int i = 0; i < r.width(); i++)
        setPixel(r.x() + i, r.y() + j, c);
  }

  void drawRect(const QRect& r, const QColor& c, int thickness = 1) {
    for (int t = 0; t < thickness; t++) {
      for (int i = 0; i < r.width(); i++) {
        setPixel(r.x() + i, r.y() + t, c);
        setPixel(r.x() + i, r.y() + r.height() - 1 - t, c);
      }
      for (int j = 0; j < r.height(); j++) {
        setPixel(r.x() + t, r.y() + j, c);
        setPixel(r.x() + r.width() - 1 - t, r.y() + j, c);
      }
    }
  }

  void fillRoundedRect(const QRect& r, int radius, const QColor& c) {
    int w = r.width(), h = r.height();
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    int rr = radius * radius;
    for (int j = 0; j < h; j++) {
      for (int i = 0; i < w; i++) {
        int cx = -1, cy = -1;
        if (i < radius && j < radius) { cx = radius; cy = radius; }
        else if (i >= w - radius && j < radius) { cx = w - radius - 1; cy = radius; }
        else if (i < radius && j >= h - radius) { cx = radius; cy = h - radius - 1; }
        else if (i >= w - radius && j >= h - radius) { cx = w - radius - 1; cy = h - radius - 1; }
        if (cx >= 0) {
          int dx = i - cx, dy = j - cy;
          if (dx * dx + dy * dy > rr) continue;
        }
        setPixel(r.x() + i, r.y() + j, c);
      }
    }
  }

  void drawLine(int x0, int y0, int x1, int y1, const QColor& c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      setPixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  void drawTextAt(int x, int y, const char* s) {
    int sc = _font.scale();
    for (const char* p = s; *p; p++) {
      unsigned char ch = (unsigned char)*p;
      if (ch >= 0x20 && ch <= 0x7F) {
        const uint8_t* g = QT_FONT8X8[ch - 0x20];
        for (int row = 0; row < 8; row++) {
          uint8_t bits = g[row];
          for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
              for (int yy = 0; yy < sc; yy++)
                for (int xx = 0; xx < sc; xx++)
                  setPixel(x + col * sc + xx, y + row * sc + yy, _pen);
            }
          }
        }
      }
      x += 8 * sc;
    }
  }

  int textWidth(const char* s) const {
    int n = 0; for (const char* p = s; *p; p++) n++;
    return n * 8 * _font.scale();
  }

  void drawText(const QRect& r, int align, const char* s) {
    int sc = _font.scale();
    int tw = textWidth(s), th = 8 * sc;
    int tx = r.x(), ty = r.y();
    if (align & Qt::AlignHCenter) tx = r.x() + (r.width() - tw) / 2;
    else if (align & Qt::AlignRight) tx = r.x() + r.width() - tw;
    if (align & Qt::AlignVCenter) ty = r.y() + (r.height() - th) / 2;
    else if (align & Qt::AlignBottom) ty = r.y() + r.height() - th;
    drawTextAt(tx, ty, s);
  }
};

// ── Signal ────────────────────────────────────────────────────────────────────
template <typename... Args>
class Signal {
  std::vector<std::function<void(Args...)>> _slots;
public:
  void connect(std::function<void(Args...)> fn) { _slots.push_back(std::move(fn)); }
  void emit(Args... args) { for (auto& s : _slots) s(args...); }
};

// ── QObject ───────────────────────────────────────────────────────────────────
class QObject {
public:
  virtual ~QObject() {}
};

// ── QLayout ───────────────────────────────────────────────────────────────────
class QWidget;  // fwd

class QLayout {
protected:
  std::vector<QWidget*> _items;
  int _spacing = 6;
  int _margin = 8;
public:
  virtual ~QLayout() {}
  void addWidget(QWidget* w) { _items.push_back(w); }
  void setSpacing(int s) { _spacing = s; }
  void setContentsMargins(int m) { _margin = m; }
  virtual void setGeometry(const QRect& r) = 0;
};

class QBoxLayout : public QLayout {
  bool _vertical;
public:
  explicit QBoxLayout(Qt::Orientation o) : _vertical(o == Qt::Vertical) {}
  void setGeometry(const QRect& r) override; // defined after QWidget
};

class QVBoxLayout : public QBoxLayout {
public:
  QVBoxLayout() : QBoxLayout(Qt::Vertical) {}
};
class QHBoxLayout : public QBoxLayout {
public:
  QHBoxLayout() : QBoxLayout(Qt::Horizontal) {}
};

// ── QWidget ───────────────────────────────────────────────────────────────────
class QWidget : public QObject {
protected:
  QRect _geom;
  bool _visible = true;
  QWidget* _parent = nullptr;
  std::vector<QWidget*> _children;
  QColor _bg = Qt::transparent;
  bool _hasBg = false;
  QLayout* _layout = nullptr;
  QFont _font;
public:
  explicit QWidget(QWidget* parent = nullptr) {
    if (parent) parent->addChild(this);
  }

  void addChild(QWidget* w) { w->_parent = this; _children.push_back(w); }

  void setGeometry(const QRect& r) {
    _geom = r;
    if (_layout) _layout->setGeometry(QRect(0, 0, _geom.width(), _geom.height()));
  }
  void setGeometry(int x, int y, int w, int h) { setGeometry(QRect(x, y, w, h)); }
  QRect geometry() const { return _geom; }
  QRect localRect() const { return QRect(0, 0, _geom.width(), _geom.height()); }
  int width() const { return _geom.width(); }
  int height() const { return _geom.height(); }
  void resize(int w, int h) { setGeometry(_geom.x(), _geom.y(), w, h); }
  void move(int x, int y) { setGeometry(x, y, _geom.width(), _geom.height()); }

  void setVisible(bool v) { _visible = v; }
  bool isVisible() const { return _visible; }
  void show() { setVisible(true); }
  void hide() { setVisible(false); }

  void setBackground(const QColor& c) { _bg = c; _hasBg = true; }
  void setFont(const QFont& f) { _font = f; }
  QFont font() const { return _font; }

  void setLayout(QLayout* l) {
    _layout = l;
    if (_layout) _layout->setGeometry(QRect(0, 0, _geom.width(), _geom.height()));
  }

  int absX() const { int a = _geom.x(); const QWidget* w = _parent; while (w) { a += w->_geom.x(); w = w->_parent; } return a; }
  int absY() const { int a = _geom.y(); const QWidget* w = _parent; while (w) { a += w->_geom.y(); w = w->_parent; } return a; }

  // Deepest visible widget containing the global point (nullptr if none).
  QWidget* childAt(int gx, int gy) {
    if (!_visible) return nullptr;
    QRect abs(absX(), absY(), width(), height());
    if (!abs.contains(QPoint(gx, gy))) return nullptr;
    for (int i = (int)_children.size() - 1; i >= 0; i--) {
      QWidget* hit = _children[i]->childAt(gx, gy);
      if (hit) return hit;
    }
    return this;
  }

  void paintTree(QPainter& p) {
    if (!_visible) return;
    p.setOrigin(absX(), absY());
    if (_hasBg) p.fillRect(localRect(), _bg);
    paintEvent(p);
    for (auto* c : _children) c->paintTree(p);
  }

  // Overridable Qt-style handlers. Coords are LOCAL to the widget.
  virtual void paintEvent(QPainter&) {}
  virtual void mousePressEvent(int /*x*/, int /*y*/) {}
  virtual void mouseMoveEvent(int /*x*/, int /*y*/) {}
  virtual void mouseReleaseEvent(int /*x*/, int /*y*/) {}
};

inline void QBoxLayout::setGeometry(const QRect& r) {
  int n = (int)_items.size();
  if (n == 0) return;
  int ix = r.x() + _margin, iy = r.y() + _margin;
  int iw = r.width() - 2 * _margin, ih = r.height() - 2 * _margin;
  if (_vertical) {
    int avail = ih - _spacing * (n - 1);
    int each = avail / n;
    int y = iy;
    for (int i = 0; i < n; i++) {
      int h = (i == n - 1) ? (iy + ih - y) : each;
      _items[i]->setGeometry(ix, y, iw, h);
      y += h + _spacing;
    }
  } else {
    int avail = iw - _spacing * (n - 1);
    int each = avail / n;
    int x = ix;
    for (int i = 0; i < n; i++) {
      int w = (i == n - 1) ? (ix + iw - x) : each;
      _items[i]->setGeometry(x, iy, w, ih);
      x += w + _spacing;
    }
  }
}

// ── QLabel ────────────────────────────────────────────────────────────────────
class QLabel : public QWidget {
  std::string _text;
  QColor _fg = Qt::white;
  int _align = Qt::AlignLeft | Qt::AlignVCenter;
public:
  explicit QLabel(const std::string& text, QWidget* parent = nullptr)
    : QWidget(parent), _text(text) {}
  void setText(const std::string& t) { _text = t; }
  std::string text() const { return _text; }
  void setAlignment(int a) { _align = a; }
  void setColor(const QColor& c) { _fg = c; }
  void paintEvent(QPainter& p) override {
    p.setPen(_fg);
    p.setFont(_font);
    p.drawText(localRect(), _align, _text.c_str());
  }
};

// ── QPushButton ───────────────────────────────────────────────────────────────
class QPushButton : public QWidget {
  std::string _text;
  bool _pressed = false;
public:
  Signal<> clicked;
  QColor color = Qt::blue;
  explicit QPushButton(const std::string& text, QWidget* parent = nullptr)
    : QWidget(parent), _text(text) {}
  void setText(const std::string& t) { _text = t; }
  std::string text() const { return _text; }

  void mousePressEvent(int, int) override { _pressed = true; }
  void mouseReleaseEvent(int x, int y) override {
    bool was = _pressed;
    _pressed = false;
    if (was && localRect().contains(QPoint(x, y))) clicked.emit();
  }
  void paintEvent(QPainter& p) override {
    QColor base = _pressed ? color.darker(140) : color;
    p.fillRoundedRect(localRect(), 6, base);
    p.setPen(Qt::white);
    p.setFont(_font);
    p.drawText(localRect(), Qt::AlignCenter, _text.c_str());
  }
};

// ── QAbstractSlider / QSlider (horizontal) ────────────────────────────────────
class QSlider : public QWidget {
  int _min = 0, _max = 100, _value = 0;
  bool _drag = false;
public:
  Signal<int> valueChanged;
  explicit QSlider(QWidget* parent = nullptr) : QWidget(parent) {}

  void setRange(int lo, int hi) { _min = lo; _max = hi; setValue(_value); }
  void setValue(int v) {
    if (v < _min) v = _min;
    if (v > _max) v = _max;
    if (v != _value) { _value = v; valueChanged.emit(_value); }
  }
  int value() const { return _value; }

  void mousePressEvent(int x, int) override { _drag = true; setFromX(x); }
  void mouseMoveEvent(int x, int) override { if (_drag) setFromX(x); }
  void mouseReleaseEvent(int, int) override { _drag = false; }

  void paintEvent(QPainter& p) override {
    int h = height(), w = width();
    int cy = h / 2;
    p.fillRoundedRect(QRect(0, cy - 3, w, 6), 3, Qt::darkGray);
    int span = (_max > _min) ? (_max - _min) : 1;
    int hx = (_value - _min) * (w - 12) / span + 6;
    p.fillRoundedRect(QRect(0, cy - 3, hx, 6), 3, Qt::blue);
    p.fillRoundedRect(QRect(hx - 6, cy - 9, 12, 18), 6, Qt::white);
  }
private:
  void setFromX(int x) {
    int w = width();
    if (w <= 1) return;
    int span = (_max > _min) ? (_max - _min) : 1;
    int v = _min + (x - 6) * span / (w - 12 > 0 ? w - 12 : 1);
    setValue(v);
  }
};

// ── QProgressBar ──────────────────────────────────────────────────────────────
class QProgressBar : public QWidget {
  int _min = 0, _max = 100, _value = 0;
  bool _showText = true;
public:
  explicit QProgressBar(QWidget* parent = nullptr) : QWidget(parent) {}
  void setRange(int lo, int hi) { _min = lo; _max = hi; }
  void setValue(int v) {
    if (v < _min) v = _min;
    if (v > _max) v = _max;
    _value = v;
  }
  int value() const { return _value; }
  void setTextVisible(bool b) { _showText = b; }

  void paintEvent(QPainter& p) override {
    p.fillRoundedRect(localRect(), 4, Qt::darkGray);
    int span = (_max > _min) ? (_max - _min) : 1;
    int fw = (_value - _min) * width() / span;
    if (fw > 0) p.fillRoundedRect(QRect(0, 0, fw, height()), 4, Qt::green);
    if (_showText) {
      char buf[8];
      int pct = (_value - _min) * 100 / span;
      snprintf(buf, sizeof(buf), "%d%%", pct);
      p.setPen(Qt::white);
      p.setFont(_font);
      p.drawText(localRect(), Qt::AlignCenter, buf);
    }
  }
};

// ── QCheckBox ─────────────────────────────────────────────────────────────────
class QCheckBox : public QWidget {
  std::string _text;
  bool _checked = false;
public:
  Signal<bool> toggled;
  explicit QCheckBox(const std::string& text, QWidget* parent = nullptr)
    : QWidget(parent), _text(text) {}
  void setChecked(bool c) { if (c != _checked) { _checked = c; toggled.emit(_checked); } }
  bool isChecked() const { return _checked; }

  void mouseReleaseEvent(int x, int y) override {
    if (localRect().contains(QPoint(x, y))) setChecked(!_checked);
  }
  void paintEvent(QPainter& p) override {
    int s = height() > 8 ? 20 : height();
    int by = (height() - s) / 2;
    p.fillRoundedRect(QRect(0, by, s, s), 4, Qt::darkGray);
    if (_checked) p.fillRoundedRect(QRect(3, by + 3, s - 6, s - 6), 3, Qt::green);
    p.setPen(Qt::white);
    p.setFont(_font);
    p.drawText(QRect(s + 8, 0, width() - s - 8, height()), Qt::AlignLeft | Qt::AlignVCenter, _text.c_str());
  }
};

// ── QApplication ──────────────────────────────────────────────────────────────
class QApplication {
  int _w, _h;
  uint32_t* _fb;
  QWidget _root;
  QWidget* _grab = nullptr;
  QColor _bg = QColor(24, 26, 30);
public:
  QApplication(int width, int height) : _w(width), _h(height) {
    _fb = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)_w * _h);
    _root.setGeometry(0, 0, _w, _h);
  }
  ~QApplication() { free(_fb); }

  QWidget* root() { return &_root; }
  int width() const { return _w; }
  int height() const { return _h; }
  void setBackground(const QColor& c) { _bg = c; }

  // Drain queued pointer events and route them to widgets.
  void processEvents() {
    int t, x, y;
    while (minis_canvas_poll(&t, &x, &y)) {
      if (t == 1) {
        _grab = _root.childAt(x, y);
        if (_grab) _grab->mousePressEvent(x - _grab->absX(), y - _grab->absY());
      } else if (t == 2) {
        if (_grab) _grab->mouseMoveEvent(x - _grab->absX(), y - _grab->absY());
      } else if (t == 3) {
        if (_grab) { _grab->mouseReleaseEvent(x - _grab->absX(), y - _grab->absY()); _grab = nullptr; }
      }
    }
  }

  // Repaint the whole tree and blit to the display.
  void render() {
    QPainter p(_fb, _w, _h);
    p.fillRect(QRect(0, 0, _w, _h), _bg);
    _root.paintTree(p);
    minis_canvas_present(_fb, _w, _h);
  }

  // Call once per Arduino loop(): handle input, redraw, present.
  void tick() {
    processEvents();
    render();
  }
};
