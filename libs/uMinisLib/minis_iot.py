"""
minis_iot.py — MicroPython MinisIoT library for MyCastle IoT platform

Transport: MQTT 3.1.1 over WebSocket  ws://{host}:{port}/mqtt
No external dependencies — uses only MicroPython built-in modules.

Quick start::

    from minis_iot import MinisIoT

    minis = MinisIoT('192.168.1.100', 1902, 'marcin', 'my-device-sn')
    minis.set_wifi('MySSID', 'MyPassword')
    minis.begin()

    while True:
        minis.loop()
        minis.send_telemetry([('temperature', 22.5, '°C')])
        time.sleep(10)

When deployed via MyCastle a MinisConfig.py is injected with:
    MINIS_DEVICE_SN, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD, MINIS_CONFIG
"""

import network
import socket
import struct
import time
import json
import os
import gc

_ticks = time.ticks_ms
_diff  = time.ticks_diff


# ─────────────────────────────────────────────────────────────────────────────
# _WsMqttClient — MQTT 3.1.1 over WebSocket
# ─────────────────────────────────────────────────────────────────────────────

class _WsMqttClient:
    """
    Minimal MQTT 3.1.1 client over WebSocket binary frames.

    Implements: CONNECT, PUBLISH (QoS 0/1), SUBSCRIBE, PING, DISCONNECT.
    Server → client frames are not masked (per RFC 6455).
    Client → server frames are masked with os.urandom(4).
    """

    # MQTT packet type nibbles (high 4 bits of first byte)
    _CONNECT    = 0x10
    _CONNACK    = 0x20
    _PUBLISH    = 0x30
    _PUBACK     = 0x40
    _SUBSCRIBE  = 0x82  # fixed header: type=8, flags=0b0010
    _SUBACK     = 0x90
    _PINGREQ    = 0xC0
    _PINGRESP   = 0xD0
    _DISCONNECT = 0xE0

    def __init__(self, host, port, client_id, keepalive=60, debug=False):
        self._host      = host
        self._port      = port
        self._cid       = client_id
        self._keepalive = keepalive
        self._debug     = debug
        self._sock      = None
        self._buf       = bytearray()
        self._pkt_id    = 0

    # ── Connection ────────────────────────────────────────────────────────────

    def connect(self, timeout_ms=10000):
        addr = socket.getaddrinfo(self._host, self._port)[0][-1]
        s = socket.socket()
        s.settimeout(max(2, timeout_ms // 1000))
        s.connect(addr)
        self._sock = s
        self._ws_handshake()
        self._sock.settimeout(0.05)   # nearly non-blocking for loop() reads
        self._mqtt_connect(timeout_ms)

    def disconnect(self):
        try:
            self._ws_send(bytes([self._DISCONNECT, 0]))
        except Exception:
            pass
        self._close()

    # ── MQTT operations ───────────────────────────────────────────────────────

    def publish(self, topic, payload, qos=0):
        t = topic.encode()   if isinstance(topic,   str) else topic
        p = payload.encode() if isinstance(payload, str) else payload
        pkt = bytearray([self._PUBLISH | (qos << 1)])
        rem = 2 + len(t) + len(p) + (2 if qos else 0)
        self._varlen(pkt, rem)
        pkt += struct.pack('!H', len(t)) + t
        if qos:
            self._pkt_id = (self._pkt_id + 1) & 0xFFFF
            pkt += struct.pack('!H', self._pkt_id)
        pkt += p
        self._ws_send(bytes(pkt))
        self._log('PUB [{}]', topic)

    def subscribe(self, topic, qos=1):
        t = topic.encode() if isinstance(topic, str) else topic
        self._pkt_id = (self._pkt_id + 1) & 0xFFFF
        pkt = bytearray([self._SUBSCRIBE])
        self._varlen(pkt, 2 + 2 + len(t) + 1)
        pkt += struct.pack('!H', self._pkt_id)
        pkt += struct.pack('!H', len(t)) + t
        pkt.append(qos)
        self._ws_send(bytes(pkt))

    def ping(self):
        self._ws_send(bytes([self._PINGREQ, 0]))

    def check_msg(self):
        """Non-blocking. Returns (topic:str, payload:str) or None."""
        try:
            frame = self._ws_recv_frame()
            if frame:
                self._buf.extend(frame)
        except OSError:
            pass
        return self._parse_next()

    # ── WebSocket layer ───────────────────────────────────────────────────────

    def _ws_handshake(self):
        key = 'bWluaXNpb3Q9PT09'   # static base64 key (server doesn't validate it)
        req = (
            'GET /mqtt HTTP/1.1\r\n'
            'Host: {}:{}\r\n'
            'Upgrade: websocket\r\n'
            'Connection: Upgrade\r\n'
            'Sec-WebSocket-Key: {}\r\n'
            'Sec-WebSocket-Version: 13\r\n'
            'Sec-WebSocket-Protocol: mqtt\r\n'
            '\r\n'
        ).format(self._host, self._port, key)
        self._sock.sendall(req.encode())
        resp = b''
        while b'\r\n\r\n' not in resp:
            chunk = self._sock.recv(512)
            if not chunk:
                raise OSError('WS handshake: connection closed')
            resp += chunk
        if b'101' not in resp:
            raise OSError('WS handshake failed: ' + repr(resp[:80]))

    def _ws_send(self, data):
        n = len(data)
        hdr = bytearray([0x82])   # FIN=1, opcode=binary(2)
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr.append(0x80 | 126)
            hdr += struct.pack('>H', n)
        else:
            hdr.append(0x80 | 127)
            hdr += struct.pack('>Q', n)
        mask = os.urandom(4)
        hdr += mask
        masked = bytearray(n)
        for i in range(n):
            masked[i] = data[i] ^ mask[i & 3]
        self._sock.sendall(bytes(hdr) + bytes(masked))

    def _ws_recv_frame(self):
        """Read one WebSocket frame payload (server → client, no mask). Non-blocking."""
        if self._sock is None:
            return None
        try:
            hdr = self._sock.recv(2)
        except OSError:
            return None
        if not hdr or len(hdr) < 2:
            return None
        masked = (hdr[1] & 0x80) != 0
        n = hdr[1] & 0x7F
        if n == 126:
            n = struct.unpack('>H', self._recv_exact(2))[0]
        elif n == 127:
            n = struct.unpack('>Q', self._recv_exact(8))[0]
        mkey = self._recv_exact(4) if masked else None
        payload = self._recv_exact(n)
        if mkey:
            payload = bytearray(payload)
            for i in range(n):
                payload[i] ^= mkey[i & 3]
            payload = bytes(payload)
        return payload

    # ── MQTT framing ──────────────────────────────────────────────────────────

    def _mqtt_connect(self, timeout_ms):
        cid  = self._cid.encode() if isinstance(self._cid, str) else self._cid
        proto = b'MQTT'
        # Variable header: protocol name, level=4, flags=CLEAN(2), keepalive
        vhdr = struct.pack('!H', len(proto)) + proto + bytes([4, 2]) + struct.pack('!H', self._keepalive)
        pl   = struct.pack('!H', len(cid)) + cid
        pkt  = bytearray([self._CONNECT])
        self._varlen(pkt, len(vhdr) + len(pl))
        pkt += vhdr + pl
        self._ws_send(bytes(pkt))
        # Wait for CONNACK
        deadline = _ticks() + timeout_ms
        while _diff(deadline, _ticks()) > 0:
            try:
                frame = self._ws_recv_frame()
            except OSError:
                frame = None
            if frame and len(frame) >= 4 and (frame[0] & 0xF0) == self._CONNACK:
                rc = frame[3]
                if rc != 0:
                    raise OSError('CONNACK error code: {}'.format(rc))
                return
            time.sleep_ms(50)
        raise OSError('MQTT connect timeout')

    def _parse_next(self):
        """Parse one MQTT packet from _buf. Returns (topic, payload) for PUBLISH or None."""
        if len(self._buf) < 2:
            return None
        first = self._buf[0]
        cmd   = first & 0xF0
        flags = first & 0x0F
        # Decode variable-length remaining-length field
        mult = 1
        rem  = 0
        i    = 1
        while True:
            if i >= len(self._buf):
                return None   # need more data
            b = self._buf[i]; i += 1
            rem += (b & 0x7F) * mult
            if not (b & 0x80):
                break
            mult <<= 7
            if mult > 0x200000:
                del self._buf[:]
                return None   # malformed
        total = i + rem
        if total > len(self._buf):
            return None       # incomplete packet
        pkt = bytes(self._buf[i:total])
        del self._buf[:total]
        if cmd == 0x30:       # PUBLISH
            qos    = (flags >> 1) & 0x03
            tlen   = struct.unpack('!H', pkt[:2])[0]
            topic  = pkt[2:2 + tlen].decode()
            offset = 2 + tlen + (2 if qos else 0)
            payload = pkt[offset:].decode()
            return (topic, payload)
        # PINGRESP / SUBACK / PUBACK — consume silently
        return None

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _recv_exact(self, n):
        buf = b''
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise OSError('Connection closed')
            buf += chunk
        return buf

    def _varlen(self, buf, n):
        """Encode variable-length integer into buf (in-place)."""
        while True:
            b = n & 0x7F
            n >>= 7
            if n:
                b |= 0x80
            buf.append(b)
            if not n:
                break

    def _close(self):
        try:
            self._sock.close()
        except Exception:
            pass
        self._sock = None

    def _log(self, fmt, *args):
        if self._debug:
            try:
                print('[uMinis] ' + fmt.format(*args))
            except Exception:
                pass


# ─────────────────────────────────────────────────────────────────────────────
# MinisIoT — public API (mirrors the Arduino MinisIoT library)
# ─────────────────────────────────────────────────────────────────────────────

class MinisIoT:
    """
    MicroPython client for the MyCastle IoT platform.

    API mirrors the Arduino MinisIoT C++ library.

    :param host:      MyCastle hostname or IP  (e.g. '192.168.1.100')
    :param port:      WebSocket MQTT port      (default: 1902)
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
        self._t_heartbeat = 'minis/{}/{}/heartbeat'.format(user_id, device_id)
        self._t_command   = 'minis/{}/{}/command'.format(user_id, device_id)
        self._t_cmd_ack   = 'minis/{}/{}/command/ack'.format(user_id, device_id)
        self._client_id   = 'minis-{}'.format(device_id)

    # ── Configuration (call before begin()) ───────────────────────────────────

    def set_wifi(self, ssid, password):
        """Set WiFi credentials. If not called, WiFi must be already connected."""
        self._ssid     = ssid
        self._password = password

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

    # ── Heartbeat ─────────────────────────────────────────────────────────────

    def send_heartbeat(self, battery=None):
        """
        Manually send a heartbeat to keep the device ONLINE.
        Not needed if send_telemetry() is called at least every heartbeat_interval seconds.

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
        """Full WebSocket broker URI."""
        return 'ws://{}:{}/mqtt'.format(self._host, self._port)

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
            self._client = _WsMqttClient(
                self._host, self._port, self._client_id,
                keepalive=self._hb_sec or 60,
                debug=self._debug,
            )
            self._client.connect(timeout_ms)
            if self._cmd_cb:
                self._client.subscribe(self._t_command, qos=1)
                self._log('Subscribed: {}', self._t_command)
            self._connected = True
            self._last_hb   = _ticks()
            self._log('Connected  broker: {}  clientId: {}', self.broker_uri(), self._client_id)
            gc.collect()
            return True
        except Exception as e:
            self._log('MQTT connect failed: {}', e)
            self._connected = False
            return False

    def _dispatch(self, topic, payload_str):
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
