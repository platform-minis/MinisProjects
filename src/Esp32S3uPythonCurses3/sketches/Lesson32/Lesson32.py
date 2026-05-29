from machine import Pin, I2C, ADC
import time

_i2c1 = I2C(1, sda=Pin(33), scl=Pin(34), freq=400000)

class _LCD:
    def __init__(self, i2c, addr=0x27):
        self.i2c=i2c; self.addr=addr; self._bl=0x08; self._setup()
    def _wb(self,b): self.i2c.writeto(self.addr,bytes([b|self._bl]))
    def _pulse(self,b): self._wb(b|4); time.sleep_us(1); self._wb(b&~4); time.sleep_us(50)
    def _s4(self,n): self._wb(n); self._pulse(n)
    def _cmd(self,d,m=0): rs=m&1; self._s4((d&0xF0)|rs); self._s4(((d<<4)&0xF0)|rs)
    def _setup(self):
        time.sleep_ms(50); self._s4(0x30); time.sleep_ms(5); self._s4(0x30)
        time.sleep_us(150); self._s4(0x30); self._s4(0x20)
        self._cmd(0x28); self._cmd(0x0F); self._cmd(0x06); self._cmd(0x01); time.sleep_ms(2)
    def write_line(self,row,text):
        self._cmd(0x80|(0x40 if row else 0))
        for ch in '{:<16}'.format(str(text)[:16]): self._cmd(ord(ch),1)
    def cursor(self,row,col):
        self._cmd(0x80|(0x40 if row else 0)|col)

_lcd = _LCD(_i2c1, 0x27)
_vrx = ADC(Pin(1), atten=ADC.ATTN_11DB)
_vry = ADC(Pin(2), atten=ADC.ATTN_11DB)
_sw = Pin(4, Pin.IN, Pin.PULL_UP)

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

def setup():
    editor_init()
    print('Joystick+LCD1602 Canvas Editor')
    print('GP1=VRX  GP2=VRY  GP4=SW | LCD I2C1: SDA=GP33 SCL=GP34')
    print('L/R: col  U/D: row  BTN: place/erase #')
    print('cal: cx=%d cy=%d dead=%d' % (_cx, _cy, _DEAD))

def loop():
    editor_update()
    time.sleep_ms(20)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
