"""
minis_entities.py — IotEntity helpers for uMinisLib

Provides Python entity classes mirroring the TypeScript IotEntity types
from MyCastle (packages/core/src/models/IotModels.ts).

Entities are registered via MinisIoT.add_entity() and announced in the
hello message so MyCastle can render the correct UI controls. Writable
entity types (Switch, Number, Button, Select) handle incoming commands
automatically and auto-acknowledge them — no manual ack_command() needed.

Entity types
------------
  SensorEntity        — read-only numeric sensor
  BinarySensorEntity  — read-only boolean indicator (on/off)
  SwitchEntity        — writable boolean toggle
  NumberEntity        — writable numeric value with range constraints
  ButtonEntity        — writable momentary trigger
  SelectEntity        — writable enum selector

Reporting values
----------------
Sensor and BinarySensor values are reported via send_telemetry() using the
entity id as the metric key:

    temp_entity = SensorEntity('temperature', 'Temperature', '°C')
    minis.add_entity(temp_entity)
    ...
    minis.send_telemetry([('temperature', 22.5)])  # key = entity id

Writing values
--------------
Writable entity callbacks are invoked automatically when MyCastle sends a
command whose name matches the entity id. The library auto-acks the command.

    def on_relay(state):
        relay.value(1 if state else 0)

    relay_entity = SwitchEntity('relay', 'Relay', callback=on_relay)
    minis.add_entity(relay_entity)
"""


class IotEntity:
    """Base class for all IoT entities. Not used directly."""

    def __init__(self, entity_id, entity_type, name, icon=None, device_class=None):
        self.id           = entity_id
        self.type         = entity_type
        self.name         = name
        self.icon         = icon
        self.device_class = device_class

    def to_dict(self):
        d = {'id': self.id, 'type': self.type, 'name': self.name}
        if self.icon:
            d['icon'] = self.icon
        if self.device_class:
            d['deviceClass'] = self.device_class
        return d

    def handle_command(self, payload):
        """Called by MinisIoT when a command matching this entity id arrives.
        Writable subclasses override this to dispatch their callback."""
        pass


# ─── Read-only entities ───────────────────────────────────────────────────────

class SensorEntity(IotEntity):
    """
    Read-only numeric sensor. Report value via send_telemetry().

    :param entity_id:    Unique id — must match the telemetry metric key.
    :param name:         Human-readable label shown in MyCastle UI.
    :param unit:         Unit string, e.g. '°C', '%', 'hPa'.
    :param icon:         Optional icon name (MyCastle/Material icon).
    :param device_class: Optional HA-style device class, e.g. 'temperature'.
    """

    def __init__(self, entity_id, name, unit='', icon=None, device_class=None):
        super().__init__(entity_id, 'sensor', name, icon, device_class)
        self.unit = unit

    def to_dict(self):
        d = super().to_dict()
        d['unit'] = self.unit
        return d


class BinarySensorEntity(IotEntity):
    """
    Read-only boolean sensor. Report value via send_telemetry().

    :param entity_id:    Unique id — must match the telemetry metric key.
    :param name:         Human-readable label.
    :param on_label:     Text shown when value is True (default: 'On').
    :param off_label:    Text shown when value is False (default: 'Off').
    :param icon:         Optional icon name.
    :param device_class: Optional HA-style device class, e.g. 'motion'.
    """

    def __init__(self, entity_id, name, on_label=None, off_label=None,
                 icon=None, device_class=None):
        super().__init__(entity_id, 'binary_sensor', name, icon, device_class)
        self.on_label  = on_label
        self.off_label = off_label

    def to_dict(self):
        d = super().to_dict()
        if self.on_label:
            d['onLabel'] = self.on_label
        if self.off_label:
            d['offLabel'] = self.off_label
        return d


# ─── Writable entities ────────────────────────────────────────────────────────

class SwitchEntity(IotEntity):
    """
    Writable boolean toggle (e.g. relay, LED strip).

    Command payload: {'state': true | false}
    Callback signature: callback(state: bool)

    :param entity_id: Unique id (also used as command name).
    :param name:      Human-readable label.
    :param callback:  Called with the new boolean state on every command.
    :param icon:      Optional icon name.
    :param device_class: Optional HA-style device class, e.g. 'outlet'.
    """

    def __init__(self, entity_id, name, callback=None, icon=None, device_class=None):
        super().__init__(entity_id, 'switch', name, icon, device_class)
        self._cb = callback

    def handle_command(self, payload):
        if self._cb is not None and 'state' in payload:
            self._cb(bool(payload['state']))


class NumberEntity(IotEntity):
    """
    Writable numeric value with min/max/step constraints (e.g. target temperature).

    Command payload: {'value': <number>}
    Callback signature: callback(value: float)

    :param entity_id: Unique id (also used as command name).
    :param name:      Human-readable label.
    :param min_val:   Minimum allowed value.
    :param max_val:   Maximum allowed value.
    :param step:      Increment step (e.g. 0.5, 1).
    :param unit:      Optional unit string, e.g. '°C'.
    :param callback:  Called with the new numeric value on every command.
    :param icon:      Optional icon name.
    :param device_class: Optional HA-style device class, e.g. 'temperature'.
    """

    def __init__(self, entity_id, name, min_val, max_val, step,
                 unit=None, callback=None, icon=None, device_class=None):
        super().__init__(entity_id, 'number', name, icon, device_class)
        self.min  = min_val
        self.max  = max_val
        self.step = step
        self.unit = unit
        self._cb  = callback

    def to_dict(self):
        d = super().to_dict()
        d['min']  = self.min
        d['max']  = self.max
        d['step'] = self.step
        if self.unit is not None:
            d['unit'] = self.unit
        return d

    def handle_command(self, payload):
        if self._cb is not None and 'value' in payload:
            self._cb(payload['value'])


class ButtonEntity(IotEntity):
    """
    Writable momentary button (e.g. reboot, calibrate, force refresh).

    Command payload: {} (empty)
    Callback signature: callback()

    :param entity_id: Unique id (also used as command name).
    :param name:      Human-readable label.
    :param callback:  Called with no arguments on every button press.
    :param icon:      Optional icon name.
    :param device_class: Optional HA-style device class, e.g. 'restart'.
    """

    def __init__(self, entity_id, name, callback=None, icon=None, device_class=None):
        super().__init__(entity_id, 'button', name, icon, device_class)
        self._cb = callback

    def handle_command(self, payload):
        if self._cb is not None:
            self._cb()


class SelectEntity(IotEntity):
    """
    Writable enum selector (e.g. operating mode, fan speed preset).

    Command payload: {'value': '<one of options>'}
    Callback signature: callback(value: str)
    Unknown values are silently ignored.

    :param entity_id: Unique id (also used as command name).
    :param name:      Human-readable label.
    :param options:   List of allowed string values.
    :param callback:  Called with the selected option string.
    :param icon:      Optional icon name.
    :param device_class: Optional HA-style device class.
    """

    def __init__(self, entity_id, name, options, callback=None,
                 icon=None, device_class=None):
        super().__init__(entity_id, 'select', name, icon, device_class)
        self.options = list(options)
        self._cb     = callback

    def to_dict(self):
        d = super().to_dict()
        d['options'] = self.options
        return d

    def handle_command(self, payload):
        if self._cb is not None and 'value' in payload:
            val = payload['value']
            if val in self.options:
                self._cb(val)
