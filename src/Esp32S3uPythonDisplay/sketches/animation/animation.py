"""
animation.py — Bouncing ball + scrolling ticker animation

Demonstrates smooth animation on the Virtual Display:
  • A ball bounces around the play area (physics + donut rendering)
  • A message scrolls right→left across the bottom ticker strip

Target frame rate: 20 fps (50 ms / frame).

All rendering goes through display.xxx() — the Display middleware
composes each frame in a framebuf.FrameBuffer and ships it via MQTT
on every display.show() call.

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

# ─── Layout ───────────────────────────────────────────────────────────────────
TICKER_H = 10
PLAY_H   = H - TICKER_H - 1
PLAY_Y   = 0
BALL_R   = 4

# ─── Ball helpers — draw through display middleware ───────────────────────────

def draw_circle(cx, cy, r, col):
    """Filled circle via horizontal scanlines — all calls through display.hline()."""
    for dy in range(-r, r + 1):
        dx = int(math.sqrt(r * r - dy * dy))
        display.hline(int(cx) - dx, int(cy) + dy, dx * 2 + 1, col)

def draw_ball(cx, cy):
    """Donut ball: filled disc with hollowed centre, plus crosshair guides."""
    # Guides
    display.vline(int(cx), PLAY_Y, PLAY_H, 1)
    display.hline(0, int(cy), W, 1)
    # Outer disc
    draw_circle(cx, cy, BALL_R, 1)
    # Inner cutout (donut effect)
    if BALL_R > 2:
        draw_circle(cx, cy, BALL_R - 2, 0)

# ─── Ball state ───────────────────────────────────────────────────────────────
bx, by = float(W // 3), float(PLAY_Y + BALL_R + 2)
vx, vy = 2.3, 1.7

def update_ball():
    global bx, by, vx, vy
    bx += vx
    by += vy
    if bx - BALL_R < 0:
        bx = float(BALL_R);         vx = abs(vx)
    elif bx + BALL_R >= W:
        bx = float(W - BALL_R - 1); vx = -abs(vx)
    if by - BALL_R < PLAY_Y:
        by = float(PLAY_Y + BALL_R); vy = abs(vy)
    elif by + BALL_R >= PLAY_Y + PLAY_H:
        by = float(PLAY_Y + PLAY_H - BALL_R - 1); vy = -abs(vy)

# ─── Ticker ───────────────────────────────────────────────────────────────────
SCROLL_MSG   = '  MyCastle Virtual Display  —  animation demo  —  display middleware  '
MSG_PW       = len(SCROLL_MSG) * 8
TICKER_Y     = H - TICKER_H
SCROLL_SPEED = 2
scroll_x     = W

def update_ticker():
    global scroll_x
    scroll_x -= SCROLL_SPEED
    if scroll_x < -MSG_PW:
        scroll_x = W

def draw_ticker():
    display.fill_rect(0, TICKER_Y, W, TICKER_H, 0)
    display.hline(0, TICKER_Y, W, 1)
    x = scroll_x
    for ch in SCROLL_MSG:
        if 0 <= x < W:
            display.text(ch, x, TICKER_Y + 1, 1)
        x += 8
        if x >= W:
            break

# ─── Main ─────────────────────────────────────────────────────────────────────
print('Connecting...')
minis.begin(15000)

TARGET_FPS  = 20
FRAME_MS    = 1000 // TARGET_FPS
last_frame  = 0
frame_count = 0

while True:
    minis.loop()

    if minis.is_connected():
        now = time.ticks_ms()
        if time.ticks_diff(now, last_frame) >= FRAME_MS:
            last_frame = now

            update_ball()
            update_ticker()

            display.fill(0)
            draw_ball(bx, by)
            draw_ticker()
            display.show()     # middleware ships the composed frame via MQTT

            frame_count += 1
            if frame_count % 100 == 0:
                print('Frame', frame_count, '({:.0f},{:.0f})'.format(bx, by))

    time.sleep_ms(10)
