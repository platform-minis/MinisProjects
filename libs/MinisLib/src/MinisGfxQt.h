/**
 * MinisGfxQt.h — MinisGfx backend rendering through the MinisQt toolkit (libs/Qt).
 *
 * Reuses MinisQt's QPainter (anti-aliased-ish rounded rects, alpha-blended
 * pixels, the shared 8x8 font) as a MinisGfx backend, so the same scene code can
 * render through the Qt pipeline. Two modes:
 *
 *   • Owning — allocates its own RGBA8888 buffer and a QPainter over it, and
 *     presents via minis_canvas_present() (the MyCastle WASM simulator path).
 *
 *         #include <MinisQt.h>
 *         #include <MinisGfxQt.h>
 *         MinisGfxQt gfx(320, 240);
 *         void loop() {
 *           gfx.clear(MG::black);
 *           gfx.fillRoundRect(20, 20, 120, 60, 8, MG::blue);
 *           gfx.drawText(30, 36, "Qt backend", MG::white, 2);
 *           gfx.display();                 // blit to the canvas
 *         }
 *
 *   • Borrowing — wraps a QPainter handed to you inside a QWidget::paintEvent so
 *     you can mix MinisGfx primitives into a MinisQt widget tree:
 *
 *         void paintEvent(QPainter& p) override {
 *           MinisGfxQt g(p, width(), height());
 *           g.drawText(4, 4, "custom", MG::white, 1);
 *         }
 *
 * Primitives QPainter implements natively (pixel, rect, rounded fill, line, text)
 * are delegated; the rest (circle, triangle, outlines, bitmaps) fall back to the
 * MinisGfx software path, which still draws through QPainter::setPixel.
 */

#pragma once

#include "MinisGfx.h"

#if defined(MINIS_GFX_QT) || __has_include(<MinisQt.h>)
#include <MinisQt.h>
#else
#error "MinisGfxQt.h requires the MinisQt library (libs/Qt). Install it, or define MINIS_GFX_QT for a non-standard include path."
#endif

class MinisGfxQt : public MinisGfx {
  uint32_t* _buf = nullptr;
  bool _owns = false;
  int _w, _h;
  QPainter* _qp = nullptr;   // borrowed or owned painter
  QPainter  _ownedPainter;   // valid only in owning mode

  static QColor toQ(const MGColor& c) { return QColor(c.r, c.g, c.b, c.a); }
public:
  // Owning mode: own buffer + painter, present via the canvas ABI.
  MinisGfxQt(int w, int h)
    : _buf((uint32_t*)malloc(sizeof(uint32_t) * (size_t)w * h)), _owns(true),
      _w(w), _h(h), _ownedPainter(_buf, w, h) {
    _qp = &_ownedPainter;
  }
  // Borrowing mode: draw through a painter you already hold (e.g. a paintEvent).
  MinisGfxQt(QPainter& painter, int w, int h)
    : _w(w), _h(h), _ownedPainter(nullptr, 0, 0) {
    _qp = &painter;
  }
  ~MinisGfxQt() override { if (_owns) free(_buf); }

  // Drain the canvas pointer queue (owning/WASM mode). Returns 0 when empty.
  // type: 1=press 2=move 3=release.
  static int pollPointer(int* type, int* x, int* y) { return minis_canvas_poll(type, x, y); }

  int  width()  const override { return _w; }
  int  height() const override { return _h; }
  void display() override { if (_owns) minis_canvas_present(_buf, _w, _h); }

  void drawPixel(int x, int y, const MGColor& c) override { _qp->setPixel(x, y, toQ(c)); }

  void fillScreen(const MGColor& c) override { _qp->fillRect(QRect(0, 0, _w, _h), toQ(c)); }
  void fillRect(int x, int y, int w, int h, const MGColor& c) override { _qp->fillRect(QRect(x, y, w, h), toQ(c)); }
  void drawRect(int x, int y, int w, int h, const MGColor& c) override { _qp->drawRect(QRect(x, y, w, h), toQ(c), 1); }
  void fillRoundRect(int x, int y, int w, int h, int r, const MGColor& c) override { _qp->fillRoundedRect(QRect(x, y, w, h), r, toQ(c)); }
  void drawLine(int x0, int y0, int x1, int y1, const MGColor& c) override { _qp->drawLine(x0, y0, x1, y1, toQ(c)); }
  void drawFastHLine(int x, int y, int w, const MGColor& c) override { _qp->drawLine(x, y, x + w - 1, y, toQ(c)); }
  void drawFastVLine(int x, int y, int h, const MGColor& c) override { _qp->drawLine(x, y, x, y + h - 1, toQ(c)); }

  // Text via QPainter's 8x8 font (scaled by integer pixel size).
  void drawChar(int x, int y, char ch, const MGColor& fg, int size) override {
    char s[2] = { ch, 0 };
    _qp->setPen(toQ(fg));
    _qp->setFont(QFont(8 * (size < 1 ? 1 : size)));
    _qp->drawTextAt(x, y, s);
  }
  void drawText(int x, int y, const char* s, const MGColor& fg, int size = 1) override {
    if (!s) return;
    _qp->setPen(toQ(fg));
    _qp->setFont(QFont(8 * (size < 1 ? 1 : size)));
    _qp->drawTextAt(x, y, s);
  }
  // circle / fillCircle / triangle / roundrect-outline / bitmaps fall back to the
  // MinisGfx software defaults, which render through drawPixel -> QPainter.
};
