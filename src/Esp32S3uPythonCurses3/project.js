// project.js — custom Blockly blocks for Esp32S3uPythonCurses3
// Same sensors as Curses2 + LCD1602 I2C display (SDA=GP21, SCL=GP22)
// Available variables: Blockly, generator, addCategory({ name, colour, blocks }), Order, addLibrary({ url, remoteName })

// ─── FieldLedMatrix: clickable 8×8 LED grid field ────────────────────────────
// Inline view shows the pattern at scale; clicking opens a popup editor
// with a larger grid and preset buttons.

class FieldLedMatrix extends Blockly.Field {
  static CELL   = 7;   // inline cell size (px)
  static BORDER = 1;   // gap between cells (px)
  static PRESETS = {
    'Smiley':   [60, 66, 165, 129, 165, 153, 66, 60],
    'Heart':    [0, 102, 255, 255, 126, 60, 24, 0],
    'Cross':    [129, 66, 36, 24, 24, 36, 66, 129],
    'Arrow ↑':  [24, 60, 126, 255, 24, 24, 24, 24],
  };

  constructor(value) {
    super(Blockly.Field.SKIP_SETUP);
    this.SERIALIZABLE = true;
    this.CURSOR = 'pointer';
    this._cells = null;
    this.setValue(value || '60,66,165,129,165,153,66,60');
  }

  static fromJson(opt) {
    return new FieldLedMatrix(opt['value'] || '60,66,165,129,165,153,66,60');
  }

  _getBytes() {
    const v = this.getValue() || '0,0,0,0,0,0,0,0';
    const parts = v.split(',').map(Number);
    while (parts.length < 8) parts.push(0);
    return parts.slice(0, 8);
  }

  initView() {
    const C = FieldLedMatrix.CELL, B = FieldLedMatrix.BORDER;
    this._cells = [];
    for (let r = 0; r < 8; r++) {
      const row = [];
      for (let c = 0; c < 8; c++) {
        const rect = Blockly.utils.dom.createSvgElement('rect', {
          x: B + c * (C + B), y: B + r * (C + B),
          width: C, height: C, rx: 1,
          fill: '#2a2a2a', stroke: '#444', 'stroke-width': '0.5',
        }, this.fieldGroup_);
        row.push(rect);
      }
      this._cells.push(row);
    }
    this._updateCells();
  }

  _updateCells() {
    if (!this._cells) return;
    const bytes = this._getBytes();
    for (let r = 0; r < 8; r++) {
      for (let c = 0; c < 8; c++) {
        const on = (bytes[r] >> (7 - c)) & 1;
        this._cells[r][c].setAttribute('fill', on ? '#FFD700' : '#2a2a2a');
      }
    }
  }

  doValueUpdate_(v) { super.doValueUpdate_(v); this._updateCells(); }

  updateSize_() {
    const C = FieldLedMatrix.CELL, B = FieldLedMatrix.BORDER;
    const total = 8 * (C + B) + B;
    this.size_ = { width: total + 4, height: total + 4 };
  }

  render_() { this._updateCells(); this.updateSize_(); }

  showEditor_() {
    const C = 18, B = 2, total = 8 * (C + B) + B;
    const bytes = this._getBytes();
    let painting = false, paintVal = 1;

    const wrap = document.createElement('div');
    wrap.style.cssText = 'padding:8px;background:#1e1e1e;border-radius:6px;user-select:none;';

    const NS = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(NS, 'svg');
    svg.setAttribute('width', total); svg.setAttribute('height', total);
    svg.style.cssText = 'display:block;cursor:crosshair;';

    const cells = [];
    const redraw = () => cells.forEach(({ r, c, el }) => {
      el.setAttribute('fill', (bytes[r] >> (7 - c)) & 1 ? '#FFD700' : '#2a2a2a');
    });
    const paint = (r, c, v) => {
      if (v) bytes[r] |= (1 << (7 - c)); else bytes[r] &= ~(1 << (7 - c));
      redraw(); this.setValue(bytes.join(','));
    };

    for (let r = 0; r < 8; r++) {
      for (let c = 0; c < 8; c++) {
        const el = document.createElementNS(NS, 'rect');
        el.setAttribute('x', B + c * (C + B)); el.setAttribute('y', B + r * (C + B));
        el.setAttribute('width', C); el.setAttribute('height', C); el.setAttribute('rx', 3);
        el.setAttribute('fill', '#2a2a2a'); el.setAttribute('stroke', '#555');
        el.setAttribute('stroke-width', '1');
        svg.appendChild(el); cells.push({ r, c, el });
      }
    }
    redraw();

    svg.addEventListener('mousedown', (e) => {
      const ci = cells.findIndex(x => x.el === e.target);
      if (ci < 0) return; e.preventDefault(); painting = true;
      const { r, c } = cells[ci];
      paintVal = (bytes[r] >> (7 - c)) & 1 ? 0 : 1;
      paint(r, c, paintVal);
    });
    svg.addEventListener('mouseover', (e) => {
      if (!painting) return;
      const ci = cells.findIndex(x => x.el === e.target);
      if (ci >= 0) { const { r, c } = cells[ci]; paint(r, c, paintVal); }
    });
    document.addEventListener('mouseup', () => { painting = false; }, { once: true });
    wrap.appendChild(svg);

    const btns = document.createElement('div');
    btns.style.cssText = 'display:flex;flex-wrap:wrap;gap:4px;margin-top:6px;';
    for (const [label, preset] of Object.entries(FieldLedMatrix.PRESETS)) {
      const btn = document.createElement('button');
      btn.textContent = label;
      btn.style.cssText = 'background:#333;color:#ddd;border:1px solid #555;border-radius:3px;padding:2px 8px;cursor:pointer;font-size:12px;';
      btn.addEventListener('mousedown', (e) => {
        e.stopPropagation();
        for (let i = 0; i < 8; i++) bytes[i] = preset[i];
        redraw(); this.setValue(bytes.join(','));
      });
      btns.appendChild(btn);
    }
    const clr = document.createElement('button');
    clr.textContent = 'Clear';
    clr.style.cssText = 'background:#333;color:#888;border:1px solid #555;border-radius:3px;padding:2px 8px;cursor:pointer;font-size:12px;';
    clr.addEventListener('mousedown', (e) => {
      e.stopPropagation();
      for (let i = 0; i < 8; i++) bytes[i] = 0;
      redraw(); this.setValue(bytes.join(','));
    });
    btns.appendChild(clr);
    wrap.appendChild(btns);

    Blockly.DropDownDiv.getContentDiv().appendChild(wrap);
    Blockly.DropDownDiv.showPositionedByField(this, () => {});
  }
}

try { Blockly.fieldRegistry.register('field_led_matrix', FieldLedMatrix); } catch (_) {}

// ─── LCD1602 I2C blocks ───────────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'lcd_init',
    message0: 'LCD1602 init  SDA=GP21  SCL=GP22',
    previousStatement: null,
    nextStatement: null,
    colour: 200,
    tooltip: 'Initialise the LCD1602 over I2C0 (SDA=GP21, SCL=GP22, address 0x27).',
  },
  {
    type: 'lcd_clear',
    message0: 'LCD1602 clear',
    previousStatement: null,
    nextStatement: null,
    colour: 200,
    tooltip: 'Clear all characters from the LCD display.',
  },
  {
    type: 'lcd_write',
    message0: 'LCD1602 line %1  %2',
    args0: [
      {
        type: 'field_dropdown',
        name: 'ROW',
        options: [['1', '0'], ['2', '1']],
      },
      { type: 'input_value', name: 'TEXT' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 200,
    tooltip: 'Write text to line 1 or 2 of the LCD (padded/truncated to 16 characters).',
  },
]);

var _LCD_DEFS = `
from lcd1602 import LCD1602
_i2c_lcd = I2C(0, sda=Pin(21), scl=Pin(22), freq=400000)
_lcd = LCD1602(_i2c_lcd, 0x27)
def _lcd_init():
    _lcd.clear()
def _lcd_clear():
    _lcd.clear()
def _lcd_write(row, text):
    _lcd.write_line(row, text)
`;

generator.forBlock['lcd_init'] = function (_block, g) {
  g.addImport('machine_i2c_lcd', 'from machine import I2C, Pin');
  g.addImport('lcd_defs', _LCD_DEFS);
  return '_lcd_init()\n';
};

generator.forBlock['lcd_clear'] = function (_block, g) {
  g.addImport('machine_i2c_lcd', 'from machine import I2C, Pin');
  g.addImport('lcd_defs', _LCD_DEFS);
  return '_lcd_clear()\n';
};

generator.forBlock['lcd_write'] = function (block, g) {
  g.addImport('machine_i2c_lcd', 'from machine import I2C, Pin');
  g.addImport('lcd_defs', _LCD_DEFS);
  var row  = block.getFieldValue('ROW');
  var text = g.valueToCode(block, 'TEXT', Order.NONE) || '""';
  return '_lcd_write(' + row + ', ' + text + ')\n';
};

addCategory({
  name: 'LCD1602',
  colour: '#00695c',
  blocks: ['lcd_init', 'lcd_clear', 'lcd_write'],
});

// ─── MAX7219 driver helpers (patterns inlined by generator, not named here) ──

var _MAX7219_DEFS = `
_REG_DECODE    = 0x09
_REG_INTENSITY = 0x0A
_REG_SCANLIMIT = 0x0B
_REG_SHUTDOWN  = 0x0C
_REG_DISPTEST  = 0x0F
_spi = SPI(1, baudrate=10_000_000, polarity=0, phase=0, sck=Pin(18), mosi=Pin(19), miso=Pin(4))
_cs  = Pin(5, Pin.OUT, value=1)
def _write(reg, data):
    _cs.value(0)
    _spi.write(bytes([reg, data]))
    _cs.value(1)
def init_max7219():
    _write(_REG_DECODE, 0x00)
    _write(_REG_INTENSITY, 0x04)
    _write(_REG_SCANLIMIT, 0x07)
    _write(_REG_SHUTDOWN, 0x01)
    _write(_REG_DISPTEST, 0x00)
def clear_display():
    for row in range(1, 9):
        _write(row, 0x00)
def show_pattern(pattern):
    for row_idx, bits in enumerate(pattern):
        _write(row_idx + 1, bits)
`;

Blockly.defineBlocksWithJsonArray([
  {
    type: 'max7219_init',
    message0: 'MAX7219 init  CLK=GP18  DIN=GP19  CS=GP5',
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: 'Initialize the MAX7219 LED matrix driver over SPI (CLK=GP18, DIN=GP19, CS=GP5).',
  },
  {
    type: 'max7219_clear',
    message0: 'MAX7219 clear display',
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: 'Turn off all 64 LEDs.',
  },
  {
    type: 'max7219_show',
    message0: 'MAX7219 show %1',
    args0: [
      {
        type: 'field_led_matrix',
        name: 'PATTERN',
        value: '60,66,165,129,165,153,66,60',
      },
    ],
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: 'Display a pixel pattern on the 8×8 LED matrix. Click the grid to toggle LEDs or choose a preset.',
  },
  {
    type: 'max7219_brightness',
    message0: 'MAX7219 brightness %1',
    args0: [
      { type: 'input_value', name: 'LEVEL', check: 'Number' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: 'Set display brightness: 0 (min) – 15 (max).',
  },
  {
    type: 'max7219_show_bar',
    message0: 'MAX7219 bar level %1',
    args0: [
      { type: 'input_value', name: 'LEVEL', check: 'Number' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: 'Show a vertical VU-meter bar: 0 = all off, 8 = all rows lit (bottom to top).',
  },
  {
    type: 'max7219_show_var',
    message0: 'MAX7219 show list %1',
    args0: [
      { type: 'input_value', name: 'PATTERN' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 230,
    tooltip: 'Display a pattern on the 8×8 LED matrix from a list of 8 bytes (each 0–255). Connect a variable or list block.',
  },
  {
    type: 'ky018_read',
    message0: 'KY-018 read (GP7)',
    output: 'Number',
    colour: 60,
    tooltip: 'Read raw light level from KY-018 on GP7: 0 (dark) – 4095 (bright).',
  },
  {
    type: 'ky018_to_level',
    message0: 'KY-018 to level %1',
    args0: [
      { type: 'input_value', name: 'VALUE', check: 'Number' },
    ],
    inputsInline: true,
    output: 'Number',
    colour: 60,
    tooltip: 'Map a raw ADC value (0–4095) to a bar level (0–8).',
  },
]);

generator.forBlock['max7219_init'] = function (_block, g) {
  g.addImport('machine_spi', 'from machine import SPI, Pin');
  g.addImport('max7219_defs', _MAX7219_DEFS);
  return 'init_max7219()\n';
};

generator.forBlock['max7219_clear'] = function (_block, g) {
  g.addImport('machine_spi', 'from machine import SPI, Pin');
  g.addImport('max7219_defs', _MAX7219_DEFS);
  return 'clear_display()\n';
};

generator.forBlock['max7219_show'] = function (block, g) {
  g.addImport('machine_spi', 'from machine import SPI, Pin');
  g.addImport('max7219_defs', _MAX7219_DEFS);
  var val = block.getFieldValue('PATTERN') || '0,0,0,0,0,0,0,0';
  var byteArr = val.split(',').map(Number);
  while (byteArr.length < 8) byteArr.push(0);
  var hexList = '[' + byteArr.slice(0, 8).map(function(b) {
    return '0x' + (b & 0xFF).toString(16).toUpperCase().padStart(2, '0');
  }).join(', ') + ']';
  return 'show_pattern(' + hexList + ')\n';
};

generator.forBlock['max7219_brightness'] = function (block, g) {
  g.addImport('machine_spi', 'from machine import SPI, Pin');
  g.addImport('max7219_defs', _MAX7219_DEFS);
  var level = g.valueToCode(block, 'LEVEL', Order.NONE) || '4';
  return '_write(_REG_INTENSITY, ' + level + ')\n';
};

generator.forBlock['max7219_show_bar'] = function (block, g) {
  g.addImport('machine_spi', 'from machine import SPI, Pin');
  g.addImport('max7219_defs', _MAX7219_DEFS);
  g.addImport('max7219_show_bar_fn',
`def show_bar(level):
    for i in range(8):
        _write(i + 1, 0xFF if i >= (8 - level) else 0x00)
`);
  var level = g.valueToCode(block, 'LEVEL', Order.NONE) || '0';
  return 'show_bar(' + level + ')\n';
};

generator.forBlock['max7219_show_var'] = function (block, g) {
  g.addImport('machine_spi', 'from machine import SPI, Pin');
  g.addImport('max7219_defs', _MAX7219_DEFS);
  var pattern = g.valueToCode(block, 'PATTERN', Order.NONE) || '[0,0,0,0,0,0,0,0]';
  return 'show_pattern(' + pattern + ')\n';
};

var _KY018_DEFS = `
_adc_ky018 = ADC(Pin(7), atten=ADC.ATTN_11DB)
def _ky018_read():
    return _adc_ky018.read()
def _ky018_to_level(raw):
    return min(int(raw * 8 / 4095), 8)
`;

generator.forBlock['ky018_read'] = function (_block, g) {
  g.addImport('machine_adc', 'from machine import ADC, Pin');
  g.addImport('ky018_defs', _KY018_DEFS);
  return ['_ky018_read()', Order.ATOMIC];
};

generator.forBlock['ky018_to_level'] = function (block, g) {
  g.addImport('machine_adc', 'from machine import ADC, Pin');
  g.addImport('ky018_defs', _KY018_DEFS);
  var val = g.valueToCode(block, 'VALUE', Order.NONE) || '0';
  return ['_ky018_to_level(' + val + ')', Order.ATOMIC];
};

addCategory({
  name: 'MAX7219',
  colour: '#3a6ea5',
  blocks: [
    'max7219_init',
    'max7219_clear',
    'max7219_show',
    'max7219_show_var',
    'max7219_brightness',
    'max7219_show_bar',
  ],
});

addCategory({
  name: 'KY-018',
  colour: '#8b6914',
  blocks: [
    'ky018_read',
    'ky018_to_level',
  ],
});

// ─── DHT11 blocks ─────────────────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'dht11_measure',
    message0: 'DHT11 measure (GP3)',
    previousStatement: null,
    nextStatement: null,
    colour: 120,
    tooltip: 'Trigger one measurement on the DHT11 sensor (DATA=GP3). Call before reading temperature or humidity.',
  },
  {
    type: 'dht11_temp',
    message0: 'DHT11 temperature',
    output: 'Number',
    colour: 120,
    tooltip: 'Return temperature in °C from the last DHT11 measurement.',
  },
  {
    type: 'dht11_hum',
    message0: 'DHT11 humidity',
    output: 'Number',
    colour: 120,
    tooltip: 'Return relative humidity in % from the last DHT11 measurement.',
  },
]);

var _DHT11_DEFS = `
_dht11 = dht.DHT11(Pin(3))
def _dht11_measure():
    _dht11.measure()
def _dht11_temp():
    return _dht11.temperature()
def _dht11_hum():
    return _dht11.humidity()
`;

generator.forBlock['dht11_measure'] = function (_block, g) {
  g.addImport('dht_import', 'import dht');
  g.addImport('machine_pin_dht', 'from machine import Pin');
  g.addImport('dht11_defs', _DHT11_DEFS);
  return '_dht11_measure()\n';
};

generator.forBlock['dht11_temp'] = function (_block, g) {
  g.addImport('dht_import', 'import dht');
  g.addImport('machine_pin_dht', 'from machine import Pin');
  g.addImport('dht11_defs', _DHT11_DEFS);
  return ['_dht11_temp()', Order.ATOMIC];
};

generator.forBlock['dht11_hum'] = function (_block, g) {
  g.addImport('dht_import', 'import dht');
  g.addImport('machine_pin_dht', 'from machine import Pin');
  g.addImport('dht11_defs', _DHT11_DEFS);
  return ['_dht11_hum()', Order.ATOMIC];
};

addCategory({
  name: 'DHT11',
  colour: '#2e7d32',
  blocks: [
    'dht11_measure',
    'dht11_temp',
    'dht11_hum',
  ],
});

// ─── HC-SR04 blocks ───────────────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'hcsr04_measure',
    message0: 'HC-SR04 measure (TRIG=GP5 ECHO=GP4)',
    output: 'Number',
    colour: 200,
    tooltip: 'Measure distance in cm (TRIG=GP5, ECHO=GP4). Returns -1 on timeout, value outside 2–400 if out of range.',
  },
]);

var _HCSR04_DEFS = `
_trig = Pin(5, Pin.OUT)
_echo = Pin(4, Pin.IN)
_trig.value(0)
def _hcsr04_measure():
    _trig.value(0)
    time.sleep_us(2)
    _trig.value(1)
    time.sleep_us(10)
    _trig.value(0)
    duration = time_pulse_us(_echo, 1, 30000)
    if duration < 0:
        return -1
    return duration / 58
`;

generator.forBlock['hcsr04_measure'] = function (_block, g) {
  g.addImport('machine_hcsr04', 'from machine import Pin, time_pulse_us');
  g.addImport('time_import', 'import time');
  g.addImport('hcsr04_defs', _HCSR04_DEFS);
  return ['_hcsr04_measure()', Order.ATOMIC];
};

addCategory({
  name: 'HC-SR04',
  colour: '#1565c0',
  blocks: ['hcsr04_measure'],
});

// ─── HC-SR501 blocks ──────────────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'hcsr501_read',
    message0: 'HC-SR501 motion? (GP2)',
    output: 'Boolean',
    colour: 30,
    tooltip: 'Return True if the HC-SR501 PIR sensor detects motion (OUT=GP2).',
  },
]);

var _HCSR501_DEFS = `
_pir = Pin(2, Pin.IN)
def _hcsr501_read():
    return bool(_pir.value())
`;

generator.forBlock['hcsr501_read'] = function (_block, g) {
  g.addImport('machine_pin_pir', 'from machine import Pin');
  g.addImport('hcsr501_defs', _HCSR501_DEFS);
  return ['_hcsr501_read()', Order.ATOMIC];
};

addCategory({
  name: 'HC-SR501',
  colour: '#e65100',
  blocks: ['hcsr501_read'],
});

// ─── RC-522 blocks ────────────────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'rc522_init',
    message0: 'RC-522 init  SCK=GP18  MOSI=GP19  MISO=GP16  SDA=GP17  RST=GP15',
    previousStatement: null,
    nextStatement: null,
    colour: 160,
    tooltip: 'Initialise the RC-522 RFID reader over SPI (SCK=GP18, MOSI=GP19, MISO=GP16, SDA=GP17, RST=GP15).',
  },
  {
    type: 'rc522_read_uid',
    message0: 'RC-522 read UID',
    output: 'String',
    colour: 160,
    tooltip: 'Return the UID of a detected RFID card as a hex string (e.g. "A1:B2:C3:D4"), or "" if no card is present.',
  },
]);

var _RC522_DEFS = `
import mfrc522 as _mfrc522_mod
_rc522_obj = _mfrc522_mod.MFRC522(sck=18, mosi=19, miso=16, rst=15, cs=17)
def _rc522_init():
    print('RC-522 ready   SCK=GP18 MOSI=GP19 MISO=GP16 SDA=GP17 RST=GP15')
def _rc522_read_uid():
    stat, _ = _rc522_obj.request(_rc522_obj.REQIDL)
    if stat != _rc522_obj.OK:
        return ''
    stat, uid = _rc522_obj.SelectTagSN()
    if stat != _rc522_obj.OK:
        return ''
    return ':'.join('{:02X}'.format(b) for b in uid)
`;

generator.forBlock['rc522_init'] = function (_block, g) {
  g.addImport('machine_spi_rc522', 'from machine import SPI, Pin');
  g.addImport('rc522_defs', _RC522_DEFS);
  return '_rc522_init()\n';
};

generator.forBlock['rc522_read_uid'] = function (_block, g) {
  g.addImport('machine_spi_rc522', 'from machine import SPI, Pin');
  g.addImport('rc522_defs', _RC522_DEFS);
  return ['_rc522_read_uid()', Order.ATOMIC];
};

addCategory({
  name: 'RC-522',
  colour: '#1b5e20',
  blocks: ['rc522_init', 'rc522_read_uid'],
});

// ─── PS2 Joystick blocks ──────────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'joystick_read_x',
    message0: 'Joystick X (GP1)',
    output: 'Number',
    colour: 90,
    tooltip: 'Read raw X-axis ADC value (GP1): 0 = full left, 4095 = full right, ~2048 at center.',
  },
  {
    type: 'joystick_read_y',
    message0: 'Joystick Y (GP6)',
    output: 'Number',
    colour: 90,
    tooltip: 'Read raw Y-axis ADC value (GP6): 0 = full up, 4095 = full down, ~2048 at center.',
  },
  {
    type: 'joystick_pressed',
    message0: 'Joystick button pressed? (GP8)',
    output: 'Boolean',
    colour: 90,
    tooltip: 'Return True if the joystick button (SW, GP8) is pressed.',
  },
  {
    type: 'joystick_direction',
    message0: 'Joystick direction (GP1, GP6)',
    output: 'String',
    colour: 90,
    tooltip: 'Return direction label from X/Y axes: LEFT, RIGHT, UP, DOWN, UP-LEFT, UP-RIGHT, DOWN-LEFT, DOWN-RIGHT, or CENTER.',
  },
]);

var _JOYSTICK_DEFS = `
_adc_jx = ADC(Pin(1), atten=ADC.ATTN_11DB)
_adc_jy = ADC(Pin(6), atten=ADC.ATTN_11DB)
_sw_joy = Pin(8, Pin.IN, Pin.PULL_UP)
def _joy_x():
    return _adc_jx.read()
def _joy_y():
    return _adc_jy.read()
def _joy_pressed():
    return not _sw_joy.value()
def _joy_direction():
    x = _adc_jx.read()
    y = _adc_jy.read()
    h = 'LEFT' if x < 1000 else ('RIGHT' if x > 3000 else '')
    v = 'UP' if y < 1000 else ('DOWN' if y > 3000 else '')
    if h and v: return h + '-' + v
    return h or v or 'CENTER'
`;

generator.forBlock['joystick_read_x'] = function (_block, g) {
  g.addImport('machine_adc', 'from machine import ADC, Pin');
  g.addImport('joystick_defs', _JOYSTICK_DEFS);
  return ['_joy_x()', Order.ATOMIC];
};

generator.forBlock['joystick_read_y'] = function (_block, g) {
  g.addImport('machine_adc', 'from machine import ADC, Pin');
  g.addImport('joystick_defs', _JOYSTICK_DEFS);
  return ['_joy_y()', Order.ATOMIC];
};

generator.forBlock['joystick_pressed'] = function (_block, g) {
  g.addImport('machine_adc', 'from machine import ADC, Pin');
  g.addImport('joystick_defs', _JOYSTICK_DEFS);
  return ['_joy_pressed()', Order.ATOMIC];
};

generator.forBlock['joystick_direction'] = function (_block, g) {
  g.addImport('machine_adc', 'from machine import ADC, Pin');
  g.addImport('joystick_defs', _JOYSTICK_DEFS);
  return ['_joy_direction()', Order.ATOMIC];
};

addCategory({
  name: 'Joystick',
  colour: '#33691e',
  blocks: ['joystick_read_x', 'joystick_read_y', 'joystick_pressed', 'joystick_direction'],
});
