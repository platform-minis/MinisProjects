"""
sensor_with_commands.py — MinisIoT uPython example

Reads temperature, publishes telemetry with relay state, and handles
'set_relay' commands sent from MyCastle dashboard.

Demonstrates:
  - on_command() callback
  - ack_command() for success and failure
  - Mixed metric types (float + bool)
"""

from minis_iot import MinisIoT
from machine import Pin
import time

# ─── Configuration (override via MinisConfig.py) ──────────────────────────────
try:
    from MinisConfig import MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD, MINIS_DEVICE_SN
except ImportError:
    MINIS_WIFI_SSID     = 'MyWiFiNetwork'
    MINIS_WIFI_PASSWORD = 'MyWiFiPassword'
    MINIS_DEVICE_SN     = 'dev-relay1'

MYCASTLE_HOST = '192.168.0.207'
MYCASTLE_PORT = 1884
USER_ID       = 'marcin'

RELAY_PIN            = Pin(26, Pin.OUT, value=0)
TELEMETRY_INTERVAL_S = 10

# ─── Sensor stub — replace with real reading ──────────────────────────────────
def read_temperature(): return 23.0

# ─── Command handler ──────────────────────────────────────────────────────────
def handle_command(cmd_id, name, payload):
    print('[CMD] {}  id={}'.format(name, cmd_id))

    if name == 'set_relay':
        if 'state' not in payload:
            minis.ack_command(cmd_id, False, "Missing 'state' field")
            return
        state = bool(payload['state'])
        RELAY_PIN.value(1 if state else 0)
        print('Relay ->', 'ON' if state else 'OFF')
        minis.ack_command(cmd_id, True)
    else:
        minis.ack_command(cmd_id, False, 'Unknown command: ' + name)

# ─── MinisIoT instance ────────────────────────────────────────────────────────
minis = MinisIoT(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_SN)
minis.set_debug(True)
minis.set_wifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD)
minis.on_command(handle_command)
minis.set_heartbeat_interval(60)

print('Connecting to MyCastle...')
if not minis.begin(15000):
    print('Initial connect failed, will retry in loop()')
else:
    print('Connected!  broker:', minis.broker_uri())

last_telemetry = time.ticks_ms()

# ─────────────────────────────────────────────────────────────────────────────

while True:
    minis.loop()

    now = time.ticks_ms()
    if minis.is_connected() and time.ticks_diff(now, last_telemetry) >= TELEMETRY_INTERVAL_S * 1000:
        last_telemetry = now

        relay_on = bool(RELAY_PIN.value())
        minis.send_telemetry([
            ('temperature', read_temperature(), '°C'),
            ('relay_state', relay_on),
        ])

    time.sleep_ms(100)
