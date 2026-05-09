// project.js — custom Blockly blocks for Esp32C3Lamp
// WS2812 LED ring (17 LEDs, red) on GP3 of ESP32-C3 Super Mini
// Available variables: Blockly, generator, addCategory({ name, colour, blocks }), Order, addLibrary({ url, remoteName })

// ─── WS2812 NeoPixel blocks ───────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'ws2812_fill',
    message0: 'WS2812 fill  R %1  G %2  B %3',
    args0: [
      { type: 'input_value', name: 'R', check: 'Number' },
      { type: 'input_value', name: 'G', check: 'Number' },
      { type: 'input_value', name: 'B', check: 'Number' },
    ],
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Fill all 17 WS2812 LEDs with the given RGB colour and write immediately. Values 0–255.',
  },
  {
    type: 'ws2812_fill_red',
    message0: 'WS2812 fill red  brightness %1',
    args0: [
      { type: 'input_value', name: 'VAL', check: 'Number' },
    ],
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Fill all 17 LEDs with red at the given brightness (0–255) and write immediately.',
  },
  {
    type: 'ws2812_clear',
    message0: 'WS2812 clear (all off)',
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Turn off all 17 WS2812 LEDs.',
  },
  {
    type: 'ws2812_set',
    message0: 'WS2812 set pixel %1  R %2  G %3  B %4',
    args0: [
      { type: 'input_value', name: 'IDX', check: 'Number' },
      { type: 'input_value', name: 'R',   check: 'Number' },
      { type: 'input_value', name: 'G',   check: 'Number' },
      { type: 'input_value', name: 'B',   check: 'Number' },
    ],
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Set one WS2812 pixel (0–16) to an RGB colour. Call WS2812 write to push the change.',
  },
  {
    type: 'ws2812_write',
    message0: 'WS2812 write',
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Push the pixel buffer to the WS2812 ring. Required after ws2812_set calls.',
  },
  {
    type: 'ws2812_num_leds',
    message0: 'WS2812 LED count',
    output: 'Number',
    colour: 30,
    tooltip: 'Returns the number of LEDs in the ring (17).',
  },
]);

var _WS2812_DEFS = `
import neopixel as _np_mod
from machine import Pin as _np_pin
_np = _np_mod.NeoPixel(_np_pin(3), 17)

def _ws2812_fill(r, g, b):
    for i in range(17):
        _np[i] = (r, g, b)
    _np.write()

def _ws2812_fill_red(brightness):
    _ws2812_fill(min(255, max(0, int(brightness))), 0, 0)

def _ws2812_clear():
    _ws2812_fill(0, 0, 0)

def _ws2812_set(idx, r, g, b):
    if 0 <= idx < 17:
        _np[idx] = (r, g, b)

def _ws2812_write():
    _np.write()
`;

generator.forBlock['ws2812_fill'] = function (block, g) {
  g.addImport('ws2812_defs', _WS2812_DEFS);
  var r = g.valueToCode(block, 'R', Order.NONE) || '0';
  var gr = g.valueToCode(block, 'G', Order.NONE) || '0';
  var b = g.valueToCode(block, 'B', Order.NONE) || '0';
  return '_ws2812_fill(' + r + ', ' + gr + ', ' + b + ')\n';
};

generator.forBlock['ws2812_fill_red'] = function (block, g) {
  g.addImport('ws2812_defs', _WS2812_DEFS);
  var val = g.valueToCode(block, 'VAL', Order.NONE) || '0';
  return '_ws2812_fill_red(' + val + ')\n';
};

generator.forBlock['ws2812_clear'] = function (_block, g) {
  g.addImport('ws2812_defs', _WS2812_DEFS);
  return '_ws2812_clear()\n';
};

generator.forBlock['ws2812_set'] = function (block, g) {
  g.addImport('ws2812_defs', _WS2812_DEFS);
  var idx = g.valueToCode(block, 'IDX', Order.NONE) || '0';
  var r   = g.valueToCode(block, 'R',   Order.NONE) || '0';
  var gr  = g.valueToCode(block, 'G',   Order.NONE) || '0';
  var b   = g.valueToCode(block, 'B',   Order.NONE) || '0';
  return '_ws2812_set(' + idx + ', ' + r + ', ' + gr + ', ' + b + ')\n';
};

generator.forBlock['ws2812_write'] = function (_block, g) {
  g.addImport('ws2812_defs', _WS2812_DEFS);
  return '_ws2812_write()\n';
};

generator.forBlock['ws2812_num_leds'] = function (_block, g) {
  g.addImport('ws2812_defs', _WS2812_DEFS);
  return ['17', Order.ATOMIC];
};

addCategory({
  name: 'WS2812',
  colour: '#bf360c',
  blocks: [
    'ws2812_fill',
    'ws2812_fill_red',
    'ws2812_clear',
    'ws2812_set',
    'ws2812_write',
    'ws2812_num_leds',
  ],
});
