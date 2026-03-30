"""
primitives.py — Drawing primitives showcase for minis_display.py

Cycles through a demo of each FrameBuffer primitive exposed by Display:
  fill, pixel, hline, vline, line, rect, fill_rect, text, ellipse

Each demo runs for 2 seconds, then the next one starts.

Every drawing call (e.g. display.hline()) is routed through the Display
middleware layer — it is Display that decides how to dispatch it to the
underlying framebuf.FrameBuffer and when to ship the result via MQTT.

Display config: 128x64 MONO_VLSB
"""

from minis_iot import MinisIoT
from minis_display import Display
import network
import time
import math

_wlan = network.WLAN(network.STA_IF)
_wlan.active(False)
time.sleep_ms(500)
del _wlan

# ─── Configuration ────────────────────────────────────────────────────────────
try:
    from MinisConfig import MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD, MINIS_DEVICE_NAME
except ImportError:
    MINIS_WIFI_SSID     = 'YourWiFiNetwork'
    MINIS_WIFI_PASSWORD = 'YourWiFiPassword'
    MINIS_DEVICE_NAME   = 'YourDeviceName'

MYCASTLE_HOST = '192.168.0.207'
MYCASTLE_PORT = 1884
USER_ID       = 'marcin'

# ─── MinisIoT + Display ───────────────────────────────────────────────────────
minis   = MinisIoT(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_NAME)
minis.set_wifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD)
minis.set_heartbeat_interval(60)

display = Display(minis, 'width=128,height=64,fmt=MONO_VLSB')
W, H = display.width, display.height

# ─── Shared helpers ───────────────────────────────────────────────────────────
def label(txt):
    """Draw inverted label bar at top using display primitives."""
    display.fill_rect(0, 0, W, 10, 1)
    display.text(txt, 2, 1, 0)

# ─── Demo functions — each uses only display.xxx() calls ─────────────────────

def demo_pixel():
    display.fill(0)
    label('pixel()')
    for y in range(12, H, 4):
        for x in range(0, W, 4):
            if (x + y) % 8 == 0:
                display.pixel(x, y, 1)
    display.show()

def demo_hline():
    display.fill(0)
    label('hline()')
    for i, y in enumerate(range(13, H - 2, 7)):
        w = W - i * 8
        if w > 0:
            display.hline((W - w) // 2, y, w, 1)
    display.show()

def demo_vline():
    display.fill(0)
    label('vline()')
    for x in range(4, W, 10):
        h = H - 12 - ((x // 10) % 3) * 10
        display.vline(x, H - h, h, 1)
    display.show()

def demo_line():
    display.fill(0)
    label('line()')
    cx, cy = 0, H
    for x in range(0, W + 1, 12):
        display.line(cx, cy, x, 11, 1)
    for y in range(H, 10, -10):
        display.line(cx, cy, W, y, 1)
    display.show()

def demo_rect():
    display.fill(0)
    label('rect()')
    for i in range(5):
        m = i * 5
        display.rect(m, 11 + m, W - m * 2, H - 11 - m * 2, 1)
    display.show()

def demo_fill_rect():
    display.fill(0)
    label('fill_rect()')
    bw, bh = 16, 13
    for row in range(4):
        for col in range(8):
            if (row + col) % 2 == 0:
                display.fill_rect(col * bw, 11 + row * bh, bw, bh, 1)
    display.show()

def demo_fill():
    display.fill(1)
    display.fill_rect(0, 0, W, 10, 1)
    display.text('fill()', 2, 1, 0)
    display.fill_rect(W // 2 - 20, H // 2 - 10, 40, 20, 0)
    display.text('0', W // 2 - 3, H // 2 - 4, 1)
    display.show()

def demo_text():
    display.fill(0)
    label('text()')
    display.text('ABCDEFGHIJ', 0, 12, 1)
    display.text('0123456789', 0, 22, 1)
    display.text('Hello World!', 0, 32, 1)
    display.text('!@#$%^&*()', 0, 42, 1)
    display.text('Size: 8px', 0, 52, 1)
    display.show()

def demo_ellipse():
    display.fill(0)
    label('ellipse()')
    cx, cy = W // 2, (H + 11) // 2
    display.ellipse(cx, cy, 50, 24, 1)
    display.ellipse(cx, cy, 36, 17, 1)
    display.ellipse(cx, cy, 20, 10, 1)
    display.ellipse(cx, cy, 6,  4,  1, True)
    display.show()

DEMOS = [
    ('pixel',     demo_pixel),
    ('hline',     demo_hline),
    ('vline',     demo_vline),
    ('line',      demo_line),
    ('rect',      demo_rect),
    ('fill_rect', demo_fill_rect),
    ('fill',      demo_fill),
    ('text',      demo_text),
    ('ellipse',   demo_ellipse),
]

# ─── Main ─────────────────────────────────────────────────────────────────────
print('Connecting...')
minis.begin(15000)

INTERVAL_MS = 2000
demo_idx    = 0
last_draw   = 0

while True:
    minis.loop()

    if minis.is_connected():
        now = time.ticks_ms()
        if time.ticks_diff(now, last_draw) >= INTERVAL_MS:
            last_draw = now
            name, fn = DEMOS[demo_idx]
            fn()
            print('Demo:', name)
            demo_idx = (demo_idx + 1) % len(DEMOS)

    time.sleep_ms(50)
