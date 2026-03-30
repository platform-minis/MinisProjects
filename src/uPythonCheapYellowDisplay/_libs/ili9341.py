"""
ili9341.py — MicroPython ILI9341 2.8" TFT driver
Designed for CheapYellowDisplay (ESP32-2432S028) from AliExpress.

Typical wiring (CYD):
    spi = machine.SPI(1, baudrate=40_000_000,
                      sck=machine.Pin(14), mosi=machine.Pin(13), miso=machine.Pin(12))
    display = ILI9341(spi,
                      cs=machine.Pin(15, machine.Pin.OUT),
                      dc=machine.Pin(2,  machine.Pin.OUT),
                      bl=machine.Pin(21, machine.Pin.OUT),
                      rotation=1)   # 1 = landscape

Drawing API
-----------
  fill(color)                   — fill entire screen
  fill_rect(x, y, w, h, color) — filled rectangle
  rect(x, y, w, h, color)      — rectangle outline
  pixel(x, y, color)           — single pixel
  hline(x, y, w, color)        — horizontal line
  vline(x, y, h, color)        — vertical line
  text(s, x, y, fg, bg, scale) — text using built-in 8x8 framebuf font

Colour helpers
--------------
  color565(r, g, b)            — RGB888 to RGB565
  BLACK, WHITE, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, ORANGE, GRAY
"""

import struct
import framebuf
from micropython import const

_SWRESET = const(0x01)
_SLPOUT  = const(0x11)
_COLMOD  = const(0x3A)
_MADCTL  = const(0x36)
_CASET   = const(0x2A)
_PASET   = const(0x2B)
_RAMWR   = const(0x2C)
_DISPON  = const(0x29)

BLACK   = const(0x0000)
WHITE   = const(0xFFFF)
RED     = const(0xF800)
GREEN   = const(0x07E0)
BLUE    = const(0x001F)
YELLOW  = const(0xFFE0)
CYAN    = const(0x07FF)
MAGENTA = const(0xF81F)
ORANGE  = const(0xFD20)
GRAY    = const(0x8410)


def color565(r, g, b):
    """Convert 8-bit RGB to 16-bit RGB565."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


class ILI9341:
    """
    Minimal ILI9341 driver.

    :param spi:      machine.SPI or machine.SoftSPI instance
    :param cs:       Chip-Select pin (machine.Pin, OUT)
    :param dc:       Data/Command pin (machine.Pin, OUT)
    :param rst:      Reset pin (machine.Pin, OUT) — optional
    :param bl:       Backlight pin (machine.Pin, OUT) — optional; HIGH = on
    :param width:    Display width  (default 320)
    :param height:   Display height (default 240)
    :param rotation: 0 portrait / 1 landscape / 2 portrait-flipped / 3 landscape-flipped
    """

    def __init__(self, spi, cs, dc, rst=None, bl=None,
                 width=320, height=240, rotation=1):
        self.spi    = spi
        self.cs     = cs
        self.dc     = dc
        self.width  = width
        self.height = height
        self._buf4  = bytearray(4)

        if rst:
            import time
            rst.value(0); time.sleep_ms(50)
            rst.value(1); time.sleep_ms(150)

        self._init(rotation)

        if bl:
            bl.value(1)

    # ── Low-level I/O ─────────────────────────────────────────────────────────

    def _cmd(self, c):
        self.dc.value(0); self.cs.value(0)
        self.spi.write(bytes([c]))
        self.cs.value(1)

    def _data(self, d):
        self.dc.value(1); self.cs.value(0)
        self.spi.write(d if isinstance(d, (bytes, bytearray)) else bytes([d]))
        self.cs.value(1)

    def _init(self, rotation):
        import time
        self._cmd(_SWRESET); time.sleep_ms(150)
        self._cmd(_SLPOUT);  time.sleep_ms(500)
        self._cmd(_COLMOD);  self._data(b'\x55')   # 16-bit RGB565
        # MADCTL: MY MX MV ML BGR MH 0 0
        # Tuned for CYD (ESP32-2432S028) ILI9341 panel orientation
        madctl = (0x08, 0xE8, 0x48, 0x28)[rotation & 3]
        self._cmd(_MADCTL);  self._data(bytes([madctl]))
        self._cmd(_DISPON)

    def _window(self, x0, y0, x1, y1):
        struct.pack_into('>HH', self._buf4, 0, x0, x1)
        self._cmd(_CASET); self._data(self._buf4)
        struct.pack_into('>HH', self._buf4, 0, y0, y1)
        self._cmd(_PASET); self._data(self._buf4)
        self._cmd(_RAMWR)

    # ── Drawing API ───────────────────────────────────────────────────────────

    def fill(self, color):
        """Fill the entire display with *color*."""
        self.fill_rect(0, 0, self.width, self.height, color)

    def fill_rect(self, x, y, w, h, color):
        """Draw a filled rectangle."""
        x1 = min(x + w - 1, self.width  - 1)
        y1 = min(y + h - 1, self.height - 1)
        n  = (x1 - x + 1) * (y1 - y + 1)
        if n <= 0:
            return
        hi, lo = (color >> 8) & 0xFF, color & 0xFF
        chunk  = bytes([hi, lo]) * 64
        self._window(x, y, x1, y1)
        self.dc.value(1); self.cs.value(0)
        for _ in range(n // 64):
            self.spi.write(chunk)
        rem = n % 64
        if rem:
            self.spi.write(bytes([hi, lo]) * rem)
        self.cs.value(1)

    def pixel(self, x, y, color):
        """Set a single pixel."""
        if 0 <= x < self.width and 0 <= y < self.height:
            self._window(x, y, x, y)
            self.dc.value(1); self.cs.value(0)
            self.spi.write(bytes([(color >> 8) & 0xFF, color & 0xFF]))
            self.cs.value(1)

    def hline(self, x, y, w, color):
        """Draw a horizontal line."""
        self.fill_rect(x, y, w, 1, color)

    def vline(self, x, y, h, color):
        """Draw a vertical line."""
        self.fill_rect(x, y, 1, h, color)

    def rect(self, x, y, w, h, color):
        """Draw a rectangle outline."""
        self.hline(x,         y,         w,     color)
        self.hline(x,         y + h - 1, w,     color)
        self.vline(x,         y + 1,     h - 2, color)
        self.vline(x + w - 1, y + 1,     h - 2, color)

    def text(self, s, x, y, fg=WHITE, bg=BLACK, scale=1):
        """
        Render string *s* using MicroPython's built-in 8x8 framebuf font.

        Each character is 8px wide x 8px tall (multiplied by *scale*).
        Characters that would exceed the display width are clipped.

        :param s:     String to render
        :param x:     Left edge in pixels
        :param y:     Top edge in pixels
        :param fg:    Foreground colour (RGB565)
        :param bg:    Background colour (RGB565)
        :param scale: Integer magnification (1=8px, 2=16px, 3=24px …)
        """
        cw     = 8 * scale
        ch     = 8 * scale
        glyph  = bytearray(8)
        fb     = framebuf.FrameBuffer(glyph, 8, 8, framebuf.MONO_HLSB)
        fg_hi, fg_lo = (fg >> 8) & 0xFF, fg & 0xFF
        bg_hi, bg_lo = (bg >> 8) & 0xFF, bg & 0xFF
        buf    = bytearray(cw * ch * 2)

        cx = x
        for char in s:
            if cx + cw > self.width:
                break
            for i in range(8):
                glyph[i] = 0
            fb.text(char, 0, 0, 1)

            idx = 0
            for row in range(8):
                byte = glyph[row]
                for _ in range(scale):        # row repetition
                    for col in range(8):      # left to right
                        on = (byte >> (7 - col)) & 1
                        hi = fg_hi if on else bg_hi
                        lo = fg_lo if on else bg_lo
                        for _ in range(scale):  # column repetition
                            buf[idx] = hi; idx += 1
                            buf[idx] = lo; idx += 1

            self._window(cx, y, cx + cw - 1, y + ch - 1)
            self.dc.value(1); self.cs.value(0)
            self.spi.write(buf)
            self.cs.value(1)
            cx += cw
