# ESP32-S3 Zero — MinisGfx Demo

A showcase for **MinisGfx**, the universal graphics abstraction in
`libs/MinisLib`. A single backend-agnostic scene — `drawScene(MinisGfx&)` — is
rendered into an RGBA framebuffer with `MinisGfxFramebuffer`, so the same sketch
runs unchanged in the **in-browser WASM simulator** and on real hardware.

The whole point of MinisGfx: `drawScene()` only ever talks to a `MinisGfx&`. To
move to a different display you swap **one line** (the backend) and the scene
code stays identical.

## What it shows

- Outlined / filled rectangles and rounded rectangles
- Circles (outline + filled) and a filled triangle
- A fan of `drawLine()` strokes
- Text at sizes 1 and 2, the cursor stream API (`setCursor`/`print`) with a text
  background, and a colour parsed from `"#3498db"`
- A 1-bit `drawBitmap()` icon
- A value bar animated by a sine each frame
- A **live touch/click marker** (drained from `minis_canvas_poll`)

## Run it in the browser

1. Open this project in **Electronics → C++**.
2. Click **Run in browser (WASM simulator)** on the card.
3. Press **Build WASM**, then **Run**.
4. The scene appears in the **DISPLAY** pane — click/drag with the mouse to move
   the marker; serial output mirrors startup.

## Run it on hardware

`drawScene()` never changes — only the backend does. MinisGfx renders every
primitive through a single `drawPixel()`, so each backend just needs a buffer
(or a vendor device) and a way to push it out.

```cpp
// RGB565 TFT (ST7789 / ILI9341 / ...)
MinisGfxBuffer565 gfx(240, 320);
drawScene(gfx);
tft.pushImage(0, 0, 240, 320, gfx.pixels());

// Monochrome OLED (SSD1306 128x64, native vertical-page layout)
MinisGfxBufferMono gfx(128, 64, MG_MONO_VLSB);
drawScene(gfx);
ssd1306_blit(gfx.pixels());

// Keep RGBA but push it yourself instead of the canvas ABI
gfx.setPresentCallback([](const uint32_t* px, int w, int h) {
  myPanel.pushRGBA(px, w, h);
});
```

Prefer to drive a display library directly (no buffer)? Use the header-only
adapters next to the sketch's source library:
`MinisGfxAdafruit.h`, `MinisGfxGxEPD2.h` (e-paper), `MinisGfxLovyan.h`,
`MinisGfxQt.h`. See `libs/MinisLib/examples/GfxDemo` for a one-file example that
switches between all five backends with a macro.

## Files

- `src/GfxDemo/GfxDemo.ino` — the sketch (scene + pointer handling)
- `src/GfxDemo/MinisGfx.h`, `src/GfxDemo/MinisGfx.cpp` — vendored copies of the
  library (canonical source: `libs/MinisLib`)
