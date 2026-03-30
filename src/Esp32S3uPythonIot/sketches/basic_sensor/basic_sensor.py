"""
basic_sensor.py — MinisIoT uPython example z VFS + ST7789 display

Sends temperature and humidity to MyCastle every 10 seconds.
Exposes the device's internal filesystem over MQTT via MinisMqttVfs.
Displays current readings on ST7789.

Pinout (ESP32-S3):
  ST7789  → ESP32-S3
  SCK     → GPIO 12
  MOSI    → GPIO 11
  CS      → GPIO 10
  DC      → GPIO  9
  RST     → GPIO  8
  BL      → GPIO  2  (backlight, HIGH = on; lub podłącz BL do 3.3V na stałe)

When deployed via MyCastle a MinisConfig.py is injected automatically with:
    MINIS_DEVICE_NAME, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD
The values below are used as fallbacks when running manually.
"""

from minis_iot import MinisIoT
from minis_vfs import Vfs
import machine
import network
import time

# Reset WiFi to a clean state (avoids "Wifi Internal State Error" after REPL interrupts)
_wlan = network.WLAN(network.STA_IF)
_wlan.active(False)
time.sleep_ms(1000)
del _wlan

# ─── Configuration (override via MinisConfig.py) ──────────────────────────────
try:
    from MinisConfig import MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD, MINIS_DEVICE_NAME
except ImportError:
    MINIS_WIFI_SSID     = 'YourWiFiNetwork'
    MINIS_WIFI_PASSWORD = 'YourWiFiPassword'
    MINIS_DEVICE_NAME   = 'YourDeviceName'

MYCASTLE_HOST = '192.168.0.207'
MYCASTLE_PORT = 1884
USER_ID       = 'marcin'

TELEMETRY_INTERVAL_S = 10

# ─── Display setup (ST7789, 240×240) ─────────────────────────────────────────
import st7789py as st7789
import vga2_bold_16x32 as font

_spi = machine.SPI(2, baudrate=10_000_000,
                   sck=machine.Pin(12),
                   mosi=machine.Pin(11))

display = st7789.ST7789(_spi, 240, 240,
                        reset=machine.Pin(8, machine.Pin.OUT),
                        cs=machine.Pin(10, machine.Pin.OUT),
                        dc=machine.Pin(9, machine.Pin.OUT),
                        backlight=machine.Pin(2, machine.Pin.OUT),
                        rotation=0,
                        color_order=st7789.BGR)
display.fill(st7789.BLACK)

def draw_display(temp, hum, connected):
    display.fill(st7789.BLACK)
    display.text(font, MINIS_DEVICE_NAME[:14], 4, 4,   st7789.color565(100, 100, 255), st7789.BLACK)
    display.text(font, 'Temp:', 4, 60, st7789.WHITE, st7789.BLACK)
    display.text(font, '{:.1f} C'.format(temp), 4, 96,  st7789.color565(255, 180, 0), st7789.BLACK)
    display.text(font, 'Hum: ', 4, 148, st7789.WHITE, st7789.BLACK)
    display.text(font, '{:.0f} %'.format(hum),  4, 184, st7789.color565(0, 200, 255), st7789.BLACK)
    status = 'Online' if connected else 'Offline'
    color  = st7789.color565(0, 255, 0) if connected else st7789.color565(255, 60, 60)
    display.text(font, status, 4, 210, color, st7789.BLACK)

# ─── Sensor stubs — replace with real readings ────────────────────────────────
def read_temperature(): return 22.5
def read_humidity():    return 60.0

# ─── MinisIoT instance ────────────────────────────────────────────────────────
minis = MinisIoT(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_NAME)
minis.set_debug(True)
minis.set_wifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD)
minis.set_heartbeat_interval(60)

# ─── VFS extension — exposes internal filesystem over MQTT ───────────────────
Vfs(minis, root='/')

draw_display(0, 0, False)
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

        draw_display(temp, hum, minis.is_connected())

    time.sleep_ms(100)
