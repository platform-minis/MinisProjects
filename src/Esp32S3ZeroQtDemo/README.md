# ESP32-S3 Zero — MinisQt Demo

A small Qt-style touch UI for the Waveshare ESP32-S3 Zero, built with the
header-only **MinisQt** library (`libs/Qt`). The UI is drawn into an RGBA
framebuffer, so the same sketch runs unchanged in the **in-browser WASM
simulator** and on real hardware with a touch display.

## What it shows

- A title label (large font)
- A `QSlider` that drives a `QProgressBar` and a live value label
- A `QCheckBox`
- A `QPushButton` with a click counter

All wired with Qt-style `Signal`s (`valueChanged`, `toggled`, `clicked`).

## Run it in the browser

1. Open this project in **Electronics → C++**.
2. Click **Run in browser (WASM simulator)** on the card.
3. Press **Build WASM**, then **Run**.
4. The UI appears in the **DISPLAY** pane — click the slider, button and
   checkbox with the mouse; serial output mirrors the events.

## Run it on hardware

`MinisQt.h` only needs two C functions to present pixels and read touch input:

```cpp
extern "C" void minis_canvas_present(const uint32_t* pixels, int w, int h);
extern "C" int  minis_canvas_poll(int* type, int* x, int* y);  // type: 1=down 2=move 3=up
```

In the WASM simulator these are supplied by the Arduino mock. On hardware,
provide your own definitions (e.g. push the framebuffer to a TFT and feed
touch coordinates) — the library ships weak no-op defaults so it always links.

## Files

- `src/QtDemo/QtDemo.ino` — the sketch
- `src/QtDemo/MinisQt.h` — vendored copy of the library (canonical source: `libs/Qt`)
