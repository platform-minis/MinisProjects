"""
minis_display.py — Virtual display middleware for MinisIoT (MicroPython)

Implements the micropython-nano-gui driver interface as a *composition*
(not inheritance) wrapper around framebuf.FrameBuffer.  Every drawing
primitive call passes through the Display class before touching the
underlying buffer, making Display a true intermediate layer:

    Application code
         │  display.fill() / display.rect() / display.text() / …
         ▼
    Display  (this file — middleware, interception point)
         │  self._fb.fill() / self._fb.rect() / …
         ▼
    framebuf.FrameBuffer  (raw pixel buffer)
         │
         ▼  display.show()
    MQTT → MyCastle → browser canvas widget

Protocol (ext/display):
  Push  → minis/{user}/{device}/ext/display/res
           { "op": "frame", "n": <seq>, "w": <w>, "h": <h>,
             "fmt": "<format>", "data": "<base64 buf>" }

  Server → minis/{user}/{device}/ext/display/req
           Supported ops: get_config

Config string format (prop=val,prop=val,...):
  width    — display width  in pixels  (default: 128)
  height   — display height in pixels  (default: 64)
  fmt      — pixel format: MONO_VLSB | MONO_HLSB | MONO_HMSB
                            RGB565 | GS4_HMSB | GS8
             (default: MONO_VLSB)

Quick-start::

    from minis_iot import MinisIoT
    from minis_display import Display

    minis   = MinisIoT(host, port, user_id, device_id)
    display = Display(minis, 'width=128,height=64,fmt=MONO_VLSB')

    display.fill(0)
    display.text('Hello', 0, 0, 1)
    display.show()          # → MQTT → browser
"""

import framebuf
import ubinascii
import json


# ── Format table ───────────────────────────────────────────────────────────────

_FMT_MAP = {
    'MONO_VLSB': framebuf.MONO_VLSB,
    'MONO_HLSB': framebuf.MONO_HLSB,
    'MONO_HMSB': framebuf.MONO_HMSB,
    'RGB565':    framebuf.RGB565,
    'GS4_HMSB':  framebuf.GS4_HMSB,
    'GS8':       framebuf.GS8,
}


def _buf_size(fmt_name, width, height):
    if fmt_name == 'RGB565':
        return width * height * 2
    elif fmt_name == 'GS8':
        return width * height
    elif fmt_name == 'GS4_HMSB':
        return (width * height + 1) // 2
    elif fmt_name in ('MONO_HLSB', 'MONO_HMSB'):
        return ((width + 7) // 8) * height
    else:  # MONO_VLSB (default)
        return width * ((height + 7) // 8)


def _parse_config(config_str):
    cfg = {}
    if not config_str:
        return cfg
    for part in config_str.split(','):
        part = part.strip()
        if '=' in part:
            k, v = part.split('=', 1)
            cfg[k.strip()] = v.strip()
    return cfg


# ── Display ────────────────────────────────────────────────────────────────────

class Display:
    """
    Virtual nano-gui display driver — composition-based middleware.

    Wraps framebuf.FrameBuffer via *composition* so that every drawing
    primitive is explicitly routed through the Display class.  This makes
    Display a genuine interception point: future versions can track dirty
    regions, add transforms, log ops, or defer rendering — without any
    changes to application code.

    :param minis:      MinisIoT instance (registered as extension 'display').
    :param config_str: 'prop=val,prop=val,...'
                       Keys: width (int), height (int), fmt (str).
    """

    EXT_TYPE = 'display'

    def __init__(self, minis, config_str=''):
        cfg = _parse_config(config_str)

        self.width  = int(cfg.get('width',  128))
        self.height = int(cfg.get('height',  64))
        fmt_name    = cfg.get('fmt', 'MONO_VLSB').upper()

        if fmt_name not in _FMT_MAP:
            raise ValueError('Unknown display fmt: ' + fmt_name)

        self._fmt_name  = fmt_name
        self._fmt_const = _FMT_MAP[fmt_name]
        self._buf       = bytearray(_buf_size(fmt_name, self.width, self.height))

        # Inner FrameBuffer — application code must not touch this directly.
        # All drawing must go through the Display methods below.
        self._fb = framebuf.FrameBuffer(
            self._buf, self.width, self.height, self._fmt_const
        )

        self._minis     = minis
        self._frame_no  = 0
        self._res_topic = '{}/{}/res'.format(minis._t_ext_base, self.EXT_TYPE)

        minis.add_extension(self.EXT_TYPE, self._on_request)

    # ── Drawing primitives — every call enters through here ───────────────────
    #
    #  This is the middleware boundary.  Any cross-cutting concern (dirty
    #  tracking, clipping, scaling, op logging …) belongs here, not in the
    #  application sketch or in FrameBuffer itself.

    def fill(self, col):
        """Fill the entire display with colour col."""
        self._fb.fill(col)

    def pixel(self, x, y, col=None):
        """Set pixel (x, y) to col, or return its current value when col is omitted."""
        if col is None:
            return self._fb.pixel(x, y)
        self._fb.pixel(x, y, col)

    def hline(self, x, y, w, col):
        """Draw a horizontal line of width w starting at (x, y)."""
        self._fb.hline(x, y, w, col)

    def vline(self, x, y, h, col):
        """Draw a vertical line of height h starting at (x, y)."""
        self._fb.vline(x, y, h, col)

    def line(self, x1, y1, x2, y2, col):
        """Draw a straight line between (x1, y1) and (x2, y2)."""
        self._fb.line(x1, y1, x2, y2, col)

    def rect(self, x, y, w, h, col, fill=False):
        """Draw a rectangle outline; pass fill=True for a filled rectangle."""
        self._fb.rect(x, y, w, h, col, fill)

    def fill_rect(self, x, y, w, h, col):
        """Draw a filled rectangle."""
        self._fb.fill_rect(x, y, w, h, col)

    def text(self, string, x, y, col=1):
        """Render an 8×8 pixel font string at (x, y)."""
        self._fb.text(string, x, y, col)

    def scroll(self, xstep, ystep):
        """Shift the display contents by (xstep, ystep) pixels."""
        self._fb.scroll(xstep, ystep)

    def blit(self, src, x, y, key=-1, palette=None):
        """
        Blit FrameBuffer src onto this display at (x, y).

        src must be a framebuf.FrameBuffer (or a Display's .framebuffer property).
        key sets the transparent colour index; palette remaps colours.
        """
        if palette is not None:
            self._fb.blit(src, x, y, key, palette)
        else:
            self._fb.blit(src, x, y, key)

    def ellipse(self, x, y, xr, yr, col, fill=False, m=0xf):
        """Draw an ellipse (requires MicroPython >= 1.20)."""
        self._fb.ellipse(x, y, xr, yr, col, fill, m)

    def poly(self, x, y, coords, col, fill=False):
        """Draw a polygon from a flat array of (x, y) pairs (MicroPython >= 1.20)."""
        self._fb.poly(x, y, coords, col, fill)

    # ── Frame transmission ────────────────────────────────────────────────────

    def show(self):
        """
        Transmit the current frame to MyCastle via MQTT.

        Call once per frame after all drawing is done.  nano-gui calls this
        automatically via refresh(); plain sketches call it manually.
        Skipped silently when MQTT is not connected.
        """
        if not self._minis.is_connected():
            return
        try:
            data_b64 = ubinascii.b2a_base64(self._buf).decode().rstrip()
            payload = json.dumps({
                'op':   'frame',
                'n':    self._frame_no,
                'w':    self.width,
                'h':    self.height,
                'fmt':  self._fmt_name,
                'data': data_b64,
            })
            self._minis._client.publish(self._res_topic, payload, qos=0)
            self._frame_no += 1
        except Exception as e:
            print('[MinisDisplay] show() error:', e)

    # ── Helper ────────────────────────────────────────────────────────────────

    @property
    def framebuffer(self):
        """Raw framebuf.FrameBuffer — use only for blit() source, not for drawing."""
        return self._fb

    # ── Extension request handler (server → device) ───────────────────────────

    def _on_request(self, req_id, op, params):
        if op == 'get_config':
            self._minis.ext_respond(self.EXT_TYPE, req_id, True, data={
                'width':  self.width,
                'height': self.height,
                'fmt':    self._fmt_name,
            })
        else:
            self._minis.ext_respond(
                self.EXT_TYPE, req_id, False,
                error={'code': 'UnknownOp', 'message': 'Unknown op: ' + str(op)},
            )
