"""
hello_display.py — Minimal minis_display.py example

Draws a border, title and a message, then shows the frame on MyCastle.
Updates every 3 seconds with an incrementing counter to confirm live updates.

All drawing calls (fill, rect, hline, text) pass through the Display
middleware layer before reaching the underlying framebuf.FrameBuffer.
show() transmits the composed frame to MyCastle via MQTT.

Display config: 128x64 MONO_VLSB (SSD1306-compatible resolution)

MyCastle Virtual Display page:
  /user/{userName}/iot/virtual-display/{deviceName}
"""

from minis_iot import MinisIoT
from minis_display import Display
import network
import time

# Reset WiFi to avoid stale state after REPL interrupts
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

# 128×64 monochrome — 1=white, 0=black (MONO_VLSB matches SSD1306 layout)
display = Display(minis, 'width=128,height=64,fmt=MONO_VLSB')

# ─── Draw helper ──────────────────────────────────────────────────────────────
def draw(count):
    W, H = display.width, display.height

    display.fill(0)                         # black background

    # Outer border
    display.rect(0, 0, W, H, 1)
    # Inner border (2px inset)
    display.rect(2, 2, W - 4, H - 4, 1)

    # Title
    display.text('MyCastle', 20, 6, 1)

    # Separator line
    display.hline(4, 17, W - 8, 1)

    # Message
    display.text('Hello, world!', 10, 22, 1)
    display.text('Frame: {}'.format(count), 10, 34, 1)

    # Status line at bottom
    display.hline(4, H - 12, W - 8, 1)
    display.text('Virtual Display', 8, H - 10, 1)

    display.show()

# ─── Main ─────────────────────────────────────────────────────────────────────
print('Connecting...')
minis.begin(15000)

count       = 0
last_draw   = 0
INTERVAL_MS = 3000

while True:
    minis.loop()

    if minis.is_connected():
        now = time.ticks_ms()
        if time.ticks_diff(now, last_draw) >= INTERVAL_MS:
            last_draw = now
            draw(count)
            count += 1
            print('Frame', count, 'sent')

    time.sleep_ms(50)
