"""
entities_demo.py — MinisIoT entities demo

Simulates a smart thermostat demonstrating all six IotEntity types:

  SensorEntity        — temperature and humidity readings
  BinarySensorEntity  — heater active status (derived from mode + target temp)
  SwitchEntity        — manual heater override (on / off)
  NumberEntity        — target temperature setpoint
  ButtonEntity        — force sensor re-read
  SelectEntity        — operating mode (heat / cool / fan / off)

When deployed via MyCastle a MinisConfig.py is injected automatically with:
    MINIS_DEVICE_NAME, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD
The values below are used as fallbacks when running manually.
"""

from minis_iot      import MinisIoT
from minis_entities import (
    SensorEntity,
    BinarySensorEntity,
    SwitchEntity,
    NumberEntity,
    ButtonEntity,
    SelectEntity,
)
import time

# ─── Configuration (override via MinisConfig.py) ──────────────────────────────
try:
    from MinisConfig import MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD, MINIS_DEVICE_NAME
except ImportError:
    MINIS_WIFI_SSID     = 'YourWiFiNetwork'
    MINIS_WIFI_PASSWORD = 'YourWiFiPassword'
    MINIS_DEVICE_NAME   = 'thermostat-01'

MYCASTLE_HOST = '192.168.0.207'
MYCASTLE_PORT = 1884
USER_ID       = 'marcin'

TELEMETRY_INTERVAL_S = 10

# ─── State ────────────────────────────────────────────────────────────────────
target_temp   = 21.0          # °C  — updated by NumberEntity command
mode          = 'heat'        # updated by SelectEntity command
heater_on     = False         # updated by SwitchEntity command
force_refresh = False         # set by ButtonEntity, consumed in main loop

# ─── Sensor stubs — replace with real hardware reads ─────────────────────────
def read_temperature(): return 19.5
def read_humidity():    return 55.0

# ─── Entity callbacks ─────────────────────────────────────────────────────────

def send_state():
    """Push current state to MyCastle immediately after any writable entity changes."""
    temp = read_temperature()
    hum  = read_humidity()
    is_active = heater_on and mode == 'heat' and temp < target_temp or \
                heater_on and mode == 'cool' and temp > target_temp
    minis.send_telemetry([
        ('temperature',   temp),
        ('humidity',      hum),
        ('heater_active', is_active),
        # Report the current setpoint and mode so dashboards stay in sync
        ('target_temp',   target_temp),
        ('heater_switch', heater_on),
        ('mode',          mode),
    ])


def on_heater_switch(state):
    global heater_on
    heater_on = state
    print('[heater_switch] state={}'.format(state))
    send_state()  # reflect new value on server immediately

def on_target_temp(value):
    global target_temp
    target_temp = float(value)
    print('[target_temp] value={:.1f}°C'.format(target_temp))
    send_state()

def on_mode_select(value):
    global mode
    mode = value
    print('[mode_select] mode={}'.format(mode))
    send_state()

def on_force_refresh():
    global force_refresh
    force_refresh = True
    print('[force_refresh] triggered')

# ─── Entity definitions ───────────────────────────────────────────────────────

# Read-only: numeric sensors — values sent via send_telemetry()
e_temperature = SensorEntity(
    'temperature', 'Temperature',
    unit='°C', device_class='temperature',
)
e_humidity = SensorEntity(
    'humidity', 'Humidity',
    unit='%', device_class='humidity',
)

# Read-only: boolean indicator — value sent via send_telemetry()
e_heater_active = BinarySensorEntity(
    'heater_active', 'Heater Active',
    on_label='Heating', off_label='Idle',
    device_class='heat',
)

# Writable: toggle — auto-acked, callback called with bool
e_heater_switch = SwitchEntity(
    'heater_switch', 'Heater Override',
    callback=on_heater_switch,
)

# Writable: number with range — auto-acked, callback called with float
e_target_temp = NumberEntity(
    'target_temp', 'Target Temperature',
    min_val=5, max_val=35, step=0.5,
    unit='°C', callback=on_target_temp,
    device_class='temperature',
)

# Writable: momentary button — auto-acked, callback called with no args
e_force_refresh = ButtonEntity(
    'force_refresh', 'Force Sensor Refresh',
    callback=on_force_refresh,
)

# Writable: enum selector — auto-acked, callback called with str
e_mode = SelectEntity(
    'mode', 'Operating Mode',
    options=['heat', 'cool', 'fan', 'off'],
    callback=on_mode_select,
)

# ─── MinisIoT setup ───────────────────────────────────────────────────────────
minis = MinisIoT(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_NAME)
minis.set_debug(True)
minis.set_wifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD)
minis.set_heartbeat_interval(60)

# Register all entities — announced in the hello message
minis.add_entity(e_temperature)
minis.add_entity(e_humidity)
minis.add_entity(e_heater_active)
minis.add_entity(e_heater_switch)
minis.add_entity(e_target_temp)
minis.add_entity(e_force_refresh)
minis.add_entity(e_mode)

print('Connecting to MyCastle...')
if not minis.begin(15000):
    print('Initial connect failed, will retry in loop()')
else:
    print('Connected!  broker:', minis.broker_uri())

last_telemetry = time.ticks_ms()

# ─── Main loop ────────────────────────────────────────────────────────────────

while True:
    minis.loop()

    now = time.ticks_ms()

    # Force-refresh overrides the normal interval
    send_now = force_refresh
    if force_refresh:
        force_refresh = False

    if minis.is_connected() and (
        send_now or time.ticks_diff(now, last_telemetry) >= TELEMETRY_INTERVAL_S * 1000
    ):
        last_telemetry = now
        send_state()
        print('Periodic telemetry sent')

    time.sleep_ms(100)
