/**
 * MinisGfx.h — universal graphics abstraction for MyCastle / Minis devices.
 *
 * One drawing interface, many backends. The idea: program your UI once against
 * MinisGfx, then bind it to whatever the target hardware can actually do.
 *
 *   • Where a linear RGBA framebuffer is available (the in-browser WASM simulator,
 *     or any TFT/OLED you can blit a full buffer to) use MinisGfxFramebuffer.
 *     All primitives are rendered in software on top of a single drawPixel().
 *
 *   • Where it is NOT possible to own a framebuffer — e-paper with its own paged
 *     refresh, or a panel that only exposes a vendor drawing API — wrap the
 *     vendor library instead. Header-only adapters (include only what you need):
 *
 *         MinisGfxAdafruit.h   →  Adafruit_GFX*          (the de-facto standard)
 *         MinisGfxGxEPD2.h     →  GxEPD2_GFX&            (e-paper, ZinggJM/GxEPD2)
 *         MinisGfxLovyan.h     →  lgfx::LovyanGFX*       (LovyanGFX, fast TFT)
 *         MinisGfxQt.h         →  QPainter / QApplication (libs/Qt MinisQt toolkit)
 *
 * Every backend speaks the same MinisGfx surface, so the same scene code runs on
 * a browser canvas, a 0.96" OLED, a 4.2" e-paper, or a parallel-bus TFT unchanged.
 *
 * Colors are carried as MGColor (RGBA8888) and converted lazily per backend
 * (RGB565 for Adafruit/Lovyan, black/white/accent for e-paper, RGBA for the
 * framebuffer). Coordinates are pixels; the origin is top-left.
 *
 * Minimal example (framebuffer / WASM):
 *   #include <MinisGfx.h>
 *   MinisGfxFramebuffer gfx(320, 240);
 *   void setup() { gfx.begin(); }
 *   void loop() {
 *     gfx.clear(MG::black);
 *     gfx.fillRoundRect(20, 20, 120, 60, 8, MG::blue);
 *     gfx.setTextColor(MG::white);
 *     gfx.drawText(30, 40, "Hello", MG::white, 2);
 *     gfx.display();           // blit to the panel / canvas
 *     delay(33);
 *   }
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ── MGColor ─────────────────────────────────────────────────────────────────
// RGBA8888 in struct form. Backends convert to whatever the panel wants. The
// rgba() packing is 0xAABBGGRR so the in-memory byte order is R,G,B,A — what the
// browser ImageData and most RGBA displays expect (matches MinisQt's QColor).
struct MGColor {
  uint8_t r = 0, g = 0, b = 0, a = 255;

  MGColor() {}
  MGColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
    : r(red), g(green), b(blue), a(alpha) {}

  static MGColor rgb(uint8_t r, uint8_t g, uint8_t b) { return MGColor(r, g, b); }
  static MGColor fromRgb888(uint32_t c) { return MGColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }
  static MGColor fromRgb565(uint16_t c) {
    uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    return MGColor((r5 * 255 + 15) / 31, (g6 * 255 + 31) / 63, (b5 * 255 + 15) / 31);
  }
  // Parse "#rgb" / "#rrggbb".
  static MGColor fromString(const char* s) {
    if (!s || s[0] != '#') return MGColor();
    auto hx = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
    };
    int len = 0; while (s[len]) len++;
    if (len == 4) return MGColor(hx(s[1]) * 17, hx(s[2]) * 17, hx(s[3]) * 17);
    if (len >= 7) return MGColor(hx(s[1]) * 16 + hx(s[2]), hx(s[3]) * 16 + hx(s[4]), hx(s[5]) * 16 + hx(s[6]));
    return MGColor();
  }

  uint16_t rgb565() const { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
  uint32_t rgb888() const { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
  uint32_t rgba()   const { return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24); }
  // Rec.601 luminance 0..255 — used by 1-bit / e-paper backends to threshold.
  uint8_t luma() const { return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8); }

  MGColor lighter(int pct = 130) const {
    auto f = [pct](uint8_t c) { int v = c * pct / 100; return (uint8_t)(v > 255 ? 255 : v); };
    return MGColor(f(r), f(g), f(b), a);
  }
  MGColor darker(int pct = 130) const {
    auto f = [pct](uint8_t c) { return (uint8_t)(c * 100 / pct); };
    return MGColor(f(r), f(g), f(b), a);
  }
  bool operator==(const MGColor& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
  bool operator!=(const MGColor& o) const { return !(*this == o); }
};

// Common named colors (namespaced to avoid clashing with Qt::/Adafruit macros).
namespace MG {
  inline const MGColor black(0, 0, 0);
  inline const MGColor white(255, 255, 255);
  inline const MGColor red(220, 50, 50);
  inline const MGColor green(60, 180, 90);
  inline const MGColor blue(60, 120, 200);
  inline const MGColor yellow(235, 200, 40);
  inline const MGColor orange(240, 140, 40);
  inline const MGColor cyan(40, 200, 220);
  inline const MGColor magenta(210, 60, 180);
  inline const MGColor gray(128, 128, 128);
  inline const MGColor darkGray(64, 64, 64);
  inline const MGColor lightGray(200, 200, 200);
  inline const MGColor transparent(0, 0, 0, 0);
}

// 8x8 bitmap font for the built-in software text path (printable ASCII
// 0x20..0x7F). bit 0 = leftmost column of each row. Defined in MinisGfx.cpp.
extern const uint8_t MINIS_FONT8X8[96][8];

// ── MinisGfx — the universal drawing surface ────────────────────────────────
// drawPixel(), width(), height() are the only required overrides. Every other
// primitive has a software default built on drawPixel(); accelerated backends
// override the ones their vendor library implements natively.
class MinisGfx {
public:
  virtual ~MinisGfx() {}

  // Lifecycle. begin() inits the panel; display() flushes a buffered backend
  // (e-paper refresh, framebuffer blit). No-ops where not applicable.
  virtual void begin() {}
  virtual void display() {}

  // Batched writes — backends that benefit from a transaction (SPI CS hold)
  // override these; the default pair is a no-op.
  virtual void startWrite() {}
  virtual void endWrite() {}

  virtual int width() const = 0;
  virtual int height() const = 0;

  // The single required primitive.
  virtual void drawPixel(int x, int y, const MGColor& c) = 0;

  // ── Filled / outlined primitives (software defaults) ───────────────────────
  virtual void fillScreen(const MGColor& c);
  void clear(const MGColor& c = MG::black) { fillScreen(c); }

  virtual void drawFastHLine(int x, int y, int w, const MGColor& c);
  virtual void drawFastVLine(int x, int y, int h, const MGColor& c);
  virtual void drawLine(int x0, int y0, int x1, int y1, const MGColor& c);

  virtual void drawRect(int x, int y, int w, int h, const MGColor& c);
  virtual void fillRect(int x, int y, int w, int h, const MGColor& c);
  virtual void drawRoundRect(int x, int y, int w, int h, int r, const MGColor& c);
  virtual void fillRoundRect(int x, int y, int w, int h, int r, const MGColor& c);

  virtual void drawCircle(int cx, int cy, int r, const MGColor& c);
  virtual void fillCircle(int cx, int cy, int r, const MGColor& c);

  virtual void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const MGColor& c);
  virtual void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const MGColor& c);

  // 1-bit MSB-first mono bitmap (Adafruit/GxEPD layout). bg omitted = transparent.
  virtual void drawBitmap(int x, int y, const uint8_t* bitmap, int w, int h, const MGColor& fg);
  virtual void drawBitmap(int x, int y, const uint8_t* bitmap, int w, int h, const MGColor& fg, const MGColor& bg);
  // Packed RGB565 image, row-major.
  virtual void drawRGBBitmap(int x, int y, const uint16_t* pixels, int w, int h);

  // ── Text (software 8x8 font; vendor backends may override for native fonts) ─
  void setTextColor(const MGColor& c) { _fg = c; _hasTextBg = false; }
  void setTextColor(const MGColor& c, const MGColor& bg) { _fg = c; _bg = bg; _hasTextBg = true; }
  void setTextSize(int s) { _textSize = s < 1 ? 1 : s; }
  void setTextWrap(bool w) { _wrap = w; }
  void setCursor(int x, int y) { _cx = x; _cy = y; }
  int cursorX() const { return _cx; }
  int cursorY() const { return _cy; }

  virtual void drawChar(int x, int y, char ch, const MGColor& fg, int size);
  // Convenience: draw a whole string at (x,y) without touching the cursor.
  virtual void drawText(int x, int y, const char* s, const MGColor& fg, int size = 1);
  // Cursor-based stream API (mirrors Arduino Print).
  virtual void write(char ch);
  void print(const char* s) { if (s) while (*s) write(*s++); }
  void println(const char* s = "") { print(s); write('\n'); }

  int textWidth(const char* s, int size = -1) const {
    int n = 0; for (const char* p = s; p && *p; p++) n++;
    return n * 8 * (size < 0 ? _textSize : size);   // 8x8 glyph cell (advance 8)
  }
  int textHeight(int size = -1) const { return 8 * (size < 0 ? _textSize : size); }

protected:
  // Cursor / text state shared by the software text path.
  MGColor _fg = MG::white, _bg = MG::black;
  bool _hasTextBg = false, _wrap = true;
  int _cx = 0, _cy = 0, _textSize = 1;
};

// ── Framebuffer backend (the "where possible" path) ─────────────────────────
// Owns (or borrows) a linear RGBA8888 buffer and renders everything in software.
// display() pushes the buffer to the panel:
//   • In the MyCastle WASM simulator (__EMSCRIPTEN__), minis_canvas_present() is
//     provided by the Arduino WASM mock and blits to the browser <canvas>.
//   • On real hardware, either set a present callback (setPresentCallback) or
//     provide a strong definition of minis_canvas_present() to push the buffer
//     to your TFT/OLED. The default off-target definition (MinisGfx.cpp) is a
//     weak no-op, so a sketch still links and runs.
extern "C" {
  void minis_canvas_present(const uint32_t* pixels, int width, int height);
}

class MinisGfxFramebuffer : public MinisGfx {
  uint32_t* _buf; int _w, _h; bool _owns;
  void (*_present)(const uint32_t*, int, int) = nullptr;
public:
  MinisGfxFramebuffer(int w, int h)
    : _buf((uint32_t*)malloc(sizeof(uint32_t) * (size_t)w * h)), _w(w), _h(h), _owns(true) {}
  // Borrow an externally-owned buffer (e.g. a vendor library's frame buffer).
  MinisGfxFramebuffer(uint32_t* buf, int w, int h)
    : _buf(buf), _w(w), _h(h), _owns(false) {}
  ~MinisGfxFramebuffer() override { if (_owns) free(_buf); }

  uint32_t* pixels() { return _buf; }
  const uint32_t* pixels() const { return _buf; }
  // Hardware flush hook — when set, display() calls this instead of the C ABI.
  void setPresentCallback(void (*cb)(const uint32_t*, int, int)) { _present = cb; }

  int width() const override { return _w; }
  int height() const override { return _h; }

  void drawPixel(int x, int y, const MGColor& c) override {
    if (x < 0 || y < 0 || x >= _w || y >= _h || c.a == 0) return;
    uint32_t* dst = &_buf[y * _w + x];
    if (c.a == 255) { *dst = c.rgba(); return; }
    uint32_t d = *dst;
    uint8_t dr = d & 0xFF, dg = (d >> 8) & 0xFF, db = (d >> 16) & 0xFF;
    int a = c.a;
    uint8_t nr = (uint8_t)((c.r * a + dr * (255 - a)) / 255);
    uint8_t ng = (uint8_t)((c.g * a + dg * (255 - a)) / 255);
    uint8_t nb = (uint8_t)((c.b * a + db * (255 - a)) / 255);
    *dst = (uint32_t)nr | ((uint32_t)ng << 8) | ((uint32_t)nb << 16) | (0xFFu << 24);
  }

  // Opaque clears are a straight memset-style fill.
  void fillScreen(const MGColor& c) override {
    if (c.a == 255) { uint32_t v = c.rgba(); for (int i = 0, n = _w * _h; i < n; i++) _buf[i] = v; }
    else MinisGfx::fillScreen(c);
  }

  void display() override {
    if (_present) _present(_buf, _w, _h);
    else minis_canvas_present(_buf, _w, _h);
  }
};

// ── RGB565 buffer backend ───────────────────────────────────────────────────
// Renders into a packed RGB565 (uint16_t) buffer — the native pixel format of
// most SPI/parallel TFTs and of LovyanGFX sprites / Adafruit GFXcanvas16. Draw
// off-screen, then push the buffer to the panel in one transfer:
//
//   MinisGfxBuffer565 gfx(240, 320);
//   gfx.clear(MG::black);
//   gfx.fillCircle(120, 160, 40, MG::red);
//   tft.pushImage(0, 0, 240, 320, gfx.pixels());   // or pushColors / writePixels
//
// Or wrap a buffer you already own (e.g. a vendor canvas) with the borrow ctor.
class MinisGfxBuffer565 : public MinisGfx {
  uint16_t* _buf; int _w, _h; bool _owns;
  void (*_present)(const uint16_t*, int, int) = nullptr;
public:
  MinisGfxBuffer565(int w, int h)
    : _buf((uint16_t*)malloc(sizeof(uint16_t) * (size_t)w * h)), _w(w), _h(h), _owns(true) {}
  MinisGfxBuffer565(uint16_t* buf, int w, int h)
    : _buf(buf), _w(w), _h(h), _owns(false) {}
  ~MinisGfxBuffer565() override { if (_owns) free(_buf); }

  uint16_t* pixels() { return _buf; }
  const uint16_t* pixels() const { return _buf; }
  size_t byteCount() const { return sizeof(uint16_t) * (size_t)_w * _h; }
  void setPresentCallback(void (*cb)(const uint16_t*, int, int)) { _present = cb; }

  int width() const override { return _w; }
  int height() const override { return _h; }

  void drawPixel(int x, int y, const MGColor& c) override {
    if (x < 0 || y < 0 || x >= _w || y >= _h || c.a == 0) return;
    uint16_t* dst = &_buf[y * _w + x];
    if (c.a == 255) { *dst = c.rgb565(); return; }
    MGColor d = MGColor::fromRgb565(*dst);
    int a = c.a;
    uint8_t nr = (uint8_t)((c.r * a + d.r * (255 - a)) / 255);
    uint8_t ng = (uint8_t)((c.g * a + d.g * (255 - a)) / 255);
    uint8_t nb = (uint8_t)((c.b * a + d.b * (255 - a)) / 255);
    *dst = MGColor(nr, ng, nb).rgb565();
  }

  void fillScreen(const MGColor& c) override {
    if (c.a == 255) { uint16_t v = c.rgb565(); for (int i = 0, n = _w * _h; i < n; i++) _buf[i] = v; }
    else MinisGfx::fillScreen(c);
  }

  void display() override { if (_present) _present(_buf, _w, _h); }
};

// ── 1-bit monochrome buffer backend ─────────────────────────────────────────
// Renders into a packed 1-bit buffer — the native format of SSD1306/SH1106 OLEDs
// and B/W e-paper. Two byte layouts:
//   • MG_MONO_HMSB — horizontal, MSB-first, row-major. Matches Adafruit
//     GFXcanvas1, GxEPD2 bitmaps and drawBitmap(). Buffer = ((w+7)/8) * h bytes.
//   • MG_MONO_VLSB — vertical pages, LSB-first. Matches the native SSD1306
//     framebuffer (8 vertical pixels per byte). Buffer = w * ((h+7)/8) bytes.
// A pixel is "on" (bit set) when its colour luma >= 128; flip with setInverted().
//
//   MinisGfxBufferMono gfx(128, 64, MG_MONO_VLSB);
//   gfx.clear(MG::black);
//   gfx.drawText(0, 0, "OLED", MG::white, 1);
//   ssd1306_pushBuffer(gfx.pixels());   // your panel's blit
enum MGMonoLayout { MG_MONO_HMSB = 0, MG_MONO_VLSB = 1 };

class MinisGfxBufferMono : public MinisGfx {
  uint8_t* _buf; int _w, _h; bool _owns; MGMonoLayout _layout;
  bool _inverted = false;
  void (*_present)(const uint8_t*, int, int) = nullptr;

  size_t bytes() const {
    return _layout == MG_MONO_HMSB ? (size_t)((_w + 7) / 8) * _h : (size_t)_w * ((_h + 7) / 8);
  }
  void setBit(int x, int y, bool on) {
    if (_layout == MG_MONO_HMSB) {
      uint8_t* p = &_buf[(size_t)y * ((_w + 7) / 8) + (x >> 3)];
      uint8_t m = 0x80 >> (x & 7);
      if (on) *p |= m; else *p &= ~m;
    } else {
      uint8_t* p = &_buf[(size_t)x + ((size_t)(y >> 3)) * _w];
      uint8_t m = 1 << (y & 7);
      if (on) *p |= m; else *p &= ~m;
    }
  }
public:
  MinisGfxBufferMono(int w, int h, MGMonoLayout layout = MG_MONO_HMSB)
    : _buf(nullptr), _w(w), _h(h), _owns(true), _layout(layout) {
    _buf = (uint8_t*)malloc(bytes());
  }
  MinisGfxBufferMono(uint8_t* buf, int w, int h, MGMonoLayout layout = MG_MONO_HMSB)
    : _buf(buf), _w(w), _h(h), _owns(false), _layout(layout) {}
  ~MinisGfxBufferMono() override { if (_owns) free(_buf); }

  uint8_t* pixels() { return _buf; }
  const uint8_t* pixels() const { return _buf; }
  size_t byteCount() const { return bytes(); }
  MGMonoLayout layout() const { return _layout; }
  // When true, a high-luma colour clears the bit instead of setting it (handy
  // for panels whose "1" means dark).
  void setInverted(bool inv) { _inverted = inv; }
  void setPresentCallback(void (*cb)(const uint8_t*, int, int)) { _present = cb; }

  int width() const override { return _w; }
  int height() const override { return _h; }

  void drawPixel(int x, int y, const MGColor& c) override {
    if (x < 0 || y < 0 || x >= _w || y >= _h || c.a == 0) return;
    bool on = c.luma() >= 128;
    if (_inverted) on = !on;
    setBit(x, y, on);
  }

  void fillScreen(const MGColor& c) override {
    bool on = c.luma() >= 128;
    if (_inverted) on = !on;
    memset(_buf, on ? 0xFF : 0x00, bytes());
  }

  void display() override { if (_present) _present(_buf, _w, _h); }
};
