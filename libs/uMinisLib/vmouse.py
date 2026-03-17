"""
vmouse.py — VirtualMouse extension for MyCastle IoT (MicroPython)

Maintains an internal virtual mouse state (position, button states).
No hardware output — state can be read by GUI automation code running
on the device.

Usage::

    from minis_iot import MinisIoT
    from vmouse import VirtualMouse

    minis = MinisIoT(host, port, user_id, device_id)
    mouse = VirtualMouse(minis)
    minis.begin()

    while True:
        minis.loop()
        # read state:
        # mouse.x, mouse.y          — current cursor position
        # mouse.buttons             — bitmask of held buttons (BTN_* constants)
        # mouse.last_click          — last clicked button name
        # mouse.last_scroll_dy      — last scroll delta
"""

# Button bitmask constants
BTN_LEFT   = 0x01
BTN_RIGHT  = 0x02
BTN_MIDDLE = 0x04

_BUTTON_MAP = {
    'left':   BTN_LEFT,
    'right':  BTN_RIGHT,
    'middle': BTN_MIDDLE,
}


def _resolve_button(name: str) -> int:
    return _BUTTON_MAP.get((name or 'left').lower(), BTN_LEFT)


class VirtualMouse:
    """
    MyCastle VirtualMouse extension — internal virtual state only.

    Registers itself with the MinisIoT instance as the 'vmouse' extension.
    Tracks cursor position and button state; no hardware output.

    Public state attributes (read by device application code):
      x, y              int        — current virtual cursor position
      buttons           int        — bitmask of currently held buttons
      last_click        str|None   — button name of most recent click
      last_scroll_dy    int        — last vertical scroll delta

    Supported ops (received from MyCastle server):
      move         { x, y }                         — set absolute position
      move_rel     { dx, dy }                        — move relative
      click        { button?, x?, y? }               — click (updates pos if given)
      double_click { button?, x?, y? }               — double-click
      press        { button? }                       — hold button
      release      { button? }                       — release button
      scroll       { dy }                            — record scroll delta
      drag         { x1, y1, x2, y2, button? }       — press, move, release
      get_pos      {}                                — returns { x, y }
    """

    EXT_TYPE = 'vmouse'

    def __init__(self, minis):
        self._minis = minis
        self.x: int = 0
        self.y: int = 0
        self.buttons: int = 0
        self.last_click: str | None = None
        self.last_scroll_dy: int = 0
        minis.add_extension(self.EXT_TYPE, self._on_request)
        print('[vmouse] Virtual mouse ready (internal state)')

    # ── Extension callback ────────────────────────────────────────────────────

    def _on_request(self, req_id: str, op: str, params: dict):
        data = None
        try:
            data = self._dispatch(op, params)
            self._minis.ext_respond(self.EXT_TYPE, req_id, True, data)
        except Exception as e:
            self._minis.ext_respond(self.EXT_TYPE, req_id, False,
                                    error={'code': 'Error', 'message': str(e)})

    # ── Dispatch ──────────────────────────────────────────────────────────────

    def _dispatch(self, op: str, params: dict):
        if op == 'move':
            self.x = int(params['x'])
            self.y = int(params['y'])

        elif op == 'move_rel':
            self.x += int(params['dx'])
            self.y += int(params['dy'])

        elif op == 'click':
            btn_name = params.get('button', 'left')
            if 'x' in params and 'y' in params:
                self.x = int(params['x'])
                self.y = int(params['y'])
            self.last_click = btn_name

        elif op == 'double_click':
            btn_name = params.get('button', 'left')
            if 'x' in params and 'y' in params:
                self.x = int(params['x'])
                self.y = int(params['y'])
            self.last_click = btn_name

        elif op == 'press':
            self.buttons |= _resolve_button(params.get('button', 'left'))

        elif op == 'release':
            self.buttons &= ~_resolve_button(params.get('button', 'left'))

        elif op == 'scroll':
            self.last_scroll_dy = int(params.get('dy', 0))

        elif op == 'drag':
            btn = params.get('button', 'left')
            self.x = int(params['x1'])
            self.y = int(params['y1'])
            self.buttons |= _resolve_button(btn)
            self.x = int(params['x2'])
            self.y = int(params['y2'])
            self.buttons &= ~_resolve_button(btn)

        elif op == 'get_pos':
            return {'x': self.x, 'y': self.y}

        else:
            raise ValueError('Unknown vmouse op: {!r}'.format(op))

        return None
