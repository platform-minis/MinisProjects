"""
iot_dashboard.py — CheapYellowDisplay IoT dashboard for MyCastle.

Reads the LDR (ambient light sensor, GPIO 34) every 10 s and sends
telemetry to MyCastle.  Displays live readings + connection status
on the ILI9341 320x240 display.  Exposes the device filesystem via
the VFS MQTT extension.

Hardware (CYD / ESP32-2432S028):
    Display   ILI9341  SPI1 MOSI=13 MISO=12 CLK=14 CS=15 DC=2 BL=21
    RGB LED   (active LOW)  R=4  G=16  B=17
    LDR       ADC  GPIO=34

When deployed via MyCastle a MinisConfig.py is injected with:
    MINIS_DEVICE_NAME, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD
The values below are fallbacks for manual testing.
"""

import machine
import network
import time
from ili9341 import ILI9341, color565, BLACK, WHITE, RED, GREEN, GRAY
from minis_iot import MinisIoT
from minis_vfs import Vfs

# ── Configuration ─────────────────────────────────────────────────────────────
try:
    from MinisConfig import MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD, MINIS_DEVICE_NAME
except ImportError:
    MINIS_WIFI_SSID     = 'YourWiFiNetwork'
    MINIS_WIFI_PASSWORD = 'YourWiFiPassword'
    MINIS_DEVICE_NAME   = 'YourDeviceName'

MYCASTLE_HOST        = '192.168.0.207'
MYCASTLE_PORT        = 1884
USER_ID              = 'marcin'
TELEMETRY_INTERVAL_S = 10

# ── Reset WiFi (avoids "Wifi Internal State Error" after REPL interrupts) ────
_wlan = network.WLAN(network.STA_IF)
_wlan.active(False)
time.sleep_ms(1000)
del _wlan

# ── Display ───────────────────────────────────────────────────────────────────
spi = machine.SPI(1, baudrate=40_000_000,
                  sck=machine.Pin(14), mosi=machine.Pin(13), miso=machine.Pin(12))
display = ILI9341(spi,
                  cs=machine.Pin(15, machine.Pin.OUT),
                  dc=machine.Pin(2,  machine.Pin.OUT),
                  bl=machine.Pin(21, machine.Pin.OUT),
                  rotation=1)   # landscape 320x240

# ── RGB LED (active LOW) ──────────────────────────────────────────────────────
led_r = machine.Pin(4,  machine.Pin.OUT, value=1)
led_g = machine.Pin(16, machine.Pin.OUT, value=1)
led_b = machine.Pin(17, machine.Pin.OUT, value=1)

def set_led(r=0, g=0, b=0):
    led_r.value(0 if r else 1)
    led_g.value(0 if g else 1)
    led_b.value(0 if b else 1)

# ── LDR ───────────────────────────────────────────────────────────────────────
ldr = machine.ADC(machine.Pin(34))
ldr.atten(machine.ADC.ATTN_11DB)

# ── UI colours ────────────────────────────────────────────────────────────────
C_HEADER = color565(0,  80, 160)
C_VALUE  = color565(255, 200,  0)
C_LABEL  = color565(160, 160, 160)
C_ONLINE = color565(0,  200,  80)
C_OFFL   = color565(210,  50,  50)
C_BAR    = color565(0,   40,  80)

def draw_ui(light, connected, sent):
    display.fill(BLACK)

    # Header
    display.fill_rect(0, 0, 320, 40, C_HEADER)
    name = MINIS_DEVICE_NAME[:16]
    display.text(name, 6, 8, WHITE, C_HEADER, scale=2)

    # Status badge (top-right)
    if connected:
        display.fill_rect(238, 4, 78, 32, C_ONLINE)
        display.text('Online', 240, 10, BLACK, C_ONLINE, scale=2)
        set_led(0, 1, 0)
    else:
        display.fill_rect(238, 4, 78, 32, C_OFFL)
        display.text('Offlne', 240, 10, BLACK, C_OFFL, scale=2)
        set_led(1, 0, 0)

    # Light reading
    display.text('Light', 10, 56, C_LABEL, BLACK, scale=2)
    display.text('{:4d} raw'.format(light), 10, 82, C_VALUE, BLACK, scale=3)

    # Light bar (visual)
    bar_w = min(300, light * 300 // 4095)
    display.fill_rect(10, 142, 300, 14, C_BAR)
    if bar_w:
        display.fill_rect(10, 142, bar_w, 14, C_ONLINE)

    # Sent counter
    display.text('Sent: {}'.format(sent), 10, 168, C_LABEL, BLACK, scale=2)

    # Footer
    display.hline(0, 220, 320, C_HEADER)
    display.text(MYCASTLE_HOST + ':' + str(MYCASTLE_PORT), 6, 224, GRAY, BLACK, scale=1)

# ── MinisIoT ──────────────────────────────────────────────────────────────────
minis = MinisIoT(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_NAME)
minis.set_debug(True)
minis.set_wifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD)
minis.set_heartbeat_interval(60)

# VFS extension — exposes /flash filesystem over MQTT
Vfs(minis, root='/')

# ── Startup ───────────────────────────────────────────────────────────────────
draw_ui(0, False, 0)
print('Connecting to MyCastle...')
if not minis.begin(15000):
    print('Initial connect failed — will retry in loop()')
else:
    print('Connected!  broker:', minis.broker_uri())

sent       = 0
last_t     = time.ticks_ms()

# ── Main loop ─────────────────────────────────────────────────────────────────
while True:
    minis.loop()

    now = time.ticks_ms()
    if time.ticks_diff(now, last_t) >= TELEMETRY_INTERVAL_S * 1000:
        last_t = now
        light  = ldr.read()

        if minis.is_connected():
            ok = minis.send_telemetry([('light', light, 'raw')])
            if ok:
                sent += 1
                print('Sent  light={}  total={}'.format(light, sent))
            else:
                print('send_telemetry failed')

        draw_ui(light, minis.is_connected(), sent)

    time.sleep_ms(100)
