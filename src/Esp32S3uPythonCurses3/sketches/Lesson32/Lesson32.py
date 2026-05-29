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
        self._cmd(0x28); self._cmd(0x0C); self._cmd(0x06); self._cmd(0x01); time.sleep_ms(2)
    def write_line(self,row,text):
        self._cmd(0x80|(0x40 if row else 0))
        for ch in '{:<16}'.format(str(text)[:16]): self._cmd(ord(ch),1)

_lcd = _LCD(_i2c1, 0x27)
_vrx = ADC(Pin(1), atten=ADC.ATTN_11DB)
_vry = ADC(Pin(2), atten=ADC.ATTN_11DB)
_sw  = Pin(4, Pin.IN, Pin.PULL_UP)

_cx = 32768; _cy = 32768; _DEAD = 12000

def _calibrate():
    global _cx, _cy
    xs = [_vrx.read_u16() for _ in range(8)]
    ys = [_vry.read_u16() for _ in range(8)]
    _cx = sum(xs)//8; _cy = sum(ys)//8

FUSE_MS  = 3000
BLAST_MS = 600

_px=0; _py=0; _bx=-1; _by=-1
_bomb_t=0; _blast_t=0
_state=0   # 0=play  1=fuse  2=blast  3=dead
_score=0; _btn=False; _mv_t=0

def _draw():
    if _state == 3:
        _lcd.write_line(0, '   GAME  OVER   ')
        _lcd.write_line(1, ' Score: %-8d' % _score)
        return
    g = [[' ']*16, [' ']*16]
    if _state == 2:
        for dr in range(-1, 2):
            for dc in range(-1, 2):
                r, c = _by+dr, _bx+dc
                if 0<=r<2 and 0<=c<16: g[r][c] = '*'
    elif _state == 1:
        g[_by][_bx] = 'B'
    g[_py][_px] = '@'
    _lcd.write_line(0, ''.join(g[0]))
    _lcd.write_line(1, ''.join(g[1]))

def game_init():
    global _px,_py,_bx,_by,_bomb_t,_blast_t,_state,_score,_btn,_mv_t
    _calibrate()
    _px=0; _py=0; _bx=-1; _by=-1; _bomb_t=0; _blast_t=0
    _state=0; _score=0; _btn=False; _mv_t=0
    _draw()

def game_update():
    global _px,_py,_bx,_by,_bomb_t,_blast_t,_state,_score,_btn,_mv_t
    now = time.ticks_ms()
    btn = not _sw.value()
    if _state == 3:
        if btn and not _btn: game_init()
        _btn = btn; return
    if _state == 1 and time.ticks_diff(now, _bomb_t) >= FUSE_MS:
        _state = 2; _blast_t = now; _draw()
    if _state == 2 and time.ticks_diff(now, _blast_t) >= BLAST_MS:
        if abs(_px-_bx) <= 1 and abs(_py-_by) <= 1:
            _state = 3
        else:
            _score += 1; _state = 0; _bx=-1; _by=-1
        _draw()
    if btn and not _btn and _state == 0:
        _bx=_px; _by=_py; _state=1; _bomb_t=now; _draw()
    _btn = btn
    if time.ticks_diff(now, _mv_t) >= 200:
        x=_vrx.read_u16(); y=_vry.read_u16()
        dx=abs(x-_cx); dy=abs(y-_cy); moved=False
        if dx >= dy and dx > _DEAD:
            if   x < _cx-_DEAD and _px > 0:  _px-=1; moved=True
            elif x > _cx+_DEAD and _px < 15: _px+=1; moved=True
        elif dy > _DEAD:
            if   y < _cy-_DEAD and _py > 0:  _py-=1; moved=True
            elif y > _cy+_DEAD and _py < 1:  _py+=1; moved=True
        if moved: _mv_t=now; _draw()

def game_score(): return _score

def setup():
    game_init()
    print('Bomberman 2x16')
    print('BTN=plant bomb  escape before it blows!')
    print('cal: cx=%d cy=%d dead=%d' % (_cx, _cy, _DEAD))

def loop():
    game_update()
    time.sleep_ms(20)

if __name__ == '__main__':
    try:
        setup()
        while True:
            loop()
    except (Exception, KeyboardInterrupt) as e:
        print(e)
