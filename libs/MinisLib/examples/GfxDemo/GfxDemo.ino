/**
 * GfxDemo — one scene, any backend, via MinisGfx.
 *
 * The scene code (drawScene) never names a concrete display. Pick a backend by
 * defining ONE of the macros below (or just leave the default framebuffer, which
 * runs in the MyCastle WASM simulator out of the box):
 *
 *   default            MinisGfxFramebuffer  — RGBA buffer, WASM canvas / any blit-able panel
 *   USE_ADAFRUIT       MinisGfxAdafruit     — Adafruit_GFX device (here: SSD1306 OLED)
 *   USE_GXEPD2         MinisGfxGxEPD2        — GxEPD2 e-paper
 *   USE_LOVYAN         MinisGfxLovyan       — LovyanGFX TFT
 *   USE_QT             MinisGfxQt           — MinisQt QPainter pipeline
 *
 * Build the chosen variant in the Arduino/PlatformIO env that has that library.
 */

// ── Backend selection ────────────────────────────────────────────────────────
#if defined(USE_ADAFRUIT)
  #include <Adafruit_SSD1306.h>
  #include <MinisGfxAdafruit.h>
  Adafruit_SSD1306 oled(128, 64, &Wire);
  MinisGfxAdafruit gfx(oled);
#elif defined(USE_GXEPD2)
  #include <GxEPD2_BW.h>
  #include <MinisGfxGxEPD2.h>
  GxEPD2_BW<GxEPD2_290_T94, GxEPD2_290_T94::HEIGHT> epd(GxEPD2_290_T94(/*CS*/5,/*DC*/17,/*RST*/16,/*BUSY*/4));
  MinisGfxGxEPD2 gfx(epd);
#elif defined(USE_LOVYAN)
  #include <LovyanGFX.hpp>
  LGFX lcd;                         // your lgfx::LGFX_Device subclass
  #include <MinisGfxLovyan.h>
  MinisGfxLovyan gfx(lcd);
#elif defined(USE_QT)
  #include <MinisQt.h>
  #include <MinisGfxQt.h>
  MinisGfxQt gfx(320, 240);
#else
  #include <MinisGfx.h>
  MinisGfxFramebuffer gfx(320, 240);
#endif

// ── Backend-agnostic scene ───────────────────────────────────────────────────
static void drawScene(MinisGfx& g, int t) {
  int W = g.width(), H = g.height();
  g.clear(MG::black);
  g.drawRect(0, 0, W, H, MG::darkGray);

  g.fillRoundRect(8, 8, W - 16, 28, 6, MG::blue);
  g.setTextColor(MG::white);
  g.drawText(16, 14, "MinisGfx", MG::white, 2);

  // A little animation so you can tell frames are flowing.
  int x = 8 + (t % (W - 40));
  g.fillCircle(x + 16, H / 2 + 10, 12, MG::green);
  g.drawCircle(x + 16, H / 2 + 10, 16, MG::lightGray);

  g.fillTriangle(W - 60, H - 12, W - 30, H - 12, W - 45, H - 44, MG::yellow);
  g.drawText(8, H - 16, "frame", MG::lightGray, 1);
}

int frame = 0;

void setup() {
#if defined(USE_ADAFRUIT)
  Wire.begin();
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  gfx.setFlush([] { oled.display(); });   // OLED needs an explicit buffer flush
#elif defined(USE_GXEPD2)
  epd.init(115200);
#elif defined(USE_LOVYAN)
  lcd.init();
#endif
  gfx.begin();
}

void loop() {
#if defined(USE_GXEPD2)
  // E-paper: redraw occasionally (refresh is slow), full-buffer path.
  drawScene(gfx, frame);
  gfx.display();
  gfx.hibernate();
  delay(15000);
#else
  gfx.startWrite();
  drawScene(gfx, frame);
  gfx.endWrite();
  gfx.display();
  delay(33);
#endif
  frame += 4;
}
