# uMinisLib

MicroPython library for IoT devices integrating with the **MyCastle IoT platform**. Provides MQTT-based communication, telemetry reporting, command handling, remote filesystem access, and virtual input devices.

## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [Core: MinisIoT](#core-minisiot)
- [Entities](#entities)
- [Extension: Vfs](#extension-vfs)
- [Extension: VirtualKeyboard](#extension-virtualkeyboard)
- [Extension: VirtualMouse](#extension-virtualmouse)
- [MQTT Topics](#mqtt-topics)
- [Examples](#examples)

---

## Installation

Copy the required files to the device root (or upload via the MyCastle web UI):

| File | Required | Purpose |
| ---- | -------- | ------- |
| `minis_iot.py` | Yes | Core MQTT client |
| `minis_vfs.py` | Optional | Remote filesystem access |
| `vkbd.py` | Optional | Virtual keyboard input |
| `vmouse.py` | Optional | Virtual mouse input |

**Dependency:** `umqtt.simple` must be available on the device (pre-installed on most ESP32 firmware builds).

When deploying through MyCastle, a `MinisConfig.py` file is automatically injected with device-specific credentials:

```python
MINIS_DEVICE_NAME = "my-device-id"
MINIS_WIFI_SSID = "MyNetwork"
MINIS_WIFI_PASSWORD = "secret"
```

---

## Quick Start

```python
from minis_iot import MinisIoT
import time

minis = MinisIoT('192.168.0.207', 1884, 'my_user', 'my_device')
minis.set_wifi('MySSID', 'MyPassword')

if not minis.begin():
    print('Connection failed, will retry in loop()')

while True:
    minis.loop()  # Must be called every iteration
    minis.send_telemetry([
        ('temperature', 22.5, '°C'),
        ('humidity', 60.0, '%'),
    ])
    time.sleep(10)
```

---

## Core: MinisIoT

`minis_iot.MinisIoT` — main entry point for all device communication.

### Constructor

```python
MinisIoT(host: str, port: int, user_id: str, device_id: str)
```

| Parameter | Type | Description |
| --------- | ---- | ----------- |
| `host` | `str` | MyCastle broker hostname or IP |
| `port` | `int` | MQTT TCP port (default: `1884`) |
| `user_id` | `str` | User ID in MyCastle |
| `device_id` | `str` | Device identifier (used in MQTT topics) |

### Configuration

Call these methods **before** `begin()`.

#### `set_wifi(ssid, password)`

```python
minis.set_wifi('MySSID', 'MyPassword')
```

Configures WiFi credentials. When set, `begin()` connects to WiFi first.

#### `set_heartbeat_interval(seconds)`

```python
minis.set_heartbeat_interval(30)  # Every 30 seconds
minis.set_heartbeat_interval(0)   # Disable auto-heartbeat
```

Default: `60` seconds.

#### `set_debug(enabled)`

```python
minis.set_debug(True)
```

Enables verbose REPL logging. Default: `False`.

#### `on_command(callback)`

```python
def handle_command(cmd_id: str, name: str, payload: dict) -> None:
    ...

minis.on_command(handle_command)
```

Registers a callback for incoming commands. See [Command Handling](#command-handling).

#### `add_extension(ext_type, callback)`

```python
minis.add_extension('my_ext', my_callback)
```

Registers a custom extension handler. Extensions like `Vfs`, `VirtualKeyboard`, and `VirtualMouse` call this internally.

---

### Lifecycle

#### `begin(timeout_ms=15000) -> bool`

Connects to WiFi (if configured) and then to the MQTT broker. Returns `True` on success. On failure, `loop()` will automatically retry.

```python
minis.begin()
minis.begin(timeout_ms=30000)  # 30-second timeout
```

#### `loop() -> None`

Must be called on every main loop iteration. Handles:

- Incoming MQTT message dispatch
- Automatic heartbeat publishing
- MQTT/WiFi reconnection on disconnect

```python
while True:
    minis.loop()
    # your logic here
```

---

### Telemetry

#### `send_telemetry(metrics, battery=None) -> bool`

Publishes sensor readings to MyCastle.

```python
# (key, value) tuples
minis.send_telemetry([('temperature', 22.5)])

# (key, value, unit) tuples
minis.send_telemetry([('temperature', 22.5, '°C'), ('humidity', 60, '%')])

# With battery voltage (in volts)
minis.send_telemetry([('temperature', 22.5)], battery=3.7)
```

Returns `True` if published successfully.

---

### Commands

#### Command Handling

Commands arrive from the MyCastle dashboard. Register a handler with `on_command()`:

```python
def handle_command(cmd_id: str, name: str, payload: dict) -> None:
    if name == 'set_relay':
        state = payload.get('state', False)
        relay.value(1 if state else 0)
        minis.ack_command(cmd_id, True)
    else:
        minis.ack_command(cmd_id, False, 'Unknown command')
```

#### `ack_command(cmd_id, success=True, reason=None) -> bool`

Acknowledges a command after handling it.

```python
minis.ack_command(cmd_id, True)
minis.ack_command(cmd_id, False, 'GPIO error')
```

---

### Status

#### `is_connected() -> bool`

```python
if minis.is_connected():
    minis.send_telemetry(...)
```

#### `broker_uri() -> str`

Returns `"tcp://{host}:{port}"`.

#### `client_id() -> str`

Returns the MQTT client ID (`"{user_id}_{device_id}"`).

---

### Internal Methods

| Method | Description |
| ------ | ----------- |
| `send_hello() -> bool` | Publishes device presence announcement |
| `send_heartbeat(battery=None) -> bool` | Publishes a heartbeat (called automatically) |
| `ext_respond(ext_type, req_id, ok, data=None, error=None) -> bool` | Sends a response for a custom extension request |

---

## Entities

`minis_entities` — Python entity classes mirroring the TypeScript `IotEntity` types from MyCastle. Entities are registered via `add_entity()` and announced in the hello message so MyCastle renders the correct UI controls.

| File | Import |
| ---- | ------ |
| `minis_entities.py` | `from minis_entities import SensorEntity, SwitchEntity, ...` |

### Entity types

| Class | Type | Direction | Description |
| ----- | ---- | --------- | ----------- |
| `SensorEntity` | `sensor` | Device → Server | Read-only numeric sensor |
| `BinarySensorEntity` | `binary_sensor` | Device → Server | Read-only boolean indicator |
| `SwitchEntity` | `switch` | Server → Device | Writable boolean toggle |
| `NumberEntity` | `number` | Server → Device | Writable numeric value with range |
| `ButtonEntity` | `button` | Server → Device | Writable momentary trigger |
| `SelectEntity` | `select` | Server → Device | Writable enum selector |

### Registering entities

```python
minis.add_entity(entity)
```

Call before `begin()`. All registered entities are included in the hello message sent on every MQTT connect.

### Read-only entities

Values are reported via `send_telemetry()` using the entity `id` as the metric key:

```python
e_temp = SensorEntity('temperature', 'Temperature', unit='°C')
minis.add_entity(e_temp)
...
minis.send_telemetry([('temperature', 22.5)])  # key = entity id
```

### Writable entities and commands

When a command arrives whose `name` matches a registered entity id, the library:

1. Calls `entity.handle_command(payload)` which invokes the entity callback
2. Auto-acknowledges the command (no `ack_command()` needed)

Commands whose name does **not** match any entity are passed to the `on_command` callback as usual.

### `SensorEntity`

```python
SensorEntity(entity_id, name, unit='', icon=None, device_class=None)
```

| Parameter | Type | Description |
| --------- | ---- | ----------- |
| `entity_id` | `str` | Unique id — must match the telemetry metric key |
| `name` | `str` | Human-readable label |
| `unit` | `str` | Unit string, e.g. `'°C'`, `'%'`, `'hPa'` |
| `icon` | `str` | Optional icon name |
| `device_class` | `str` | Optional HA-style class, e.g. `'temperature'` |

### `BinarySensorEntity`

```python
BinarySensorEntity(entity_id, name, on_label=None, off_label=None, icon=None, device_class=None)
```

| Parameter | Type | Description |
| --------- | ---- | ----------- |
| `entity_id` | `str` | Unique id — must match the telemetry metric key |
| `name` | `str` | Human-readable label |
| `on_label` | `str` | Text shown when `True` (default: `'On'`) |
| `off_label` | `str` | Text shown when `False` (default: `'Off'`) |

### `SwitchEntity`

```python
SwitchEntity(entity_id, name, callback=None, icon=None, device_class=None)
```

Command payload: `{"state": true | false}`. Callback: `callback(state: bool)`.

### `NumberEntity`

```python
NumberEntity(entity_id, name, min_val, max_val, step, unit=None, callback=None, icon=None, device_class=None)
```

Command payload: `{"value": <number>}`. Callback: `callback(value: float)`.

| Parameter | Type | Description |
| --------- | ---- | ----------- |
| `min_val` | `int/float` | Minimum allowed value |
| `max_val` | `int/float` | Maximum allowed value |
| `step` | `int/float` | Increment step, e.g. `0.5` or `1` |
| `unit` | `str` | Optional unit string |

### `ButtonEntity`

```python
ButtonEntity(entity_id, name, callback=None, icon=None, device_class=None)
```

Command payload: `{}` (empty). Callback: `callback()`.

### `SelectEntity`

```python
SelectEntity(entity_id, name, options, callback=None, icon=None, device_class=None)
```

Command payload: `{"value": "<option>"}`. Callback: `callback(value: str)`. Unknown values are silently ignored.

| Parameter | Type | Description |
| --------- | ---- | ----------- |
| `options` | `list[str]` | Allowed option values |

### Entities usage example

```python
from minis_iot      import MinisIoT
from minis_entities import SensorEntity, BinarySensorEntity, SwitchEntity
from minis_entities import NumberEntity, ButtonEntity, SelectEntity

target_temp = 21.0
heater_on   = False
mode        = 'heat'

def on_switch(state):
    global heater_on
    heater_on = state

def on_target(value):
    global target_temp
    target_temp = float(value)

def on_mode(value):
    global mode
    mode = value

def on_refresh():
    print('Force refresh triggered')

minis = MinisIoT(host, port, user_id, device_id)

minis.add_entity(SensorEntity('temperature', 'Temperature', unit='°C'))
minis.add_entity(BinarySensorEntity('heater_active', 'Heater Active',
                                    on_label='Heating', off_label='Idle'))
minis.add_entity(SwitchEntity('heater_switch', 'Heater', callback=on_switch))
minis.add_entity(NumberEntity('target_temp', 'Target Temp',
                               min_val=5, max_val=35, step=0.5,
                               unit='°C', callback=on_target))
minis.add_entity(ButtonEntity('force_refresh', 'Refresh', callback=on_refresh))
minis.add_entity(SelectEntity('mode', 'Mode',
                               options=['heat', 'cool', 'fan', 'off'],
                               callback=on_mode))
minis.begin()

while True:
    minis.loop()
    temp = read_temperature()
    minis.send_telemetry([
        ('temperature',   temp),
        ('heater_active', heater_on and mode == 'heat' and temp < target_temp),
    ])
    time.sleep(10)
```

---

## Extension: Vfs

`minis_vfs.Vfs` — exposes the device filesystem over MQTT for remote management from the MyCastle web UI.

### Vfs Constructor

```python
Vfs(minis: MinisIoT, root: str = '/')
```

| Parameter | Type | Description |
| --------- | ---- | ----------- |
| `minis` | `MinisIoT` | MinisIoT instance to register with |
| `root` | `str` | Root path exposed to server. Requests outside this path are rejected. Default: `'/'` |

Auto-registers as the `'vfs'` extension. No further setup required.

### Vfs Usage

```python
from minis_iot import MinisIoT
from minis_vfs import Vfs

minis = MinisIoT(host, port, user_id, device_id)
vfs = Vfs(minis, root='/')  # Expose full filesystem
minis.begin()

while True:
    minis.loop()
```

### Vfs Operations

| Operation | Description |
| --------- | ----------- |
| `stat` | Get file/directory metadata (type, size, timestamps) |
| `readdir` | List directory contents |
| `readfile` | Download file content (base64-encoded) |
| `writefile` | Upload and write file content |
| `delete` | Delete file or directory (supports recursive) |
| `rename` | Rename/move file or directory |
| `mkdir` | Create directory |

---

## Extension: VirtualKeyboard

`vkbd.VirtualKeyboard` — receives keyboard input from MyCastle and maintains a state that device code can poll. Useful for device-side GUI automation or input-driven logic.

### VirtualKeyboard Constructor

```python
VirtualKeyboard(minis: MinisIoT)
```

Auto-registers as the `'vkbd'` extension.

### VirtualKeyboard State Properties

| Property | Type | Description |
| -------- | ---- | ----------- |
| `pressed_keys` | `set[str]` | Set of currently held key names |
| `modifiers` | `int` | Bitmask of active modifier keys |
| `last_text` | `str` | Text from the last `type_text` operation |

### Modifier Constants

```python
MOD_LCTRL  = 0x01
MOD_LSHIFT = 0x02
MOD_LALT   = 0x04
MOD_LGUI   = 0x08
MOD_RCTRL  = 0x10
MOD_RSHIFT = 0x20
MOD_RALT   = 0x40
MOD_RGUI   = 0x80
```

### VirtualKeyboard Usage

```python
from vkbd import VirtualKeyboard, MOD_LCTRL

kbd = VirtualKeyboard(minis)
minis.begin()

while True:
    minis.loop()

    if 'enter' in kbd.pressed_keys:
        print('Enter pressed')

    if kbd.modifiers & MOD_LCTRL and 'c' in kbd.pressed_keys:
        print('Ctrl+C held')

    if kbd.last_text:
        print(f'User typed: {kbd.last_text}')
        kbd.last_text = ''  # Clear after reading
```

### Supported Keys

- **Characters:** `'a'`–`'z'`, `'0'`–`'9'`, symbols
- **Special keys:** `'enter'`, `'escape'`, `'backspace'`, `'tab'`, `'space'`, `'delete'`, `'home'`, `'end'`, `'pageup'`, `'pagedown'`, `'up'`, `'down'`, `'left'`, `'right'`
- **Function keys:** `'f1'`–`'f12'`

### Supported Modifier Names

`'ctrl'` / `'lctrl'` / `'rctrl'`, `'shift'` / `'lshift'` / `'rshift'`, `'alt'` / `'lalt'` / `'ralt'`, `'win'` / `'gui'` / `'cmd'`

---

## Extension: VirtualMouse

`vmouse.VirtualMouse` — receives mouse input from MyCastle and maintains cursor/button state that device code can poll.

### VirtualMouse Constructor

```python
VirtualMouse(minis: MinisIoT)
```

Auto-registers as the `'vmouse'` extension.

### VirtualMouse State Properties

| Property | Type | Description |
| -------- | ---- | ----------- |
| `x` | `int` | Cursor X position |
| `y` | `int` | Cursor Y position |
| `buttons` | `int` | Bitmask of currently held buttons |
| `last_click` | `str \| None` | Button name of the most recent click |
| `last_scroll_dy` | `int` | Delta from the most recent scroll event |

### Button Constants

```python
BTN_LEFT   = 0x01
BTN_RIGHT  = 0x02
BTN_MIDDLE = 0x04
```

### VirtualMouse Usage

```python
from vmouse import VirtualMouse, BTN_LEFT

mouse = VirtualMouse(minis)
minis.begin()

while True:
    minis.loop()

    print(f'Cursor at ({mouse.x}, {mouse.y})')

    if mouse.buttons & BTN_LEFT:
        print('Left button held')

    if mouse.last_click == 'left':
        print('Left click registered')
        mouse.last_click = None  # Clear after reading
```

### VirtualMouse Operations

| Operation | Payload | Description |
| --------- | ------- | ----------- |
| `move` | `{x, y}` | Set absolute cursor position |
| `move_rel` | `{dx, dy}` | Move cursor relative to current position |
| `click` | `{button?, x?, y?}` | Click button (and optionally move) |
| `double_click` | `{button?, x?, y?}` | Double-click |
| `press` | `{button?}` | Hold button down |
| `release` | `{button?}` | Release button |
| `scroll` | `{dy}` | Record scroll delta |
| `drag` | `{x1, y1, x2, y2, button?}` | Press, move, release sequence |
| `get_pos` | `{}` | Responds with `{x, y}` |

Button name values: `'left'` (default), `'right'`, `'middle'`.

---

## MQTT Topics

All topics follow the pattern `minis/{user_id}/{device_id}/{suffix}`:

| Topic suffix | Direction | Description |
| ------------ | --------- | ----------- |
| `telemetry` | Device → Server | Sensor readings |
| `hello` | Device → Server | Presence announcement on connect |
| `heartbeat` | Device → Server | Periodic keep-alive |
| `command` | Server → Device | Incoming commands |
| `command/ack` | Device → Server | Command acknowledgment |
| `ext/{type}/req` | Server → Device | Extension request (e.g. VFS operation) |
| `ext/{type}/res` | Device → Server | Extension response |

Extension types: `'vfs'`, `'vkbd'`, `'vmouse'`, or any custom type added via `add_extension()`.

---

## Examples

### Basic Sensor

```python
from minis_iot import MinisIoT
import time

try:
    from MinisConfig import MINIS_DEVICE_NAME, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD
except ImportError:
    MINIS_DEVICE_NAME = 'dev-board'
    MINIS_WIFI_SSID = 'MySSID'
    MINIS_WIFI_PASSWORD = 'MyPassword'

minis = MinisIoT('192.168.0.207', 1884, 'my_user', MINIS_DEVICE_NAME)
minis.set_wifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD)
minis.begin()

while True:
    minis.loop()
    minis.send_telemetry([
        ('temperature', 22.5, '°C'),
        ('humidity', 60.0, '%'),
    ])
    time.sleep(10)
```

### Sensor with Command Handling

```python
from minis_iot import MinisIoT
from machine import Pin
import time

relay = Pin(2, Pin.OUT)

def handle_command(cmd_id, name, payload):
    if name == 'set_relay':
        state = payload.get('state', False)
        relay.value(1 if state else 0)
        minis.ack_command(cmd_id, True)
    else:
        minis.ack_command(cmd_id, False, f'Unknown command: {name}')

minis = MinisIoT('192.168.0.207', 1884, 'my_user', 'my_device')
minis.set_wifi('MySSID', 'MyPassword')
minis.on_command(handle_command)
minis.begin()

while True:
    minis.loop()
    temp = read_temp()  # your sensor function
    minis.send_telemetry([
        ('temperature', temp, '°C'),
        ('relay', bool(relay.value())),
    ])
    time.sleep(5)
```

### Full Setup with All Extensions

```python
from minis_iot import MinisIoT
from minis_vfs import Vfs
from vkbd import VirtualKeyboard
from vmouse import VirtualMouse

minis = MinisIoT('192.168.0.207', 1884, 'my_user', 'my_device')
minis.set_wifi('MySSID', 'MyPassword')

vfs = Vfs(minis, root='/')
kbd = VirtualKeyboard(minis)
mouse = VirtualMouse(minis)

minis.begin()

while True:
    minis.loop()

    # Read keyboard state
    if 'space' in kbd.pressed_keys:
        print('Space pressed')

    # Read mouse state
    if mouse.last_click:
        print(f'{mouse.last_click} click at ({mouse.x}, {mouse.y})')
        mouse.last_click = None
```
