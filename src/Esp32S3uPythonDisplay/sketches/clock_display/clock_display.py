"""
clock_display.py — Digital clock on Virtual Display

Shows a large digital clock with date line and seconds progress bar.
Updates every second using machine.RTC (set via NTP if available).

All drawing (fill_rect, hline, text) passes through the Display
middleware — Display routes each call to the inner framebuf.FrameBuffer
and ships the finished frame to MyCastle on display.show().

Display config: 128x64 MONO_VLSB
"""

from minis_iot import MinisIoT
from minis_display import Display
import network
import time

try:
    import ntptime
    _HAS_NTP = True
except ImportError:
    _HAS_NTP = False

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

TIMEZONE_OFFSET = 1   # UTC+1 (CET); adjust as needed

# ─── MinisIoT + Display ───────────────────────────────────────────────────────
minis   = MinisIoT(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_NAME)
minis.set_wifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD)
minis.set_heartbeat_interval(60)

display = Display(minis, 'width=128,height=64,fmt=MONO_VLSB')
W, H = display.width, display.height

# ─── 3×5 tiny pixel font (digits 0-9 and colon) ──────────────────────────────
# Each glyph is 3 wide × 5 tall, stored as 5-byte rows (bit3=leftmost)
_TINY = {
    '0': [0b111, 0b101, 0b101, 0b101, 0b111],
    '1': [0b010, 0b110, 0b010, 0b010, 0b111],
    '2': [0b111, 0b001, 0b111, 0b100, 0b111],
    '3': [0b111, 0b001, 0b111, 0b001, 0b111],
    '4': [0b101, 0b101, 0b111, 0b001, 0b001],
    '5': [0b111, 0b100, 0b111, 0b001, 0b111],
    '6': [0b111, 0b100, 0b111, 0b101, 0b111],
    '7': [0b111, 0b001, 0b001, 0b001, 0b001],
    '8': [0b111, 0b101, 0b111, 0b101, 0b111],
    '9': [0b111, 0b101, 0b111, 0b001, 0b111],
    ':': [0b000, 0b010, 0b000, 0b010, 0b000],
}

# Large digit: each tiny glyph drawn at 4× scale → 12×20 px per char
DIGIT_SCALE = 4
DIGIT_W     = 3 * DIGIT_SCALE   # 12
DIGIT_H     = 5 * DIGIT_SCALE   # 20

def draw_big_char(x, y, ch, col=1):
    rows = _TINY.get(ch)
    if rows is None:
        return
    for row_idx, row in enumerate(rows):
        for bit in range(3):
            if row & (0b100 >> bit):
                px = x + bit * DIGIT_SCALE
                py = y + row_idx * DIGIT_SCALE
                display.fill_rect(px, py, DIGIT_SCALE, DIGIT_SCALE, col)

def draw_big_time(hh, mm, ss):
    """Draw HH:MM:SS centered, large digits."""
    text = '{:02d}:{:02d}:{:02d}'.format(hh, mm, ss)
    # chars: 8 digits + 2 colons
    # colon width = DIGIT_W (same slot width for simplicity)
    total_w = len(text) * DIGIT_W + (len(text) - 1) * 2
    x = (W - total_w) // 2
    y = 10
    for ch in text:
        draw_big_char(x, y, ch)
        x += DIGIT_W + 2

# ─── RTC helpers ─────────────────────────────────────────────────────────────
_ntp_synced = False

def _get_time():
    """Return (year, month, day, weekday, hh, mm, ss) in local time."""
    import machine
    t = machine.RTC().datetime()       # (Y, M, D, wday, H, M, S, sub)
    hh = (t[4] + TIMEZONE_OFFSET) % 24
    return t[0], t[1], t[2], t[3], hh, t[5], t[6]

_DAYS   = ('Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun')
_MONTHS = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
           'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')

# ─── Draw ─────────────────────────────────────────────────────────────────────
def draw():
    y, mo, d, wd, hh, mm, ss = _get_time()

    display.fill(0)

    # Top line: day-of-week  date
    date_str = '{} {:02d} {} {}'.format(_DAYS[wd], d, _MONTHS[mo - 1], y)
    # Center the date string (8px per char)
    x_date = max(0, (W - len(date_str) * 8) // 2)
    display.text(date_str, x_date, 0, 1)

    # Separator
    display.hline(0, 9, W, 1)

    # Big time
    draw_big_time(hh, mm, ss)

    # Seconds progress bar at bottom
    bar_y = H - 7
    display.hline(0, bar_y, W, 1)
    display.text('sec', 0, H - 6, 1)
    bar_x   = 24
    bar_w   = W - bar_x - 1
    filled  = int(ss / 59 * bar_w)
    display.fill_rect(bar_x, bar_y + 1, filled, 5, 1)

    display.show()

# ─── Main ─────────────────────────────────────────────────────────────────────
print('Connecting...')
minis.begin(15000)

# Sync time via NTP once after connect
if _HAS_NTP and minis.is_connected():
    try:
        ntptime.settime()
        _ntp_synced = True
        print('NTP synced')
    except Exception as e:
        print('NTP failed:', e)

INTERVAL_MS = 1000
last_draw   = 0

while True:
    minis.loop()

    if minis.is_connected():
        now = time.ticks_ms()
        if time.ticks_diff(now, last_draw) >= INTERVAL_MS:
            last_draw = now
            draw()

    time.sleep_ms(50)
