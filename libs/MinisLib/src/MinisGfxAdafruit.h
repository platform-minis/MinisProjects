/**
 * MinisGfxAdafruit.h — MinisGfx backend wrapping an Adafruit_GFX device.
 *
 * Use this when the panel only exposes the Adafruit_GFX drawing API and you
 * cannot (or do not want to) own a framebuffer: any Adafruit_ILI9341 /
 * Adafruit_ST7789 / Adafruit_SSD1306 / Adafruit_SH110X / etc. Primitives and
 * text are delegated to the native, often hardware-accelerated, implementation;
 * colors are converted MGColor → RGB565.
 *
 *   #include <Adafruit_SSD1306.h>
 *   #include <MinisGfxAdafruit.h>
 *
 *   Adafruit_SSD1306 oled(128, 64, &Wire);
 *   MinisGfxAdafruit  gfx(oled);
 *
 *   void setup() {
 *     oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
 *     gfx.setFlush([]{ oled.display(); });   // monochrome OLED needs an explicit flush
 *   }
 *   void loop() {
 *     gfx.clear(MG::black);
 *     gfx.drawText(0, 0, "Hi", MG::white, 1);
 *     gfx.display();                          // -> the flush callback above
 *   }
 *
 * Adafruit_GFX has no begin()/display() of its own (those live on the concrete
 * driver), so drive init from your sketch and route the panel flush through
 * setFlush() if your display is buffered (OLED). For direct-write TFTs you can
 * skip the flush entirely.
 */

#pragma once

#include "MinisGfx.h"

#if defined(MINIS_GFX_ADAFRUIT) || __has_include(<Adafruit_GFX.h>)
#include <Adafruit_GFX.h>
#else
#error "MinisGfxAdafruit.h requires the Adafruit_GFX library. Install it, or define MINIS_GFX_ADAFRUIT if it lives under a non-standard include path."
#endif

class MinisGfxAdafruit : public MinisGfx {
  Adafruit_GFX& _g;
  void (*_flush)() = nullptr;
  static uint16_t cv(const MGColor& c) { return c.rgb565(); }
public:
  explicit MinisGfxAdafruit(Adafruit_GFX& dev) : _g(dev) {}

  // Optional flush for buffered panels (e.g. SSD1306 OLED -> oled.display()).
  void setFlush(void (*fn)()) { _flush = fn; }

  int  width()  const override { return _g.width(); }
  int  height() const override { return _g.height(); }
  void display() override { if (_flush) _flush(); }
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
    _g.drawBitmap(x, y, (uint8_t*)bm, w, h, cv(fg));
  }
  void drawBitmap(int x, int y, const uint8_t* bm, int w, int h, const MGColor& fg, const MGColor& bg) override {
    _g.drawBitmap(x, y, (uint8_t*)bm, w, h, cv(fg), cv(bg));
  }
  void drawRGBBitmap(int x, int y, const uint16_t* px, int w, int h) override {
    _g.drawRGBBitmap(x, y, (uint16_t*)px, w, h);
  }

  // Text delegated to the native font engine (GFX fonts, scaling, wrap).
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
