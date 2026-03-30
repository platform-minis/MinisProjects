"""
sensor_bars.py — Sensor bar chart on Virtual Display

Reads (or simulates) sensor values and draws a horizontal bar chart.
Also publishes real telemetry to MyCastle for the IoT dashboard.

Every rect, fill_rect, hline, and text call passes through the Display
middleware.  Display composes the frame in a framebuf.FrameBuffer and
ships it to MyCastle via MQTT on each display.show().

Sensors simulated (replace read_sensors() with real hardware reads):
  temperature  10–40 °C
  humidity      0–100 %
  pressure    950–1050 hPa
  light         0–100 %

Refresh: every 2 seconds.

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

# ─── Sensor simulation (replace with real reads if hardware available) ────────
_t = 0.0   # simulation phase

def read_sensors():
    global _t
    _t += 0.05
    temperature = 25.0 + 8.0 * math.sin(_t)
    humidity    = 55.0 + 30.0 * math.sin(_t * 0.7 + 1.0)
    pressure    = 1013.0 + 20.0 * math.sin(_t * 0.4 + 2.0)
    light       = 50.0 + 45.0 * math.sin(_t * 1.3 + 0.5)
    return temperature, humidity, pressure, light

# ─── Bar chart layout ─────────────────────────────────────────────────────────
# Title row height
TITLE_H = 9

# 4 bars, evenly spaced below title
BAR_COUNT   = 4
LABEL_W     = 40        # px reserved for label
BAR_X       = LABEL_W
BAR_AREA_W  = W - LABEL_W - 2
BAR_H       = 10
BAR_GAP     = (H - TITLE_H - BAR_COUNT * BAR_H) // (BAR_COUNT + 1)

SENSORS = [
    # (label, unit, min_val, max_val)
    ('Temp',  '°C', 10.0,   40.0),
    ('Humi',  '%',   0.0,  100.0),
    ('Pres',  'hPa', 950.0, 1050.0),
    ('Light', '%',   0.0,  100.0),
]

def _clamp(v, lo, hi):
    return max(lo, min(hi, v))

def draw_bars(values):
    display.fill(0)

    # Title
    display.text('Sensor Bars', 10, 0, 1)
    display.hline(0, TITLE_H - 1, W, 1)

    for i, ((label, unit, lo, hi), val) in enumerate(zip(SENSORS, values)):
        y = TITLE_H + BAR_GAP + i * (BAR_H + BAR_GAP)

        # Label
        display.text(label, 0, y + 1, 1)

        # Bar outline
        display.rect(BAR_X, y, BAR_AREA_W, BAR_H, 1)

        # Filled portion
        frac  = (val - lo) / (hi - lo)
        frac  = _clamp(frac, 0.0, 1.0)
        fill  = int(frac * (BAR_AREA_W - 2))
        if fill > 0:
            display.fill_rect(BAR_X + 1, y + 1, fill, BAR_H - 2, 1)

        # Value text overlaid on bar (white-on-fill or black-on-empty)
        if unit == 'hPa':
            val_str = '{:.0f}'.format(val)
        elif unit == '%':
            val_str = '{:.0f}%'.format(val)
        else:
            val_str = '{:.1f}{}'.format(val, unit)

        # Fit text inside bar
        tx = BAR_X + 2
        ty = y + 1
        # Draw with inverted colour so it contrasts against the fill
        display.text(val_str, tx, ty, 0 if fill > len(val_str) * 8 else 1)

    display.show()

# ─── Main ─────────────────────────────────────────────────────────────────────
print('Connecting...')
minis.begin(15000)

INTERVAL_MS    = 2000
TELEMETRY_EVERY = 5   # send telemetry every N redraws
count     = 0
last_draw = 0

while True:
    minis.loop()

    if minis.is_connected():
        now = time.ticks_ms()
        if time.ticks_diff(now, last_draw) >= INTERVAL_MS:
            last_draw = now
            temp, humi, pres, light = read_sensors()
            draw_bars([temp, humi, pres, light])
            count += 1
            print('Frame {} — T={:.1f} H={:.0f} P={:.0f} L={:.0f}'.format(
                count, temp, humi, pres, light))

            # Publish telemetry periodically
            if count % TELEMETRY_EVERY == 0:
                minis.send_telemetry([
                    ('temperature', round(temp, 1), '°C'),
                    ('humidity',    round(humi, 0), '%'),
                    ('pressure',    round(pres, 0), 'hPa'),
                    ('light',       round(light, 0), '%'),
                ])

    time.sleep_ms(50)
