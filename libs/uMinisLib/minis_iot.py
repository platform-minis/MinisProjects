"""
minis_iot.py — MicroPython MinisIoT library for MyCastle IoT platform

Transport: MQTT 3.1.1 over plain TCP (umqtt.simple)
Port:      1884 (MQTT_TCP_PORT on MyCastle backend)

Quick start::

    from minis_iot import MinisIoT

    minis = MinisIoT('192.168.1.100', 1884, 'marcin', 'my-device-sn')
    minis.set_wifi('MySSID', 'MyPassword')
    minis.begin()

    while True:
        minis.loop()
        minis.send_telemetry([('temperature', 22.5, '°C')])
        time.sleep(10)

When deployed via MyCastle a MinisConfig.py is injected with:
    MINIS_DEVICE_SN, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD
"""

import network
import time
import json
import gc
from umqtt.simple import MQTTClient

_ticks = time.ticks_ms
_diff  = time.ticks_diff


# ─────────────────────────────────────────────────────────────────────────────
# _TcpMqttClient — thin wrapper around umqtt.simple
# ─────────────────────────────────────────────────────────────────────────────

class _TcpMqttClient:
    """Wraps umqtt.simple to match the interface expected by MinisIoT."""

    def __init__(self, host, port, client_id, keepalive=60, debug=False):
        self._mqtt = MQTTClient(client_id, host, port=port, keepalive=keepalive)
        self._pending = None
        self._mqtt.set_callback(self._on_msg)

    def _on_msg(self, topic, payload):
        self._pending = (
            topic.decode()   if isinstance(topic,   bytes) else topic,
            payload.decode() if isinstance(payload, bytes) else payload,
        )

    def connect(self, timeout_ms=10000):
        self._mqtt.connect()

    def disconnect(self):
        try:
            self._mqtt.disconnect()
        except Exception:
            pass

    def publish(self, topic, payload, qos=0):
        self._mqtt.publish(topic, payload, qos=qos)

    def subscribe(self, topic, qos=1):
        self._mqtt.subscribe(topic, qos)

    def check_msg(self):
        """Non-blocking. Returns (topic, payload) or None."""
        self._pending = None
        self._mqtt.check_msg()
        return self._pending

    def ping(self):
        self._mqtt.ping()


# ─────────────────────────────────────────────────────────────────────────────
# MinisIoT — public API (mirrors the Arduino MinisIoT library)
# ─────────────────────────────────────────────────────────────────────────────

class MinisIoT:
    """
    MicroPython client for the MyCastle IoT platform.

    API mirrors the Arduino MinisIoT C++ library.

    :param host:      MyCastle hostname or IP  (e.g. '192.168.1.100')
    :param port:      Plain TCP MQTT port      (default: 1884)
    :param user_id:   User ID in MyCastle      (e.g. 'marcin')
    :param device_id: Device SN / ID           (e.g. 'dev-001')
    """

    def __init__(self, host, port, user_id, device_id):
        self._host     = host
        self._port     = port
        self._user     = user_id
        self._device   = device_id
        self._ssid     = None
        self._password = None
        self._cmd_cb   = None
        self._hb_sec   = 60
        self._debug    = False
        self._connected = False
        self._client   = None
        self._last_hb  = 0
        self._last_wifi_retry = 0

        self._t_telemetry = 'minis/{}/{}/telemetry'.format(user_id, device_id)
        self._t_hello     = 'minis/{}/{}/hello'.format(user_id, device_id)
        self._t_heartbeat = 'minis/{}/{}/heartbeat'.format(user_id, device_id)
        self._t_command   = 'minis/{}/{}/command'.format(user_id, device_id)
        self._t_cmd_ack   = 'minis/{}/{}/command/ack'.format(user_id, device_id)
        self._client_id   = 'minis-{}'.format(device_id)
        self._t_ext_base  = 'minis/{}/{}/ext'.format(user_id, device_id)

        # extension_type → callback(req_id, op, params_dict)
        self._extensions     = {}
        # extension_type → req topic string
        self._ext_req_topics = {}

    # ── Configuration (call before begin()) ───────────────────────────────────

    def set_wifi(self, ssid, password):
        """Set WiFi credentials. If not called, WiFi must be already connected."""
        self._ssid     = ssid
        self._password = password

    def add_extension(self, ext_type, callback):
        """
        Register a device-side extension handler.

        :param ext_type: Extension type string, e.g. 'vkbd', 'vmouse'
        :param callback: callback(req_id: str, op: str, params: dict)
                         Call ext_respond() after processing.

        The extension is announced in the next hello message and
        subscribed on the next MQTT connect.
        """
        self._extensions[ext_type] = callback
        self._ext_req_topics[ext_type] = '{}/{}/req'.format(self._t_ext_base, ext_type)

    def ext_respond(self, ext_type, req_id, ok, data=None, error=None):
        """
        Publish the response for an extension request.

        :param ext_type: Extension type string (must match add_extension call)
        :param req_id:   Request ID from the callback
        :param ok:       True on success, False on failure
        :param data:     Optional result dict (e.g. {'x': 100, 'y': 200})
        :param error:    Optional error dict (e.g. {'code': 'Error', 'message': '...'})
        :returns: True if published successfully
        """
        if not self._connected:
            return False
        try:
            payload = {'id': req_id, 'ok': ok}
            if data is not None:
                payload['data'] = data
            if error is not None:
                payload['error'] = error
            res_topic = '{}/{}/res'.format(self._t_ext_base, ext_type)
            self._client.publish(res_topic, json.dumps(payload), qos=1)
            return True
        except Exception as e:
            self._log('ext_respond error: {}', e)
            return False

    def on_command(self, callback):
        """
        Register command callback: callback(id: str, name: str, payload: dict)
        Always call ack_command(id, ...) after processing.
        """
        self._cmd_cb = callback

    def set_heartbeat_interval(self, seconds):
        """
        Heartbeat interval in seconds (default: 60).
        Set to 0 to disable automatic heartbeat.
        """
        self._hb_sec = seconds

    def set_debug(self, enabled):
        """Enable verbose logging to REPL."""
        self._debug = enabled

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    def begin(self, timeout_ms=15000):
        """
        Connect WiFi (if credentials set) + connect to MQTT broker.
        Returns True on success.
        If False is returned, loop() will retry automatically.
        """
        if self._ssid:
            if not self._connect_wifi(timeout_ms):
                self._log('WiFi failed — will retry in loop()')
                return False
        return self._connect_mqtt(timeout_ms)

    def loop(self):
        """
        Must be called every iteration of the main loop.
        Dispatches incoming commands, sends periodic heartbeats, auto-reconnects.
        """
        # WiFi watchdog
        if self._ssid:
            sta = network.WLAN(network.STA_IF)
            if not sta.isconnected():
                self._connected = False
                now = _ticks()
                if _diff(now, self._last_wifi_retry) >= 5000:
                    self._log('WiFi down, retrying...')
                    try:
                        sta.connect(self._ssid, self._password)
                    except Exception:
                        pass
                    self._last_wifi_retry = now
                return

        # MQTT reconnect
        if not self._connected:
            try:
                self._connect_mqtt(5000)
            except Exception as e:
                self._log('MQTT reconnect failed: {}', e)
                return

        # Dispatch incoming messages
        try:
            result = self._client.check_msg()
            if result:
                self._dispatch(*result)
        except Exception as e:
            self._log('check_msg error: {}', e)
            self._connected = False
            return

        # Auto-heartbeat
        if self._hb_sec > 0:
            if _diff(_ticks(), self._last_hb) >= self._hb_sec * 1000:
                self.send_heartbeat()

    # ── Telemetry ─────────────────────────────────────────────────────────────

    def send_telemetry(self, metrics, battery=None):
        """
        Publish sensor readings to MyCastle.

        :param metrics: list of (key, value) or (key, value, unit) tuples
                        e.g. [('temperature', 22.5, '°C'), ('motion', True)]
        :param battery: battery voltage in volts (float) or None
        :returns: True if published successfully
        """
        if not self._connected:
            return False
        try:
            sta = network.WLAN(network.STA_IF)
            rssi = 0
            try:
                rssi = sta.status('rssi')
            except Exception:
                pass
            payload = {'metrics': [], 'rssi': rssi}
            for m in metrics:
                entry = {'key': m[0], 'value': m[1]}
                if len(m) >= 3:
                    entry['unit'] = m[2]
                payload['metrics'].append(entry)
            if battery is not None:
                payload['battery'] = battery
            self._client.publish(self._t_telemetry, json.dumps(payload), qos=1)
            return True
        except Exception as e:
            self._log('sendTelemetry error: {}', e)
            self._connected = False
            return False

    # ── Hello ─────────────────────────────────────────────────────────────────

    def send_hello(self):
        """
        Announce device presence to MyCastle.
        Called automatically after every successful MQTT connection.
        Immediately sets device status to ONLINE on the server side.
        """
        if not self._connected:
            return False
        try:
            payload = {'uptime': time.ticks_ms() // 1000}
            if self._extensions:
                payload['extensions'] = [
                    {'type': t, 'enabled': True} for t in self._extensions
                ]
            self._client.publish(self._t_hello, json.dumps(payload), qos=1)
            self._log('Hello sent (extensions={})', list(self._extensions.keys()))
            return True
        except Exception as e:
            self._log('sendHello error: {}', e)
            self._connected = False
            return False

    # ── Heartbeat ─────────────────────────────────────────────────────────────

    def send_heartbeat(self, battery=None):
        """
        Manually send a heartbeat to keep the device ONLINE.

        :param battery: battery voltage in volts (float) or None
        :returns: True if published successfully
        """
        if not self._connected:
            return False
        try:
            payload = {'uptime': time.ticks_ms() // 1000}
            try:
                sta = network.WLAN(network.STA_IF)
                payload['rssi'] = sta.status('rssi')
            except Exception:
                pass
            if battery is not None:
                payload['battery'] = battery
            self._client.publish(self._t_heartbeat, json.dumps(payload), qos=1)
            self._last_hb = _ticks()
            return True
        except Exception as e:
            self._log('sendHeartbeat error: {}', e)
            self._connected = False
            return False

    # ── Command ACK ───────────────────────────────────────────────────────────

    def ack_command(self, cmd_id, success=True, reason=None):
        """
        Acknowledge a command received via on_command callback.
        MUST be called after every command, even on failure.

        :param cmd_id:  Command UUID (from the callback)
        :param success: True → 'EXECUTED', False → 'FAILED'
        :param reason:  Error description (only used when success=False)
        :returns: True if published successfully
        """
        if not self._connected:
            return False
        try:
            payload = {'id': cmd_id, 'status': 'EXECUTED' if success else 'FAILED'}
            if not success and reason:
                payload['reason'] = reason
            self._client.publish(self._t_cmd_ack, json.dumps(payload), qos=1)
            return True
        except Exception as e:
            self._log('ackCommand error: {}', e)
            return False

    # ── Status ────────────────────────────────────────────────────────────────

    def is_connected(self):
        """True when MQTT is connected to MyCastle broker."""
        return self._connected

    def broker_uri(self):
        """Broker URI."""
        return 'mqtt://{}:{}'.format(self._host, self._port)

    def client_id(self):
        """MQTT client ID (format: minis-{device_id})."""
        return self._client_id

    # ── Private helpers ───────────────────────────────────────────────────────

    def _connect_wifi(self, timeout_ms):
        sta = network.WLAN(network.STA_IF)
        sta.active(True)
        if sta.isconnected():
            return True
        self._log('Connecting WiFi: {}', self._ssid)
        sta.connect(self._ssid, self._password)
        deadline = _ticks() + timeout_ms
        while not sta.isconnected():
            if _diff(deadline, _ticks()) <= 0:
                self._log('WiFi timeout')
                return False
            time.sleep_ms(250)
        self._log('WiFi connected  IP: {}', sta.ifconfig()[0])
        return True

    def _connect_mqtt(self, timeout_ms):
        try:
            if self._client:
                try:
                    self._client.disconnect()
                except Exception:
                    pass
            self._client = _TcpMqttClient(
                self._host, self._port, self._client_id,
                keepalive=self._hb_sec or 60,
                debug=self._debug,
            )
            self._client.connect(timeout_ms)
            if self._cmd_cb:
                self._client.subscribe(self._t_command, qos=1)
                self._log('Subscribed: {}', self._t_command)
            for ext_type, req_topic in self._ext_req_topics.items():
                self._client.subscribe(req_topic, qos=1)
                self._log('Subscribed ext: {}', req_topic)
            self._connected = True
            self._last_hb   = _ticks()
            self._log('Connected  broker: {}  clientId: {}', self.broker_uri(), self._client_id)
            self.send_hello()
            gc.collect()
            return True
        except Exception as e:
            self._log('MQTT connect failed: {}', e)
            self._connected = False
            return False

    def _dispatch(self, topic, payload_str):
        # ── Extension request ────────────────────────────────────────────────
        for ext_type, req_topic in self._ext_req_topics.items():
            if topic == req_topic:
                cb = self._extensions.get(ext_type)
                if not cb:
                    return
                try:
                    doc    = json.loads(payload_str)
                    req_id = doc.get('id', '')
                    op     = doc.get('op', '')
                    params = {k: v for k, v in doc.items() if k not in ('id', 'op')}
                    self._log('EXT [{}] op={} id={}', ext_type, op, req_id)
                    cb(req_id, op, params)
                except Exception as e:
                    self._log('ext dispatch error: {}', e)
                return

        # ── Command ──────────────────────────────────────────────────────────
        if topic != self._t_command or not self._cmd_cb:
            return
        try:
            doc      = json.loads(payload_str)
            cmd_id   = doc.get('id',      '')
            cmd_name = doc.get('name',    '')
            cmd_pl   = doc.get('payload', {})
            self._log('CMD [{}] id={}', cmd_name, cmd_id)
            self._cmd_cb(cmd_id, cmd_name, cmd_pl)
        except Exception as e:
            self._log('dispatch error: {}', e)

    def _log(self, fmt, *args):
        if self._debug:
            try:
                print('[MinisIoT] ' + fmt.format(*args))
            except Exception:
                pass
