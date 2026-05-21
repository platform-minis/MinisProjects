// project.js — custom Blockly blocks for Esp32S3uPythonCurses
// Available variables: Blockly, generator, addCategory({ name, colour, blocks }), Order, addLibrary({ url, remoteName })

Blockly.defineBlocksWithJsonArray([
  {
    type: 'curses_init',
    message0: 'curses init',
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Initialize the curses library and create the standard screen.',
  },
  {
    type: 'curses_deinit',
    message0: 'curses deinit',
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Restore the terminal to its original state.',
  },
  {
    type: 'curses_clear',
    message0: 'curses clear',
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Clear the screen.',
  },
  {
    type: 'curses_refresh',
    message0: 'curses refresh',
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Refresh the screen to show pending changes.',
  },
  {
    type: 'curses_print',
    message0: 'curses print at row %1 col %2 text %3',
    args0: [
      { type: 'input_value', name: 'ROW', check: 'Number' },
      { type: 'input_value', name: 'COL', check: 'Number' },
      { type: 'input_value', name: 'TEXT', check: 'String' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Print text at the given row and column.',
  },
  {
    type: 'curses_getkey',
    message0: 'curses get key',
    output: 'String',
    colour: 150,
    tooltip: 'Wait for and return a keypress.',
  },
  {
    type: 'curses_move',
    message0: 'curses move to row %1 col %2',
    args0: [
      { type: 'input_value', name: 'ROW', check: 'Number' },
      { type: 'input_value', name: 'COL', check: 'Number' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Move the cursor to the given position.',
  },
]);

generator.forBlock['curses_init'] = function (_block, g) {
  g.addImport('curses', 'import curses');
  return 'stdscr = curses.initscr()\n';
};

generator.forBlock['curses_deinit'] = function (_block, _g) {
  return 'curses.endwin()\n';
};

generator.forBlock['curses_clear'] = function (_block, _g) {
  return 'stdscr.clear()\n';
};

generator.forBlock['curses_refresh'] = function (_block, _g) {
  return 'stdscr.refresh()\n';
};

generator.forBlock['curses_print'] = function (block, g) {
  var row = g.valueToCode(block, 'ROW', Order.NONE) || '0';
  var col = g.valueToCode(block, 'COL', Order.NONE) || '0';
  var text = g.valueToCode(block, 'TEXT', Order.NONE) || '""';
  return 'stdscr.addstr(' + row + ', ' + col + ', str(' + text + '))\n';
};

generator.forBlock['curses_getkey'] = function (_block, _g) {
  return ['stdscr.getkey()', Order.ATOMIC];
};

generator.forBlock['curses_move'] = function (block, g) {
  var row = g.valueToCode(block, 'ROW', Order.NONE) || '0';
  var col = g.valueToCode(block, 'COL', Order.NONE) || '0';
  return 'stdscr.move(' + row + ', ' + col + ')\n';
};

addCategory({
  name: 'Curses',
  colour: '#2e8b57',
  blocks: [
    'curses_init',
    'curses_deinit',
    'curses_clear',
    'curses_refresh',
    'curses_print',
    'curses_getkey',
    'curses_move',
  ],
});

// ─── WS2812B (on-board RGB LED, GP47) ────────────────────────────────────────

var _WS2812B_DEFS = `
_rgb = neopixel.NeoPixel(Pin(21), 1)
`;

Blockly.defineBlocksWithJsonArray([
  {
    type: 'ws2812b_init',
    message0: 'WS2812B init (GP21)',
    previousStatement: null,
    nextStatement: null,
    colour: 300,
    tooltip: 'Initialize the on-board WS2812B RGB LED on GP47.',
  },
  {
    type: 'ws2812b_fill',
    message0: 'WS2812B color %1',
    args0: [
      { type: 'input_value', name: 'COLOR', check: 'Number' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 300,
    tooltip: 'Set the WS2812B RGB LED color (0xRRGGBB hex value).',
  },
]);

generator.forBlock['ws2812b_init'] = function (_block, g) {
  g.addImport('machine_pin', 'from machine import Pin');
  g.addImport('neopixel_import', 'import neopixel');
  g.addImport('ws2812b_defs', _WS2812B_DEFS);
  return 'print("WS2812B ready")\n';
};

generator.forBlock['ws2812b_fill'] = function (block, g) {
  g.addImport('machine_pin', 'from machine import Pin');
  g.addImport('neopixel_import', 'import neopixel');
  g.addImport('ws2812b_defs', _WS2812B_DEFS);
  var color = g.valueToCode(block, 'COLOR', Order.NONE) || '0';
  return '_c = ' + color + '\n_rgb[0] = ((_c>>16)&0xff, (_c>>8)&0xff, _c&0xff)\n_rgb.write()\n';
};

// ─── BOOT button (GP0, active LOW, internal pull-up) ─────────────────────────
// State machine: short press → clicked, long press (≥500 ms) → held

var _BOOT_BTN_DEFS = `
_btn_pin = Pin(0, Pin.IN, Pin.PULL_UP)
_btn_state = {'pressed': False, 'last_ms': 0, 'clicked': False, 'held': False}
_HOLD_MS = 500
def _btn_tick():
    now = time.ticks_ms()
    p = not _btn_pin.value()
    _btn_state['clicked'] = False
    _btn_state['held'] = False
    if p and not _btn_state['pressed']:
        _btn_state['pressed'] = True
        _btn_state['last_ms'] = now
    elif not p and _btn_state['pressed']:
        _btn_state['pressed'] = False
        if time.ticks_diff(now, _btn_state['last_ms']) < _HOLD_MS:
            _btn_state['clicked'] = True
        else:
            _btn_state['held'] = True
`;

Blockly.defineBlocksWithJsonArray([
  {
    type: 'boot_btn_init',
    message0: 'BOOT button init (GP0)',
    previousStatement: null,
    nextStatement: null,
    colour: 210,
    tooltip: 'Initialize the BOOT button on GP0 (active LOW, internal pull-up).',
  },
  {
    type: 'boot_btn_tick',
    message0: 'BOOT button tick',
    previousStatement: null,
    nextStatement: null,
    colour: 210,
    tooltip: 'Update BOOT button state — call once per loop iteration.',
  },
  {
    type: 'boot_btn_clicked',
    message0: 'BOOT button clicked?',
    output: 'Boolean',
    colour: 210,
    tooltip: 'True if BOOT button was short-pressed since last tick.',
  },
  {
    type: 'boot_btn_held',
    message0: 'BOOT button held?',
    output: 'Boolean',
    colour: 210,
    tooltip: 'True if BOOT button was held (long press) since last tick.',
  },
]);

generator.forBlock['boot_btn_init'] = function (_block, g) {
  g.addImport('machine_pin', 'from machine import Pin');
  g.addImport('time_import', 'import time');
  g.addImport('boot_btn_defs', _BOOT_BTN_DEFS);
  return 'print("BOOT button ready")\n';
};

generator.forBlock['boot_btn_tick'] = function (_block, g) {
  g.addImport('machine_pin', 'from machine import Pin');
  g.addImport('time_import', 'import time');
  g.addImport('boot_btn_defs', _BOOT_BTN_DEFS);
  return '_btn_tick()\n';
};

generator.forBlock['boot_btn_clicked'] = function (_block, g) {
  g.addImport('machine_pin', 'from machine import Pin');
  g.addImport('time_import', 'import time');
  g.addImport('boot_btn_defs', _BOOT_BTN_DEFS);
  return ["_btn_state['clicked']", Order.ATOMIC];
};

generator.forBlock['boot_btn_held'] = function (_block, g) {
  g.addImport('machine_pin', 'from machine import Pin');
  g.addImport('time_import', 'import time');
  g.addImport('boot_btn_defs', _BOOT_BTN_DEFS);
  return ["_btn_state['held']", Order.ATOMIC];
};

addCategory({
  name: 'RGB LED',
  colour: '#8b008b',
  blocks: ['ws2812b_init', 'ws2812b_fill'],
});

addCategory({
  name: 'BOOT Button',
  colour: '#1565c0',
  blocks: ['boot_btn_init', 'boot_btn_tick', 'boot_btn_clicked', 'boot_btn_held'],
});
