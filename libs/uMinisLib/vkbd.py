"""
vkbd.py — VirtualKeyboard extension for MyCastle IoT (MicroPython)

Maintains an internal virtual keyboard state (pressed keys, modifiers).
No hardware output — state can be read by GUI automation code running
on the device.

Usage::

    from minis_iot import MinisIoT
    from vkbd import VirtualKeyboard

    minis = MinisIoT(host, port, user_id, device_id)
    kbd = VirtualKeyboard(minis)
    minis.begin()

    while True:
        minis.loop()
        # read state:
        # kbd.pressed_keys   — set of currently held key names
        # kbd.modifiers      — bitmask of active modifier bits
        # kbd.last_text      — last text typed via type_text
"""

# Modifier bitmask constants
MOD_LCTRL  = 0x01
MOD_LSHIFT = 0x02
MOD_LALT   = 0x04
MOD_LGUI   = 0x08
MOD_RCTRL  = 0x10
MOD_RSHIFT = 0x20
MOD_RALT   = 0x40
MOD_RGUI   = 0x80

_MODIFIER_MAP = {
    'ctrl':   MOD_LCTRL,  'lctrl':  MOD_LCTRL,  'rctrl':  MOD_RCTRL,
    'shift':  MOD_LSHIFT, 'lshift': MOD_LSHIFT,  'rshift': MOD_RSHIFT,
    'alt':    MOD_LALT,   'lalt':   MOD_LALT,    'ralt':   MOD_RALT,
    'win':    MOD_LGUI,   'gui':    MOD_LGUI,    'cmd':    MOD_LGUI,
}

_KNOWN_KEYS = {
    'enter', 'return', 'esc', 'escape', 'backspace', 'tab', 'space',
    'delete', 'home', 'end', 'pageup', 'pagedown',
    'up', 'down', 'left', 'right',
    'f1', 'f2', 'f3', 'f4', 'f5', 'f6',
    'f7', 'f8', 'f9', 'f10', 'f11', 'f12',
}


def _resolve_key(name: str) -> str:
    """Normalise a key name. Returns canonical name or raises ValueError."""
    n = name.lower()
    if len(n) == 1:  # single printable character
        return n
    if n in _KNOWN_KEYS:
        return n
    # allow 'return' as alias for 'enter'
    raise ValueError('Unknown key: {!r}'.format(name))


def _resolve_modifier(name: str) -> int:
    """Return modifier bitmask or raise ValueError."""
    mod = _MODIFIER_MAP.get(name.lower())
    if mod is None:
        raise ValueError('Unknown modifier: {!r}'.format(name))
    return mod


class VirtualKeyboard:
    """
    MyCastle VirtualKeyboard extension — internal virtual state only.

    Registers itself with the MinisIoT instance as the 'vkbd' extension.
    Tracks pressed keys and modifier state; no hardware output.

    Public state attributes (read by device application code):
      pressed_keys  set[str]   — canonical names of currently held keys
      modifiers     int        — bitmask of active modifier bits (MOD_* constants)
      last_text     str        — last string passed to type_text

    Supported ops (received from MyCastle server):
      key_press   { key, modifiers?: [str] }  — press + release a key
      key_down    { key }                      — hold key
      key_up      { key }                      — release key (or all if unknown)
      type_text   { text }                     — record typed text, update state
      hotkey      { keys: [str] }              — press combination, then release all
    """

    EXT_TYPE = 'vkbd'

    def __init__(self, minis):
        self._minis = minis
        self.pressed_keys: set = set()
        self.modifiers: int = 0
        self.last_text: str = ''
        minis.add_extension(self.EXT_TYPE, self._on_request)
        print('[vkbd] Virtual keyboard ready (internal state)')

    # ── Extension callback ────────────────────────────────────────────────────

    def _on_request(self, req_id: str, op: str, params: dict):
        try:
            self._dispatch(op, params)
            self._minis.ext_respond(self.EXT_TYPE, req_id, True)
        except Exception as e:
            self._minis.ext_respond(self.EXT_TYPE, req_id, False,
                                    error={'code': 'Error', 'message': str(e)})

    # ── Dispatch ──────────────────────────────────────────────────────────────

    def _dispatch(self, op: str, params: dict):
        if op == 'key_press':
            key = _resolve_key(params['key'])
            mods = params.get('modifiers') or []
            mod_byte = 0
            for m in mods:
                mod_byte |= _resolve_modifier(m)
            self.modifiers |= mod_byte
            self.pressed_keys.add(key)
            # simulate release
            self.pressed_keys.discard(key)
            self.modifiers &= ~mod_byte

        elif op == 'key_down':
            n = params['key'].lower()
            if n in _MODIFIER_MAP:
                self.modifiers |= _MODIFIER_MAP[n]
            else:
                self.pressed_keys.add(_resolve_key(params['key']))

        elif op == 'key_up':
            n = params.get('key', '').lower()
            if n in _MODIFIER_MAP:
                self.modifiers &= ~_MODIFIER_MAP[n]
            else:
                try:
                    self.pressed_keys.discard(_resolve_key(params['key']))
                except (ValueError, KeyError):
                    self.pressed_keys.clear()
                    self.modifiers = 0

        elif op == 'type_text':
            text = params.get('text', '')
            self.last_text = text

        elif op == 'hotkey':
            keys = params.get('keys', [])
            mod_byte = 0
            for k in keys:
                n = k.lower()
                if n in _MODIFIER_MAP:
                    mod_byte |= _MODIFIER_MAP[n]
                else:
                    self.pressed_keys.add(_resolve_key(k))
            self.modifiers |= mod_byte
            # release all after hotkey
            self.pressed_keys.clear()
            self.modifiers = 0

        else:
            raise ValueError('Unknown vkbd op: {!r}'.format(op))
