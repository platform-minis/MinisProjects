/**
 * MinisGfxGxEPD2.h — MinisGfx backend wrapping a GxEPD2 e-paper display.
 *
 * E-paper is the canonical "you can't keep a live framebuffer" case: refresh is
 * slow, partial windows matter, and big panels are drawn in RAM-limited pages.
 * GxEPD2_GFX derives from Adafruit_GFX, so primitives delegate natively; colors
 * are quantised MGColor → black / white (and the accent ink on 3-colour panels).
 *
 * Full-buffer panels (the common small/medium displays) — draw then display():
 *   #include <GxEPD2_BW.h>
 *   #include <MinisGfxGxEPD2.h>
 *
 *   GxEPD2_BW<GxEPD2_290_T94, GxEPD2_290_T94::HEIGHT> epd(GxEPD2_290_T94(CS,DC,RST,BUSY));
 *   MinisGfxGxEPD2 gfx(epd);
 *
 *   void setup() { epd.init(115200); }
 *   void refresh() {
 *     gfx.clear(MG::white);
 *     gfx.drawText(8, 10, "Hello e-paper", MG::black, 2);
 *     gfx.display();          // full refresh
 *     gfx.hibernate();        // cut panel power between updates
 *   }
 *
 * RAM-limited panels — paged drawing (the closure runs once per page):
 *   gfx.paged([&]{
 *     gfx.clear(MG::white);
 *     gfx.drawText(8, 10, "Hello e-paper", MG::black, 2);
 *   });
 */

#pragma once

#include "MinisGfx.h"

#if defined(MINIS_GFX_GXEPD2) || __has_include(<GxEPD2_GFX.h>)
#include <GxEPD2_GFX.h>
#else
#error "MinisGfxGxEPD2.h requires the GxEPD2 library (https://github.com/ZinggJM/GxEPD2). Install it, or define MINIS_GFX_GXEPD2 for a non-standard include path."
#endif

// E-paper ink constants (defined by GxEPD2 — provide fallbacks just in case).
#ifndef GxEPD_BLACK
#define GxEPD_BLACK  0x0000
#define GxEPD_WHITE  0xFFFF
#define GxEPD_RED    0xF800
#define GxEPD_YELLOW 0xFFE0
#endif

class MinisGfxGxEPD2 : public MinisGfx {
  GxEPD2_GFX& _g;
  bool _partial = false;

  // Quantise an RGBA colour to the inks this panel can actually print.
  uint16_t cv(const MGColor& c) const {
    if (_g.epd2.hasColor) {
      // Strong red → accent ink (covers red and 3-colour red/yellow panels).
      if (c.r > 150 && c.g < 110 && c.b < 110) return GxEPD_RED;
      if (c.r > 150 && c.g > 150 && c.b < 110) return GxEPD_YELLOW;
    }
    return c.luma() < 128 ? GxEPD_BLACK : GxEPD_WHITE;
  }
public:
  explicit MinisGfxGxEPD2(GxEPD2_GFX& dev) : _g(dev) {}

  int  width()  const override { return _g.width(); }
  int  height() const override { return _g.height(); }

  // Refresh the panel with whatever has been drawn into the buffer.
  void display() override { _g.display(_partial); }

  void setPartialMode(bool on) { _partial = on; }
  void setFullWindow() { _g.setFullWindow(); }
  void setPartialWindow(int x, int y, int w, int h) { _g.setPartialWindow(x, y, w, h); _partial = true; }
  void hibernate() { _g.hibernate(); }
  void powerOff()  { _g.powerOff(); }

  // Paged drawing for RAM-limited panels. The closure is invoked once per page;
  // keep it pure (just drawing) so each page renders identically.
  template <class Fn>
  void paged(Fn draw) {
    _g.setFullWindow();
    _g.firstPage();
    do { draw(); } while (_g.nextPage());
  }

  void drawPixel(int x, int y, const MGColor& c) override { _g.drawPixel(x, y, cv(c)); }

  void fillScreen(const MGColor& c) override { _g.fillScreen(cv(c)); }
  void drawFastHLine(int x, int y, int w, const MGColor& c) override { _g.drawFastHLine(x, y, w, cv(c)); }
  void drawFastVLine(int x, int y, int h, const MGColor& c) override { _g.drawFastVLine(x, y, h, cv(c)); }
  void drawLine(int x0, int y0, int x1, int y1, const MGColor& c) override { _g.drawLine(x0, y0, x1, y1, cv(c)); }
  void drawRect(int x, int y, int w, int h, const MGColor& c) override { _g.drawRect(x, y, w, h, cv(c)); }
  void fillRect(int x, int y, int w, int h, const MGColor& c) override { _g.fillRect(x, y, w, h, cv(c)); }
  void drawRoundRect(int x, int y, int w, int h, int r, const MGColor& c) override { _g.drawRoundRect(x, y, w, h, r, cv(c)); }
  void fillRoundRect(int x, int y, int w, int h, int r, const MGColor& c) override { _g.fillRoundRect(x, y, w, h, r, cv(c)); }
  void drawCircle(int cx, int cy, int r, const MGColor& c) override { _g.drawCircle(cx, cy, r, cv(c)); }
  void fillCircle(int cx, int cy, int r, const MGColor& c) override { _g.fillCircle(cx, cy, r, cv(c)); }
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const MGColor& c) override { _g.drawTriangle(x0, y0, x1, y1, x2, y2, cv(c)); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const MGColor& c) override { _g.fillTriangle(x0, y0, x1, y1, x2, y2, cv(c)); }

  void drawBitmap(int x, int y, const uint8_t* bm, int w, int h, const MGColor& fg) override {
    _g.drawBitmap(x, y, (uint8_t*)bm, w, h, cv(fg));
  }
  void drawBitmap(int x, int y, const uint8_t* bm, int w, int h, const MGColor& fg, const MGColor& bg) override {
    _g.drawBitmap(x, y, (uint8_t*)bm, w, h, cv(fg), cv(bg));
  }
  void drawRGBBitmap(int x, int y, const uint16_t* px, int w, int h) override {
    _g.drawRGBBitmap(x, y, (uint16_t*)px, w, h);
  }

  void drawChar(int x, int y, char ch, const MGColor& fg, int size) override {
    _g.drawChar(x, y, ch, cv(fg), _hasTextBg ? cv(_bg) : cv(fg), size);
  }
  void drawText(int x, int y, const char* s, const MGColor& fg, int size = 1) override {
    _g.setTextWrap(false);
    _g.setTextSize(size < 1 ? 1 : size);
    if (_hasTextBg) _g.setTextColor(cv(fg), cv(_bg)); else _g.setTextColor(cv(fg));
    _g.setCursor(x, y);
    if (s) _g.print(s);
  }
  void write(char ch) override {
    _g.setTextWrap(_wrap);
    _g.setTextSize(_textSize);
    if (_hasTextBg) _g.setTextColor(cv(_fg), cv(_bg)); else _g.setTextColor(cv(_fg));
    _g.setCursor(_cx, _cy);
    _g.write((uint8_t)ch);
    _cx = _g.getCursorX();
    _cy = _g.getCursorY();
  }
};
