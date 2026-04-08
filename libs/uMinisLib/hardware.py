"""
hardware.py — Hardware drivers with Minis/M5Stack-compatible API.

Provides Button, RGB, IR and Minis classes.  Minis is a facade that
mirrors the M5Stack M5 module interface (begin / update / BtnA / BtnB)
so Blockly-generated code works on raw ESP32-S3 targets without changes.
"""
from machine import Pin, ADC, PWM, UART, I2C, WDT, SPI, I2S  # noqa: F401 — re-exported for `from hardware import Pin/ADC/PWM/UART/I2C/WDT/SPI/I2S`
try:
    from machine import CAN as _CAN_machine  # noqa: F401 — re-exported as CAN below
    CAN = _CAN_machine
except ImportError:
    CAN = None  # type: ignore  — CAN not available on this target
import time

# ---------------------------------------------------------------------------
# RGB — WS2812 NeoPixel LEDs
# ---------------------------------------------------------------------------

try:
    import neopixel as _neopixel
    _HAS_NEOPIXEL = True
except ImportError:
    _HAS_NEOPIXEL = False


class RGB:
    """
    WS2812 NeoPixel LED strip with M5Stack-compatible API.

    Usage::

        rgb = RGB()                        # built-in LED (pin 48, 1 LED)
        rgb = RGB(pin=5, n=8)             # custom strip
        rgb.set_color(0, 0xff0000)        # set LED 0 to red
        rgb.fill_color(0x0000ff)          # fill all blue
        rgb.set_brightness(50)            # 50%
    """

    def __init__(self, pin=None, n=1):
        if pin is None:
            pin = 48  # ESP32-S3 DevKit built-in WS2812
        self._n = n
        if _HAS_NEOPIXEL:
            self._np = _neopixel.NeoPixel(Pin(pin), n)
        else:
            self._np = None
        self._brightness = 100  # percent 0-100

    def _apply(self, index, color_int):
        if not self._np:
            return
        f = self._brightness / 100.0
        r = int(((color_int >> 16) & 0xFF) * f)
        g = int(((color_int >> 8) & 0xFF) * f)
        b = int((color_int & 0xFF) * f)
        self._np[index] = (r, g, b)
        self._np.write()

    def set_color(self, index, color):
        """Set LED at *index* to *color* (0xRRGGBB integer)."""
        self._apply(index, color)

    def fill_color(self, color):
        """Set all LEDs to *color* (0xRRGGBB integer)."""
        if not self._np:
            return
        f = self._brightness / 100.0
        r = int(((color >> 16) & 0xFF) * f)
        g = int(((color >> 8) & 0xFF) * f)
        b = int((color & 0xFF) * f)
        for i in range(self._n):
            self._np[i] = (r, g, b)
        self._np.write()

    def set_brightness(self, brightness):
        """Set brightness level 0-100 (percent)."""
        self._brightness = max(0, min(100, int(brightness)))


# ---------------------------------------------------------------------------
# IR — NEC infrared transmitter
# ---------------------------------------------------------------------------

try:
    import esp32 as _esp32
    _HAS_RMT = True
except ImportError:
    _HAS_RMT = False


class IR:
    """
    NEC IR transmitter with M5Stack-compatible API.

    Uses the ESP32 RMT peripheral when available for accurate timing,
    otherwise falls back to bit-banging (no 38 kHz carrier — for testing
    only; real IR receivers require the carrier).

    Usage::

        ir = IR()             # default pin 12
        ir = IR(pin=4)
        ir.tx(0x00, 0xAA)    # send NEC frame: addr=0, cmd=0xAA
    """

    _T = 562  # NEC base unit in μs

    def __init__(self, pin=None):
        if pin is None:
            pin = 12
        self._pin_num = pin
        if _HAS_RMT:
            # clock_div=80 → 1 μs per tick (80 MHz / 80)
            try:
                self._rmt = _esp32.RMT(0, pin=Pin(pin), clock_div=80, idle_level=False)
                self._mode = 'rmt'
            except Exception:
                self._mode = 'bitbang'
                self._pin = Pin(pin, Pin.OUT, value=0)
        else:
            self._mode = 'bitbang'
            self._pin = Pin(pin, Pin.OUT, value=0)

    def tx(self, addr, data):
        """Send an NEC IR frame (8-bit address + 8-bit command)."""
        durations = self._build_nec(addr & 0xFF, data & 0xFF)
        if self._mode == 'rmt':
            self._rmt.write_pulses(durations, start=True)
        else:
            self._tx_bitbang(durations)

    # ------------------------------------------------------------------
    def _build_nec(self, addr, data):
        T = self._T
        d = [9000, 4500]  # leader burst + space
        for byte in (addr, (~addr) & 0xFF, data, (~data) & 0xFF):
            for i in range(8):
                d.append(T)
                d.append(T * 3 if (byte >> i) & 1 else T)
        d.append(T)  # final burst
        return d

    def _tx_bitbang(self, durations):
        level = 1
        p = self._pin
        for d in durations:
            p(level)
            time.sleep_us(d)
            level ^= 1
        p(0)


class Button:
    """
    Tick-based button driver for raw ESP32 MicroPython.

    API is compatible with M5Stack hardware.Button so the same Blockly
    code generation works for both M5Stack and raw ESP32 targets.

    Usage::

        btn = Button(0, active_low=True, pullup_active=True)
        btn.setCallback(type=Button.CB_TYPE.WAS_CLICKED, cb=my_cb)

        # in main loop:
        btn.tick(None)
        if btn.wasClicked():
            print("clicked!")

    Callback signature::

        def my_cb(state):   # state == CB_TYPE value that triggered
            pass
    """

    class CB_TYPE:
        WAS_CLICKED = 0
        WAS_HOLD = 1
        WAS_DOUBLE_CLICK = 2
        WAS_RELEASED = 3

    # Timing constants (ms)
    HOLD_DURATION_MS = 500
    DOUBLE_CLICK_MS = 300

    def __init__(self, pin, active_low=True, pullup_active=False):
        pull = Pin.PULL_UP if pullup_active else None
        self._pin = Pin(pin, Pin.IN, pull)
        self._active_low = active_low

        self._is_pressed = False
        self._prev_raw = False
        self._press_time = 0
        self._hold_fired = False

        self._click_count = 0
        self._last_click_time = 0

        self._was_clicked = False
        self._was_hold = False
        self._was_double_click = False
        self._was_released = False

        self._callbacks = {}

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _raw(self):
        v = self._pin.value()
        return (v == 0) if self._active_low else bool(v)

    def _fire(self, cb_type):
        cb = self._callbacks.get(cb_type)
        if cb:
            try:
                cb(cb_type)
            except Exception as e:
                print('Button cb error:', e)

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def tick(self, _):
        """Update button state — call once per loop iteration."""
        now = time.ticks_ms()
        raw = self._raw()

        # Reset single-tick flags
        self._was_clicked = False
        self._was_hold = False
        self._was_double_click = False
        self._was_released = False

        if raw and not self._prev_raw:
            # Rising edge: button pressed
            self._is_pressed = True
            self._press_time = now
            self._hold_fired = False

        elif not raw and self._prev_raw:
            # Falling edge: button released
            self._is_pressed = False
            self._was_released = True
            self._fire(self.CB_TYPE.WAS_RELEASED)

            duration = time.ticks_diff(now, self._press_time)
            if duration < self.HOLD_DURATION_MS:
                # Short press: check for double click
                if (self._click_count == 1 and
                        time.ticks_diff(now, self._last_click_time) < self.DOUBLE_CLICK_MS):
                    self._was_double_click = True
                    self._click_count = 0
                    self._fire(self.CB_TYPE.WAS_DOUBLE_CLICK)
                else:
                    self._click_count = 1
                    self._last_click_time = now
                    self._was_clicked = True
                    self._fire(self.CB_TYPE.WAS_CLICKED)

        elif raw and self._prev_raw:
            # Still held — fire hold once when threshold is reached
            if (not self._hold_fired and
                    time.ticks_diff(now, self._press_time) >= self.HOLD_DURATION_MS):
                self._hold_fired = True
                self._was_hold = True
                self._fire(self.CB_TYPE.WAS_HOLD)

        # Expire double-click window
        if (self._click_count == 1 and
                time.ticks_diff(now, self._last_click_time) >= self.DOUBLE_CLICK_MS):
            self._click_count = 0

        self._prev_raw = raw

    def setCallback(self, type=None, cb=None):
        """Register a callback for the given CB_TYPE event."""
        if type is not None and cb is not None:
            self._callbacks[type] = cb

    def isPressed(self):
        """True while the button is held down."""
        return self._is_pressed

    def isReleased(self):
        """True while the button is not pressed."""
        return not self._is_pressed

    def wasClicked(self):
        """True for one tick after a short press-and-release."""
        return self._was_clicked

    def wasHold(self):
        """True for one tick when the hold threshold is reached."""
        return self._was_hold

    def wasDoubleClick(self):
        """True for one tick after two quick presses."""
        return self._was_double_click


# ---------------------------------------------------------------------------
# Minis — M5Stack-compatible facade for raw ESP32-S3
# ---------------------------------------------------------------------------

class _Minis:
    """
    Minis hardware facade with M5Stack-compatible API.

    Mirrors the M5Stack ``M5`` module so Blockly-generated code is
    portable between M5Stack and raw ESP32-S3 targets.

    Usage::

        from hardware import Minis

        # in setup:
        Minis.begin()                          # default: BtnA=GPIO 0
        Minis.begin(btnA_pin=0, btnB_pin=14)   # custom pins

        # in loop:
        Minis.update()

        # read state:
        if Minis.BtnA.wasClicked():
            print("clicked!")

        # callbacks:
        Minis.BtnA.setCallback(type=Minis.BtnA.CB_TYPE.WAS_CLICKED, cb=my_cb)
    """

    def __init__(self):
        self.BtnA = None
        self.BtnB = None

    def begin(self, btnA_pin=0, btnB_pin=None):
        """Initialize Minis hardware.

        Creates BtnA (and optionally BtnB) as tick-based Button instances.
        BtnA defaults to GPIO 0 (the BOOT button on most ESP32-S3 DevKits,
        active-low with internal pull-up).
        """
        self.BtnA = Button(btnA_pin, active_low=True, pullup_active=True)
        if btnB_pin is not None:
            self.BtnB = Button(btnB_pin, active_low=True, pullup_active=True)

    def update(self):
        """Poll all buttons — call once per loop iteration."""
        if self.BtnA is not None:
            self.BtnA.tick(None)
        if self.BtnB is not None:
            self.BtnB.tick(None)


Minis = _Minis()


# ---------------------------------------------------------------------------
# Speaker — PWM buzzer with M5Stack-compatible API
# ---------------------------------------------------------------------------

class _Speaker:
    """
    PWM buzzer with a subset of the M5Stack Speaker API.

    Only ``tone()``, ``begin()``, ``end()``, ``stop()``, ``setVolume()``,
    ``setVolumePercentage()``, and ``isRunning()`` are implemented via the
    MicroPython PWM peripheral.  Methods that require I2S hardware (playWav,
    playRaw, etc.) raise ``NotImplementedError`` on plain ESP32-S3 targets
    without I2S.  Override this class or provide your own Speaker instance
    when I2S is needed.

    Usage::

        from hardware import Speaker

        Speaker.begin()
        Speaker.tone(440, 500)   # 440 Hz for 500 ms
        Speaker.setVolume(128)
        Speaker.end()
    """

    DEFAULT_PIN = 46  # common buzzer pin on ESP32-S3 boards

    def __init__(self):
        self._pin_num = self.DEFAULT_PIN
        self._volume = 128      # 0-255
        self._pwm = None        # PWM instance while playing
        self._enabled = False

    # ------------------------------------------------------------------
    def config(self, **kwargs):
        """Configure speaker hardware.  Supports: pin_data_out, buzzer."""
        if 'pin_data_out' in kwargs:
            self._pin_num = int(kwargs['pin_data_out'])
        # 'buzzer' key acknowledged but ignored (always buzzer mode here)

    def begin(self):
        """Enable the speaker.  Returns True on success."""
        self._enabled = True
        return True

    def end(self):
        """Disable the speaker and free PWM resources."""
        self.stop()
        self._enabled = False

    def stop(self):
        """Stop any currently playing tone."""
        if self._pwm is not None:
            try:
                self._pwm.deinit()
            except Exception:
                pass
            self._pwm = None

    # ------------------------------------------------------------------
    def isRunning(self):
        """True while a tone is actively playing."""
        return self._pwm is not None

    def isEnabled(self):
        """True after begin() and before end()."""
        return self._enabled

    def isPlaying(self):
        """Alias for isRunning()."""
        return self.isRunning()

    # ------------------------------------------------------------------
    def setVolume(self, vol):
        """Set volume 0-255 (maps to PWM duty cycle)."""
        self._volume = max(0, min(255, int(vol)))

    def setVolumePercentage(self, pct):
        """Set volume as a fraction 0.0–1.0."""
        self.setVolume(int(pct * 255))

    def getVolume(self):
        """Return current volume 0-255."""
        return self._volume

    def getVolumePercentage(self):
        """Return volume as a float 0.0–1.0."""
        return self._volume / 255.0

    def setAllChannelVolume(self, vol):
        """Set volume on all channels (single-channel fallback)."""
        self.setVolume(vol)

    def setChannelVolume(self, _ch, vol):
        """Set volume on a specific channel (single-channel fallback)."""
        self.setVolume(vol)

    def getPlayingChannels(self):
        """Return number of active channels (0 or 1)."""
        return 1 if self.isRunning() else 0

    def getChannelVolume(self, _ch):
        """Return volume for a channel (same as getVolume)."""
        return self._volume

    # ------------------------------------------------------------------
    def tone(self, freq, ms=0):
        """Play a tone at *freq* Hz for *ms* milliseconds (0 = continuous)."""
        self.stop()
        if not self._enabled or freq <= 0:
            return
        duty = (self._volume * 512) // 255  # scale 0-255 → 0-512 (10-bit)
        try:
            self._pwm = PWM(Pin(self._pin_num), freq=int(freq), duty=duty)
            if ms > 0:
                time.sleep_ms(int(ms))
                self.stop()
        except Exception as e:
            print('Speaker tone error:', e)
            self._pwm = None

    # ------------------------------------------------------------------
    def playWav(self, _buf):
        raise NotImplementedError('playWav requires I2S — not available on this target')

    def playRaw(self, _buf, _rate=0):
        raise NotImplementedError('playRaw requires I2S — not available on this target')

    def playWavFile(self, _path):
        raise NotImplementedError('playWavFile requires I2S — not available on this target')


Speaker = _Speaker()


# ---------------------------------------------------------------------------
# UserDisplay — SPI display panel with M5Stack-compatible API
# ---------------------------------------------------------------------------

class UserDisplay:
    """
    SPI display driver wrapper with a UIFlow2/M5Stack-compatible constructor.

    Supports ILI9341/ILI9342, ST7789 and GC9A01 panels via the MicroPython
    ``ili9XXX`` or ``st7789`` community drivers.  Falls back gracefully when
    no display driver is installed.

    Usage::

        from hardware import UserDisplay

        display = UserDisplay(
            panel=UserDisplay.PANEL.ILI9342,
            w=320, h=240, ox=0, oy=0,
            invert=True, rgb=False,
            spi_host=2, spi_freq=40, spi_mode=0,
            sclk=18, mosi=23, miso=-1, dc=15, cs=5, rst=-1, busy=-1,
            bl=32, bl_invert=False, bl_pwm_freq=1, bl_pwm_chn=0,
        )
        display.fill(0x0000)   # clear to black
    """

    class PANEL:
        ILI9342 = 'ILI9342'
        ILI9341 = 'ILI9341'
        ST7789_240 = 'ST7789_240'
        ST7789_135 = 'ST7789_135'
        GC9A01 = 'GC9A01'

    def __init__(
        self,
        panel=None,
        w=320, h=240, ox=0, oy=0,
        invert=False, rgb=True,
        spi_host=2, spi_freq=40, spi_mode=0,
        sclk=-1, mosi=-1, miso=-1, dc=-1, cs=-1, rst=-1, busy=-1,
        bl=-1, bl_invert=False, bl_pwm_freq=1, bl_pwm_chn=0,
    ):
        from machine import SPI, Pin as _Pin  # noqa: F811
        self._w = w
        self._h = h
        self._driver = None

        # Build SPI bus
        spi_kwargs = dict(baudrate=spi_freq * 1_000_000, polarity=(spi_mode >> 1) & 1, phase=spi_mode & 1)
        if sclk >= 0:
            spi_kwargs['sck'] = _Pin(sclk)
        if mosi >= 0:
            spi_kwargs['mosi'] = _Pin(mosi)
        if miso >= 0:
            spi_kwargs['miso'] = _Pin(miso)
        spi = SPI(spi_host, **spi_kwargs)

        dc_pin = _Pin(dc, _Pin.OUT) if dc >= 0 else None
        cs_pin = _Pin(cs, _Pin.OUT) if cs >= 0 else None
        rst_pin = _Pin(rst, _Pin.OUT) if rst >= 0 else None

        # Attempt to load a display driver
        ptype = getattr(panel, '__class__', None)
        panel_name = panel if isinstance(panel, str) else str(panel)

        try:
            if 'ST7789' in panel_name:
                import st7789 as _drv  # type: ignore
                self._driver = _drv.ST7789(spi, w, h, dc=dc_pin, cs=cs_pin, reset=rst_pin,
                                           rotation=0, color_order=_drv.RGB if rgb else _drv.BGR)
            elif 'GC9A01' in panel_name:
                import gc9a01 as _drv  # type: ignore
                self._driver = _drv.GC9A01(spi, w, h, dc=dc_pin, cs=cs_pin, reset=rst_pin)
            else:
                # ILI9341 / ILI9342
                import ili9XXX as _drv  # type: ignore
                self._driver = _drv.ili9XXX(spi=spi_host, width=w, height=h,
                                            dc=dc, cs=cs, rst=rst,
                                            sck=sclk, mosi=mosi, miso=miso,
                                            invert=invert)
        except ImportError:
            # No display driver installed — provide a no-op stub
            self._driver = None

        # Backlight PWM
        if bl >= 0:
            try:
                bl_duty = 0 if bl_invert else 1023
                self._bl_pwm = PWM(_Pin(bl), freq=bl_pwm_freq * 1000, duty=bl_duty)
            except Exception:
                self._bl_pwm = None
        else:
            self._bl_pwm = None

    # ------------------------------------------------------------------
    # Basic drawing API (delegated to the driver when available)
    # ------------------------------------------------------------------

    def fill(self, color):
        """Fill the screen with a 16-bit RGB565 color."""
        if self._driver:
            self._driver.fill(color)

    def pixel(self, x, y, color):
        """Draw a single pixel."""
        if self._driver:
            self._driver.pixel(x, y, color)

    def text(self, txt, x, y, color=0xFFFF, bg=0x0000):
        """Draw a text string at (x, y)."""
        if self._driver:
            try:
                self._driver.text(txt, x, y, color, bg)
            except Exception:
                pass

    def width(self):
        return self._w

    def height(self):
        return self._h


# ---------------------------------------------------------------------------
# RTC — Real-Time Clock with M5Stack-compatible API
# ---------------------------------------------------------------------------

class RTC:
    """
    Real-Time Clock wrapper with UIFlow2/M5Stack-compatible API.

    Wraps ``machine.RTC`` and adds ``local_datetime()`` and ``timezone()``
    helpers not present in standard MicroPython.

    Usage::

        from hardware import RTC

        rtc = RTC()
        rtc.init((2026, 4, 8, 12, 0, 0, 0, 0))   # set UTC time
        rtc.timezone('GMT+2')                      # set timezone offset
        print(rtc.datetime())                      # get UTC tuple
        print(rtc.local_datetime())                # get local time tuple
        year = rtc.datetime()[0]                   # extract year
    """

    def __init__(self):
        from machine import RTC as _RTC
        self._rtc = _RTC()
        self._tz_offset_sec = 0  # timezone offset in seconds

    # ------------------------------------------------------------------

    def init(self, dt):
        """Set UTC time.  dt = (year, month, mday, hour, minute, second, microsecond, 0)."""
        # machine.RTC.datetime() tuple: (year, month, day, weekday, hours, minutes, seconds, subseconds)
        year, month, mday, hour, minute, second, usecond, _ = dt
        self._rtc.datetime((year, month, mday, 0, hour, minute, second, usecond))

    def datetime(self):
        """Return current UTC time as (year, month, day, weekday, hour, minute, second, subsecond)."""
        return self._rtc.datetime()

    def local_datetime(self):
        """Return local time tuple (UTC + timezone offset)."""
        import time as _time
        utc_tuple = self._rtc.datetime()
        # Convert to epoch seconds, apply offset, convert back
        try:
            epoch = _time.mktime((
                utc_tuple[0], utc_tuple[1], utc_tuple[2],
                utc_tuple[4], utc_tuple[5], utc_tuple[6],
                utc_tuple[3], 0,
            ))
            local_epoch = epoch + self._tz_offset_sec
            lt = _time.localtime(local_epoch)
            return (lt[0], lt[1], lt[2], lt[6], lt[3], lt[4], lt[5], 0)
        except Exception:
            return utc_tuple

    def timezone(self, tz=None):
        """Get or set timezone string like 'GMT+2', 'GMT-5', 'GMT0'."""
        if tz is None:
            return self._tz_str if hasattr(self, '_tz_str') else 'GMT0'
        self._tz_str = str(tz)
        # Parse offset: 'GMT+N', 'GMT-N', or 'GMT0'
        s = self._tz_str.replace('GMT', '').strip()
        try:
            self._tz_offset_sec = int(s) * 3600
        except ValueError:
            self._tz_offset_sec = 0


# ---------------------------------------------------------------------------
# sdcard — SD card mount helper with UIFlow2-compatible API
# ---------------------------------------------------------------------------

class _SdCardModule:
    """
    SDCard mount helper.  Mounts the SD card to ``/sd/`` using MicroPython's
    ``machine.SPI`` + community ``sdcard`` driver.

    Usage::

        from hardware import sdcard
        import os

        sdcard.SDCard(slot=2, sck=18, miso=19, mosi=23, cs=5, freq=1000000)
        print(os.listdir('/sd'))
    """

    def SDCard(self, slot=1, width=1, sck=-1, miso=-1, mosi=-1, cs=-1, freq=1000000):
        """Mount SD card at /sd/.  Silently unmounts any existing /sd mount first."""
        import os as _os
        # Unmount silently if already mounted
        try:
            _os.umount('/sd')
        except Exception:
            pass
        try:
            import sdcard as _sd  # community sdcard.py driver  # type: ignore
            spi_kwargs = dict(baudrate=freq)
            if sck >= 0:
                spi_kwargs['sck'] = Pin(sck)
            if miso >= 0:
                spi_kwargs['miso'] = Pin(miso)
            if mosi >= 0:
                spi_kwargs['mosi'] = Pin(mosi)
            _spi = SPI(slot, **spi_kwargs)
            _cs = Pin(cs, Pin.OUT) if cs >= 0 else Pin(5, Pin.OUT)
            sd = _sd.SDCard(_spi, _cs)
            _os.mount(_os.VfsFat(sd), '/sd')
        except ImportError:
            # sdcard.py driver not installed — try machine.SDCard (ESP32 built-in)
            try:
                from machine import SDCard as _MSD  # type: ignore
                sd = _MSD(slot=slot, width=width, sck=sck, miso=miso, mosi=mosi, cs=cs, freq=freq)
                _os.mount(_os.VfsFat(sd), '/sd')
            except Exception as e:
                print('SDCard mount error:', e)


sdcard = _SdCardModule()
