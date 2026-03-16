"""
basic_sensor.py — MinisIoT uPython example

Sends temperature and humidity to MyCastle every 10 seconds.
No command handling.

When deployed via MyCastle a MinisConfig.py is injected automatically with:
    MINIS_DEVICE_SN, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD, MINIS_CONFIG
The values below are used as fallbacks when running manually.
"""

from minis_iot import MinisIoT
import time

# ─── Configuration (override via MinisConfig.py) ──────────────────────────────
try:
    from MinisConfig import MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD, MINIS_DEVICE_SN
except ImportError:
    MINIS_WIFI_SSID     = 'YourWiFiNetwork'
    MINIS_WIFI_PASSWORD = 'YourWiFiPassword'
    MINIS_DEVICE_SN     = 'YourDeviceSerialNumber'

MYCASTLE_HOST = '192.168.0.89'
MYCASTLE_PORT = 1894
USER_ID       = 'marcin'

TELEMETRY_INTERVAL_S = 10

# ─── Sensor stubs — replace with real readings ────────────────────────────────
def read_temperature(): return 22.5
def read_humidity():    return 60.0

# ─── MinisIoT instance ────────────────────────────────────────────────────────
minis = MinisIoT(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_SN)
minis.set_debug(True)
minis.set_wifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD)
minis.set_heartbeat_interval(60)

print('Connecting to MyCastle...')
if not minis.begin(15000):
    print('Initial connect failed, will retry in loop()')
else:
    print('Connected!  broker:', minis.broker_uri(), ' clientId:', minis.client_id())

last_telemetry = time.ticks_ms()

# ─────────────────────────────────────────────────────────────────────────────

while True:
    minis.loop()

    now = time.ticks_ms()
    if minis.is_connected() and time.ticks_diff(now, last_telemetry) >= TELEMETRY_INTERVAL_S * 1000:
        last_telemetry = now

        temp = read_temperature()
        hum  = read_humidity()

        ok = minis.send_telemetry([
            ('temperature', temp, '°C'),
            ('humidity',    hum,  '%'),
        ])
        if ok:
            print('Sent  temp={:.1f}°C  hum={:.0f}%'.format(temp, hum))
        else:
            print('send_telemetry failed')

    time.sleep_ms(100)
