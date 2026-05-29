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
        self._cmd(0x28); self._cmd(0x0E); self._cmd(0x06); self._cmd(0x01); time.sleep_ms(2)
    def write_line(self,row,text):
        self._cmd(0x80|(0x40 if row else 0))
        for ch in '{:<16}'.format(str(text)[:16]): self._cmd(ord(ch),1)

_lcd = _LCD(_i2c1, 0x27)
_vrx = ADC(Pin(1), atten=ADC.ATTN_11DB)
_vry = ADC(Pin(2), atten=ADC.ATTN_11DB)
_sw = Pin(4, Pin.IN, Pin.PULL_UP)

_CHARS = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:()'
_ed_buf = [' '] * 16
_ed_pos = 0; _ed_ci = 0; _ed_btn = False; _ed_ms = 0

def _ed_dir():
    x = _vrx.read_u16(); y = _vry.read_u16()
    if x < 20000: return 'L'
    if x > 45000: return 'R'
    if y < 20000: return 'U'
    if y > 45000: return 'D'
    return None

def _ed_show():
    _lcd.write_line(0, ''.join(_ed_buf))
    _lcd.write_line(1, 'Col:{:02d} [{:s}]     '.format(_ed_pos + 1, _CHARS[_ed_ci]))

def editor_init():
    global _ed_buf, _ed_pos, _ed_ci
    _ed_buf = [' '] * 16; _ed_pos = 0; _ed_ci = 0
    _ed_show()

def editor_update():
    global _ed_pos, _ed_ci, _ed_btn, _ed_ms
    btn = not _sw.value()
    if btn and not _ed_btn:
        txt = ''.join(_ed_buf).rstrip()
        print('>>', txt)
        _lcd.write_line(1, '>> ' + txt[:13])
        time.sleep_ms(800); _ed_show()
    _ed_btn = btn
    now = time.ticks_ms()
    if time.ticks_diff(now, _ed_ms) < 180: return
    d = _ed_dir()
    if not d: return
    _ed_ms = now
    if d == 'L' and _ed_pos > 0:
        _ed_pos -= 1; _ed_ci = _CHARS.index(_ed_buf[_ed_pos]) if _ed_buf[_ed_pos] in _CHARS else 0
    elif d == 'R' and _ed_pos < 15:
        _ed_pos += 1; _ed_ci = _CHARS.index(_ed_buf[_ed_pos]) if _ed_buf[_ed_pos] in _CHARS else 0
    elif d == 'U':
        _ed_ci = (_ed_ci - 1) % len(_CHARS); _ed_buf[_ed_pos] = _CHARS[_ed_ci]
    elif d == 'D':
        _ed_ci = (_ed_ci + 1) % len(_CHARS); _ed_buf[_ed_pos] = _CHARS[_ed_ci]
    _ed_show()

def editor_get_text(): return ''.join(_ed_buf).rstrip()

def setup():
    editor_init()
    print('Joystick+LCD1602 Text Editor')
    print('GP1=VRX  GP2=VRY  GP4=SW | LCD I2C1: SDA=GP33 SCL=GP34')
    print('L/R: move cursor  U/D: change char  BTN: print text')

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
