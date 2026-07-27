/**
 * MinisGfxLovyan.h — MinisGfx backend wrapping a LovyanGFX device.
 *
 * LovyanGFX drives a huge range of SPI/parallel TFTs and is fast; wrap it when
 * you want the same MinisGfx scene code on a LovyanGFX panel. Colours are passed
 * as 24-bit RGB888 (uint32_t) so LovyanGFX converts to the panel's native depth
 * — correct on 16-bit and 24-bit displays alike.
 *
 *   #include <LovyanGFX.hpp>          // or your board's LGFX config header
 *   #include <MinisGfxLovyan.h>
 *
 *   LGFX           lcd;               // your lgfx::LGFX_Device subclass
 *   MinisGfxLovyan gfx(lcd);
 *
 *   void setup() { lcd.init(); }
 *   void loop() {
 *     gfx.startWrite();
 *     gfx.clear(MG::black);
 *     gfx.fillRoundRect(20, 20, 120, 60, 8, MG::blue);
 *     gfx.drawText(30, 36, "Lovyan", MG::white, 2);
 *     gfx.endWrite();                 // LovyanGFX draws straight to the panel
 *   }
 *
 * LovyanGFX writes directly to the display, so display() is a no-op — wrap a
 * frame in startWrite()/endWrite() to batch the SPI transaction instead.
 */

#pragma once

#include "MinisGfx.h"

#if defined(MINIS_GFX_LOVYAN) || __has_include(<LovyanGFX.hpp>)
#include <LovyanGFX.hpp>
#else
#error "MinisGfxLovyan.h requires the LovyanGFX library. Install it, or define MINIS_GFX_LOVYAN for a non-standard include path."
#endif

class MinisGfxLovyan : public MinisGfx {
  lgfx::LGFXBase& _g;
  // 24-bit RGB888 — LovyanGFX interprets a uint32_t colour as RGB888.
  static uint32_t cv(const MGColor& c) { return c.rgb888(); }
public:
  explicit MinisGfxLovyan(lgfx::LGFXBase& dev) : _g(dev) {}

  int  width()  const override { return _g.width(); }
  int  height() const override { return _g.height(); }
  void startWrite() override { _g.startWrite(); }
  void endWrite()  override { _g.endWrite(); }

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
    _g.drawBitmap(x, y, bm, w, h, cv(fg));
  }
  void drawBitmap(int x, int y, const uint8_t* bm, int w, int h, const MGColor& fg, const MGColor& bg) override {
    _g.drawBitmap(x, y, bm, w, h, cv(fg), cv(bg));
  }
  void drawRGBBitmap(int x, int y, const uint16_t* px, int w, int h) override {
    _g.pushImage(x, y, w, h, px);   // uint16_t source = RGB565
  }

  void drawChar(int x, int y, char ch, const MGColor& fg, int size) override {
    _g.setTextSize(size < 1 ? 1 : size);
    if (_hasTextBg) _g.setTextColor(cv(fg), cv(_bg)); else _g.setTextColor(cv(fg));
    _g.setCursor(x, y);
    _g.print(ch);
  }
  void drawText(int x, int y, const char* s, const MGColor& fg, int size = 1) override {
    _g.setTextWrap(false);
    _g.setTextSize(size < 1 ? 1 : size);
    if (_hasTextBg) _g.setTextColor(cv(fg), cv(_bg)); else _g.setTextColor(cv(fg));
    if (s) _g.drawString(s, x, y);
  }
  void write(char ch) override {
    _g.setTextWrap(_wrap);
    _g.setTextSize(_textSize);
    if (_hasTextBg) _g.setTextColor(cv(_fg), cv(_bg)); else _g.setTextColor(cv(_fg));
    _g.setCursor(_cx, _cy);
    _g.print(ch);
    _cx = _g.getCursorX();
    _cy = _g.getCursorY();
  }
};
