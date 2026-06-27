# MinisGfx Demo

A showcase for **MinisGfx**, the universal graphics abstraction in
`libs/MinisLib`. One backend-agnostic scene is drawn into an RGBA framebuffer and
shown in the DISPLAY pane — build to WebAssembly and **Run in browser**, then
click/drag with the mouse to move the live marker.

The same `drawScene(MinisGfx&)` code re-targets an RGB565 TFT, a monochrome OLED,
or a vendor library (Adafruit GFX / GxEPD2 e-paper / LovyanGFX / MinisQt) by
swapping a single backend line — see `README.md` for the hardware snippets.
