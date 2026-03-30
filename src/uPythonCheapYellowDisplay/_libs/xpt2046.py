"""
xpt2046.py — MicroPython XPT2046 resistive touchscreen driver
Designed for CheapYellowDisplay (ESP32-2432S028).

GPIO 39 (T_DO) is input-only on ESP32, so SoftSPI is required — hardware SPI
cannot be used for the touch bus.

Typical wiring (CYD):
    touch_spi = machine.SoftSPI(baudrate=1_000_000,
                                sck=machine.Pin(25),
                                mosi=machine.Pin(32),
                                miso=machine.Pin(39))
    touch = XPT2046(touch_spi,
                    cs=machine.Pin(33, machine.Pin.OUT),
                    irq=machine.Pin(36, machine.Pin.IN))

    pos = touch.read()   # returns (x, y) or None
"""

from micropython import const

_CMD_X = const(0x90)   # differential X, 12-bit
_CMD_Y = const(0xD0)   # differential Y, 12-bit

# Default calibration — adjust for your unit with touch_calibrate() helper below.
_CAL_DEFAULT = {'x_min': 300, 'x_max': 3800, 'y_min': 200, 'y_max': 3700}


class XPT2046:
    """
    :param spi:     SoftSPI instance (GPIO 39 is input-only, use SoftSPI)
    :param cs:      Chip-select pin (machine.Pin, OUT)
    :param irq:     Touch-interrupt pin (machine.Pin, IN) — recommended
    :param cal:     Dict with x_min/x_max/y_min/y_max calibration values
    :param size:    (display_width, display_height) in pixels
    :param samples: Samples to average per read (reduces noise, default 4)
    """

    def __init__(self, spi, cs, irq=None, cal=None, size=(320, 240), samples=4):
        self._spi     = spi
        self._cs      = cs
        self._irq     = irq
        self._cal     = cal or dict(_CAL_DEFAULT)
        self._w, self._h = size
        self._samples = samples

    def _read_raw_once(self, cmd):
        buf = bytearray(3)
        self._cs.value(0)
        self._spi.write(bytes([cmd]))
        self._spi.readinto(buf)
        self._cs.value(1)
        return ((buf[0] << 8) | buf[1]) >> 3   # 12-bit

    def _raw(self):
        xs = ys = 0
        for _ in range(self._samples):
            xs += self._read_raw_once(_CMD_X)
            ys += self._read_raw_once(_CMD_Y)
        return xs // self._samples, ys // self._samples

    def touched(self):
        """Return True when the screen is pressed."""
        if self._irq is not None:
            return not self._irq.value()   # IRQ is active-LOW
        rx, ry = self._raw()
        return rx > 200 and ry > 200

    def read(self):
        """
        Return (x, y) in display pixels, or None if not touched.
        Applies linear calibration and clamps to display bounds.
        """
        if not self.touched():
            return None
        rx, ry = self._raw()
        c = self._cal
        x = (rx - c['x_min']) * (self._w - 1) // max(1, c['x_max'] - c['x_min'])
        y = (ry - c['y_min']) * (self._h - 1) // max(1, c['y_max'] - c['y_min'])
        return max(0, min(self._w - 1, x)), max(0, min(self._h - 1, y))

    def calibrate(self, tl_raw, br_raw):
        """
        Update calibration from two corner measurements.

        :param tl_raw: (raw_x, raw_y) for the top-left corner
        :param br_raw: (raw_x, raw_y) for the bottom-right corner
        """
        self._cal['x_min'] = tl_raw[0]
        self._cal['y_min'] = tl_raw[1]
        self._cal['x_max'] = br_raw[0]
        self._cal['y_max'] = br_raw[1]
