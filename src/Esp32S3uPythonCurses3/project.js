// project.js — Esp32S3uPythonCurses3
// Lesson31: AHT20+BMP280 (I2C0: SDA=GP13/SCL=GP14) + LCD1602 (I2C1: SDA=GP43/SCL=GP44)

// ─── LCD1602 I2C1 blocks ─────────────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'lcd_init',
    message0: 'LCD1602 init  SDA=GP43  SCL=GP44',
    previousStatement: null,
    nextStatement: null,
    colour: 200,
    tooltip: 'Initialise LCD1602 over I2C1 (SDA=GP43, SCL=GP44, address 0x27). Call once in setup.',
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
from machine import I2C, Pin
import time
_i2c1 = I2C(1, sda=Pin(33), scl=Pin(34), freq=400000)
class _LCD:
    def __init__(self, i2c, addr=0x27):
        self.i2c = i2c; self.addr = addr; self._bl = 0x08; self._setup()
    def _wb(self, b): self.i2c.writeto(self.addr, bytes([b | self._bl]))
    def _pulse(self, b):
        self._wb(b | 4); time.sleep_us(1); self._wb(b & ~4); time.sleep_us(50)
    def _s4(self, n): self._wb(n); self._pulse(n)
    def _cmd(self, d, m=0):
        rs = m & 1; self._s4((d & 0xF0) | rs); self._s4(((d << 4) & 0xF0) | rs)
    def _setup(self):
        time.sleep_ms(50); self._s4(0x30); time.sleep_ms(5); self._s4(0x30)
        time.sleep_us(150); self._s4(0x30); self._s4(0x20)
        self._cmd(0x28); self._cmd(0x0C); self._cmd(0x06); self._cmd(0x01); time.sleep_ms(2)
    def write_line(self, row, text):
        self._cmd(0x80 | (0x40 if row else 0))
        for ch in '{:<16}'.format(str(text)[:16]): self._cmd(ord(ch), 1)
    def cursor(self, row, col):
        self._cmd(0x80 | (0x40 if row else 0) | col)
_lcd = _LCD(_i2c1, 0x27)
def _lcd_init(): pass
def _lcd_clear():
    _lcd._cmd(0x01); time.sleep_ms(2)
def _lcd_write(row, text): _lcd.write_line(row, text)
`;

generator.forBlock['lcd_init'] = function (_block, g) {
  g.addImport('lcd_defs', _LCD_DEFS);
  return '_lcd_init()\n';
};

generator.forBlock['lcd_clear'] = function (_block, g) {
  g.addImport('lcd_defs', _LCD_DEFS);
  return '_lcd_clear()\n';
};

generator.forBlock['lcd_write'] = function (block, g) {
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

// ─── AHT20 + BMP280 I2C0 blocks ──────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'aht_bmp_init',
    message0: 'AHT20+BMP280 init (SDA=GP13 SCL=GP14)',
    previousStatement: null,
    nextStatement: null,
    colour: 120,
    tooltip: 'Initialise AHT20 and BMP280 over I2C0 (SDA=GP13, SCL=GP14). Call once in setup before any readings.',
  },
  {
    type: 'aht_measure',
    message0: 'AHT20 measure',
    previousStatement: null,
    nextStatement: null,
    colour: 120,
    tooltip: 'Trigger one AHT20 measurement. Call before reading temperature or humidity.',
  },
  {
    type: 'aht_temp',
    message0: 'AHT20 temperature',
    output: 'Number',
    colour: 120,
    tooltip: 'Return temperature in °C from the last AHT20 measurement.',
  },
  {
    type: 'aht_hum',
    message0: 'AHT20 humidity',
    output: 'Number',
    colour: 120,
    tooltip: 'Return relative humidity in % from the last AHT20 measurement.',
  },
  {
    type: 'bmp_measure',
    message0: 'BMP280 measure',
    previousStatement: null,
    nextStatement: null,
    colour: 160,
    tooltip: 'Read BMP280 ADC values and apply Bosch compensation. Call before reading temperature or pressure.',
  },
  {
    type: 'bmp_temp',
    message0: 'BMP280 temperature',
    output: 'Number',
    colour: 160,
    tooltip: 'Return temperature in °C from the last BMP280 measurement.',
  },
  {
    type: 'bmp_pres',
    message0: 'BMP280 pressure (hPa)',
    output: 'Number',
    colour: 160,
    tooltip: 'Return barometric pressure in hPa from the last BMP280 measurement.',
  },
]);

var _AHT_BMP_DEFS = `
from machine import I2C, Pin
import time
_i2c0 = I2C(0, sda=Pin(13), scl=Pin(14), freq=400000)
_AHT_ADDR = 0x38
_BMP_ADDR = 0x77
_bmp_cal = None
_aht_t = 0.0
_aht_h = 0.0
_bmp_t = 0.0
_bmp_p = 0.0
def _u16(b, i): return b[i] | (b[i + 1] << 8)
def _s16(b, i):
    v = _u16(b, i)
    return v - 65536 if v > 32767 else v
def _aht_bmp_init():
    global _bmp_cal
    _i2c0.writeto(_AHT_ADDR, bytes([0xBE, 0x08, 0x00]))
    time.sleep_ms(20)
    _i2c0.writeto_mem(_BMP_ADDR, 0xF4, bytes([0xB7]))
    time.sleep_ms(10)
    c = _i2c0.readfrom_mem(_BMP_ADDR, 0x88, 24)
    _bmp_cal = (_u16(c,0),_s16(c,2),_s16(c,4),_u16(c,6),_s16(c,8),_s16(c,10),_s16(c,12),_s16(c,14),_s16(c,16),_u16(c,18),_s16(c,20),_s16(c,22))
def _aht_measure():
    global _aht_t, _aht_h
    _i2c0.writeto(_AHT_ADDR, bytes([0xAC, 0x33, 0x00]))
    time.sleep_ms(80)
    d = _i2c0.readfrom(_AHT_ADDR, 6)
    rh = (d[1] << 12) | (d[2] << 4) | (d[3] >> 4)
    rt = ((d[3] & 0x0F) << 16) | (d[4] << 8) | d[5]
    _aht_t = round(rt / 1048576.0 * 200.0 - 50.0, 1)
    _aht_h = round(rh / 1048576.0 * 100.0, 1)
def _aht_temp(): return _aht_t
def _aht_hum(): return _aht_h
def _bmp_measure():
    global _bmp_t, _bmp_p
    cal = _bmp_cal
    r = _i2c0.readfrom_mem(_BMP_ADDR, 0xF7, 6)
    ap = (r[0] << 12) | (r[1] << 4) | (r[2] >> 4)
    at = (r[3] << 12) | (r[4] << 4) | (r[5] >> 4)
    v1 = (at / 16384.0 - cal[0] / 1024.0) * cal[1]
    v2 = ((at / 131072.0 - cal[0] / 8192.0) ** 2) * cal[2]
    tf = v1 + v2
    _bmp_t = round(tf / 5120.0, 1)
    v1 = tf / 2.0 - 64000.0
    v2 = v1 * v1 * cal[8] / 32768.0 + v1 * cal[7] * 2.0
    v2 = v2 / 4.0 + cal[6] * 65536.0
    v1 = (cal[5] * v1 * v1 / 524288.0 + cal[4] * v1) / 524288.0
    v1 = (1.0 + v1 / 32768.0) * cal[3]
    if v1 != 0:
        p = 1048576.0 - ap
        p = (p - v2 / 4096.0) * 6250.0 / v1
        v1 = cal[11] * p * p / 2147483648.0
        v2 = p * cal[10] / 32768.0
        _bmp_p = round((p + (v1 + v2 + cal[9]) / 16.0) / 100.0, 1)
def _bmp_temp(): return _bmp_t
def _bmp_pres(): return _bmp_p
`;

generator.forBlock['aht_bmp_init'] = function (_block, g) {
  g.addImport('aht_bmp_defs', _AHT_BMP_DEFS);
  return '_aht_bmp_init()\n';
};

generator.forBlock['aht_measure'] = function (_block, g) {
  g.addImport('aht_bmp_defs', _AHT_BMP_DEFS);
  return '_aht_measure()\n';
};

generator.forBlock['aht_temp'] = function (_block, g) {
  g.addImport('aht_bmp_defs', _AHT_BMP_DEFS);
  return ['_aht_temp()', Order.ATOMIC];
};

generator.forBlock['aht_hum'] = function (_block, g) {
  g.addImport('aht_bmp_defs', _AHT_BMP_DEFS);
  return ['_aht_hum()', Order.ATOMIC];
};

generator.forBlock['bmp_measure'] = function (_block, g) {
  g.addImport('aht_bmp_defs', _AHT_BMP_DEFS);
  return '_bmp_measure()\n';
};

generator.forBlock['bmp_temp'] = function (_block, g) {
  g.addImport('aht_bmp_defs', _AHT_BMP_DEFS);
  return ['_bmp_temp()', Order.ATOMIC];
};

generator.forBlock['bmp_pres'] = function (_block, g) {
  g.addImport('aht_bmp_defs', _AHT_BMP_DEFS);
  return ['_bmp_pres()', Order.ATOMIC];
};

addCategory({
  name: 'AHT20+BMP280',
  colour: '#2e7d32',
  blocks: ['aht_bmp_init', 'aht_measure', 'aht_temp', 'aht_hum', 'bmp_measure', 'bmp_temp', 'bmp_pres'],
});

// ─── Joystick (VRX=GP1, VRY=GP2, SW=GP4) ────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'joy_init',
    message0: 'Joystick init  VRX=GP1  VRY=GP2  SW=GP4',
    previousStatement: null,
    nextStatement: null,
    colour: 260,
    tooltip: 'Initialize PS joystick: GP1=X-axis ADC, GP2=Y-axis ADC, GP4=button. Call once in setup.',
  },
  {
    type: 'joy_x',
    message0: 'Joystick X (0–65535)',
    output: 'Number',
    colour: 260,
    tooltip: 'Raw ADC value of joystick X-axis. Center ≈ 32767, left < 20000, right > 45000.',
  },
  {
    type: 'joy_y',
    message0: 'Joystick Y (0–65535)',
    output: 'Number',
    colour: 260,
    tooltip: 'Raw ADC value of joystick Y-axis. Center ≈ 32767, up < 20000, down > 45000.',
  },
  {
    type: 'joy_button',
    message0: 'Joystick button pressed',
    output: 'Boolean',
    colour: 260,
    tooltip: 'True when the joystick button is pressed (active-low with pull-up).',
  },
  {
    type: 'joy_is_left',
    message0: 'Joystick ← left',
    output: 'Boolean',
    colour: 260,
    tooltip: 'True when joystick is pushed left (X < 20000).',
  },
  {
    type: 'joy_is_right',
    message0: 'Joystick → right',
    output: 'Boolean',
    colour: 260,
    tooltip: 'True when joystick is pushed right (X > 45000).',
  },
  {
    type: 'joy_is_up',
    message0: 'Joystick ↑ up',
    output: 'Boolean',
    colour: 260,
    tooltip: 'True when joystick is pushed up (Y < 20000).',
  },
  {
    type: 'joy_is_down',
    message0: 'Joystick ↓ down',
    output: 'Boolean',
    colour: 260,
    tooltip: 'True when joystick is pushed down (Y > 45000).',
  },
]);

var _JOY_DEFS = `
from machine import Pin, ADC
_vrx = ADC(Pin(1), atten=ADC.ATTN_11DB)
_vry = ADC(Pin(2), atten=ADC.ATTN_11DB)
_sw = Pin(4, Pin.IN, Pin.PULL_UP)
def joy_init(): pass
def joy_x(): return _vrx.read_u16()
def joy_y(): return _vry.read_u16()
def joy_button(): return not _sw.value()
def joy_is_left(): return _vrx.read_u16() < 20000
def joy_is_right(): return _vrx.read_u16() > 45000
def joy_is_up(): return _vry.read_u16() < 20000
def joy_is_down(): return _vry.read_u16() > 45000
`;

generator.forBlock['joy_init'] = function (_block, g) {
  g.addImport('joy_defs', _JOY_DEFS);
  return 'joy_init()\n';
};
generator.forBlock['joy_x'] = function (_block, g) {
  g.addImport('joy_defs', _JOY_DEFS);
  return ['joy_x()', Order.ATOMIC];
};
generator.forBlock['joy_y'] = function (_block, g) {
  g.addImport('joy_defs', _JOY_DEFS);
  return ['joy_y()', Order.ATOMIC];
};
generator.forBlock['joy_button'] = function (_block, g) {
  g.addImport('joy_defs', _JOY_DEFS);
  return ['joy_button()', Order.ATOMIC];
};
generator.forBlock['joy_is_left'] = function (_block, g) {
  g.addImport('joy_defs', _JOY_DEFS);
  return ['joy_is_left()', Order.ATOMIC];
};
generator.forBlock['joy_is_right'] = function (_block, g) {
  g.addImport('joy_defs', _JOY_DEFS);
  return ['joy_is_right()', Order.ATOMIC];
};
generator.forBlock['joy_is_up'] = function (_block, g) {
  g.addImport('joy_defs', _JOY_DEFS);
  return ['joy_is_up()', Order.ATOMIC];
};
generator.forBlock['joy_is_down'] = function (_block, g) {
  g.addImport('joy_defs', _JOY_DEFS);
  return ['joy_is_down()', Order.ATOMIC];
};

addCategory({
  name: 'Joystick',
  colour: '#6a1b9a',
  blocks: ['joy_init', 'joy_x', 'joy_y', 'joy_button', 'joy_is_left', 'joy_is_right', 'joy_is_up', 'joy_is_down'],
});

// ─── LCD Text Editor ─────────────────────────────────────────────────────────

Blockly.defineBlocksWithJsonArray([
  {
    type: 'editor_init',
    message0: 'Canvas editor init',
    previousStatement: null,
    nextStatement: null,
    colour: 330,
    tooltip: 'Initialize 2×16 LCD canvas editor with blinking cursor. Requires joy_init and lcd_init. Call once in setup.',
  },
  {
    type: 'editor_update',
    message0: 'Canvas editor update',
    previousStatement: null,
    nextStatement: null,
    colour: 330,
    tooltip: 'Process joystick: L/R moves column, U/D moves row, button toggles # at cursor. Call every loop.',
  },
  {
    type: 'editor_get_text',
    message0: 'Canvas text',
    output: 'String',
    colour: 330,
    tooltip: 'Returns both canvas rows as a string: "row0 | row1" (trimmed).',
  },
]);

var _EDITOR_DEFS = `
_buf = [[' ']*16, [' ']*16]
_row = 0; _col = 0; _btn = False; _ms_x = 0; _ms_y = 0
_cx = 32768; _cy = 32768; _DEAD = 12000
def _calibrate():
    global _cx, _cy
    xs = [_vrx.read_u16() for _ in range(8)]
    ys = [_vry.read_u16() for _ in range(8)]
    _cx = sum(xs) // 8
    _cy = sum(ys) // 8
def _show():
    _lcd.write_line(0, ''.join(_buf[0]))
    _lcd.write_line(1, ''.join(_buf[1]))
    _lcd.cursor(_row, _col)
def editor_init():
    global _buf, _row, _col, _ms_x, _ms_y
    _lcd._cmd(0x0F)
    _calibrate()
    _buf = [[' ']*16, [' ']*16]; _row = 0; _col = 0; _ms_x = 0; _ms_y = 0
    _show()
def editor_update():
    global _row, _col, _btn, _ms_x, _ms_y
    btn = not _sw.value()
    if btn and not _btn:
        _buf[_row][_col] = ' ' if _buf[_row][_col] != ' ' else '#'
        _show()
    _btn = btn
    now = time.ticks_ms(); moved = False
    x = _vrx.read_u16(); y = _vry.read_u16()
    dx = abs(x - _cx);   dy = abs(y - _cy)
    if dx >= dy:
        if time.ticks_diff(now, _ms_x) >= 200:
            if x < _cx - _DEAD and _col > 0:
                _col -= 1; _ms_x = now; moved = True
            elif x > _cx + _DEAD and _col < 15:
                _col += 1; _ms_x = now; moved = True
    else:
        if time.ticks_diff(now, _ms_y) >= 200:
            if y < _cy - _DEAD and _row > 0:
                _row -= 1; _ms_y = now; moved = True
            elif y > _cy + _DEAD and _row < 1:
                _row += 1; _ms_y = now; moved = True
    if moved: _lcd.cursor(_row, _col)
def editor_get_text(): return ''.join(_buf[0]).rstrip() + ' | ' + ''.join(_buf[1]).rstrip()
`;

generator.forBlock['editor_init'] = function (_block, g) {
  g.addImport('lcd_defs', _LCD_DEFS);
  g.addImport('joy_defs', _JOY_DEFS);
  g.addImport('editor_defs', _EDITOR_DEFS);
  return 'editor_init()\n';
};
generator.forBlock['editor_update'] = function (_block, g) {
  g.addImport('lcd_defs', _LCD_DEFS);
  g.addImport('joy_defs', _JOY_DEFS);
  g.addImport('editor_defs', _EDITOR_DEFS);
  return 'editor_update()\n';
};
generator.forBlock['editor_get_text'] = function (_block, g) {
  g.addImport('lcd_defs', _LCD_DEFS);
  g.addImport('joy_defs', _JOY_DEFS);
  g.addImport('editor_defs', _EDITOR_DEFS);
  return ['editor_get_text()', Order.ATOMIC];
};

addCategory({
  name: 'LCD Editor',
  colour: '#ad1457',
  blocks: ['editor_init', 'editor_update', 'editor_get_text'],
});
