// ─────────────────────────────────────────────────────────────────────────────
// GfxDemo — a showcase for the MinisGfx universal graphics abstraction.
//
// One backend-agnostic scene (drawScene) rendered through MinisGfxFramebuffer,
// an RGBA8888 software framebuffer. It runs in the MyCastle in-browser WASM
// simulator out of the box (electronics → C++ → Run in browser): the framebuffer
// shows up in the DISPLAY pane and the last touch/click is tracked live.
//
// The point of MinisGfx: the SAME drawScene(MinisGfx&) code renders to any
// backend. To run on real hardware, swap the backend (see "HARDWARE" below) and
// drawScene stays untouched.
//
// MinisGfx.h / MinisGfx.cpp are vendored next to this sketch so the simulator
// compiles them with no extra library install; the canonical copy lives in
// libs/MinisLib of this repo.
// ─────────────────────────────────────────────────────────────────────────────
#include "MinisGfx.h"

// The WASM mock (and a real-hardware sketch) provides this to drain pointer
// events from the canvas. type: 1=down 2=move 3=up.
extern "C" int minis_canvas_poll(int* type, int* x, int* y);

static const int W = 320, H = 240;

// The active backend. On hardware, replace this one line — see HARDWARE below.
MinisGfxFramebuffer gfx(W, H);

// Live UI state, mutated by pointer events, read by the scene.
static int  touchX = -1, touchY = -1;
static bool touching = false;
static int  frame = 0;

// ── The backend-agnostic scene ───────────────────────────────────────────────
// Note it only ever talks to `MinisGfx&` — never to the concrete backend.
static void drawScene(MinisGfx& g) {
  const int w = g.width();
  g.clear(MGColor(18, 20, 24));

  // Title bar.
  g.fillRect(0, 0, w, 28, MGColor(40, 44, 52));
  g.drawText(8, 6, "MinisGfx showcase", MG::white, 2);

  // Primitive gallery — outlined + filled rects, rounded rects.
  g.drawRect(8, 38, 60, 40, MG::lightGray);
  g.fillRect(76, 38, 60, 40, MG::blue);
  g.drawRoundRect(144, 38, 60, 40, 10, MG::cyan);
  g.fillRoundRect(212, 38, 60, 40, 10, MG::green);

  // Circles and a triangle.
  g.drawCircle(38, 110, 18, MG::yellow);
  g.fillCircle(106, 110, 18, MG::orange);
  g.fillTriangle(150, 128, 200, 128, 175, 92, MG::magenta);

  // Lines fanning out from a point — shows arbitrary drawLine().
  for (int i = 0; i <= 8; i++)
    g.drawLine(240, 128, 240 + (i - 4) * 14, 92, MG::red.lighter(100 + i * 8));

  // Text at three sizes + a colour swatch parsed from a hex string.
  g.drawText(8, 150, "size 1 ABCabc 123", MG::lightGray, 1);
  g.drawText(8, 166, "size 2", MG::white, 2);
  g.setTextColor(MGColor::fromString("#3498db"), MGColor(18, 20, 24));
  g.setTextSize(2);
  g.setCursor(110, 166);
  g.print("cursor");                 // stream API with a background fill

  // A tiny 1-bit bitmap drawn with drawBitmap() (a 8x8 checker + frame).
  static const uint8_t icon[8] = { 0xFF, 0x81, 0xA5, 0x81, 0xBD, 0x81, 0x81, 0xFF };
  g.drawText(8, 192, "bitmap:", MG::lightGray, 1);
  g.drawBitmap(72, 190, icon, 8, 8, MG::white);

  // A value bar driven by a slow sine so something animates every frame.
  float s = (sinf(frame * 0.06f) + 1.0f) * 0.5f;     // 0..1
  int barW = (int)(s * (w - 16));
  g.drawRect(8, 210, w - 16, 16, MG::darkGray);
  g.fillRect(8, 210, barW, 16, MG::green.darker(110));

  // Live touch marker.
  if (touchX >= 0) {
    MGColor ring = touching ? MG::white : MG::gray;
    g.drawCircle(touchX, touchY, 14, ring);
    g.fillCircle(touchX, touchY, 5, MG::red);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d,%d", touchX, touchY);
    g.drawText(touchX + 18, touchY - 4, buf, ring, 1);
  } else {
    g.drawText(90, 110, "tap / click", MG::gray, 1);
  }
}

// ── Pointer handling (drives the live marker) ────────────────────────────────
static void pumpInput() {
  int t, x, y;
  while (minis_canvas_poll(&t, &x, &y)) {
    touchX = x; touchY = y;
    if (t == 1) touching = true;
    else if (t == 3) touching = false;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("MinisGfx demo starting");
  gfx.begin();
}

void loop() {
  pumpInput();
  drawScene(gfx);
  gfx.display();          // blit the RGBA framebuffer to the canvas / panel
  frame++;
  delay(33);              // ~30 fps
}

// ─────────────────────────────────────────────────────────────────────────────
// HARDWARE — drawScene() above never changes; only the backend does.
//
//   • RGBA / generic blit-able panel: keep MinisGfxFramebuffer and push the
//     buffer yourself instead of the canvas ABI:
//
//       gfx.setPresentCallback([](const uint32_t* px, int w, int h) {
//         myPanel.pushRGBA(px, w, h);          // your driver
//       });
//
//   • RGB565 TFT (e.g. ST7789/ILI9341) — render to a native 565 buffer:
//
//       #include "MinisGfx.h"
//       MinisGfxBuffer565 gfx(240, 320);
//       // ... drawScene(gfx); ...
//       tft.pushImage(0, 0, 240, 320, gfx.pixels());
//
//   • Monochrome OLED (SSD1306, 128x64) — render to a native 1-bit buffer:
//
//       MinisGfxBufferMono gfx(128, 64, MG_MONO_VLSB);
//       // ... drawScene(gfx); ...
//       ssd1306_blit(gfx.pixels());            // your driver
//
//   • Or wrap a vendor library directly (no buffer) with the header-only
//     adapters: MinisGfxAdafruit.h / MinisGfxGxEPD2.h / MinisGfxLovyan.h /
//     MinisGfxQt.h — see libs/MinisLib/examples/GfxDemo.
// ─────────────────────────────────────────────────────────────────────────────
