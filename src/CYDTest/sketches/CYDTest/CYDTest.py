"""
CYDTest.py — micro-gui widget demo on Cheap Yellow Display (ESP32-2432S028)

Hardware
  Board:   ESP32-2432S028 (CYD)
  Display: ILI9341 2.8" 320x240 IPS TFT
  Touch:   XPT2046 resistive (not used in this sketch)
  LED:     RGB on GPIO 17/4/16, active-low

Library
  peterhinch/micropython-micro-gui  (nanogui mode — display only)
  https://github.com/peterhinch/micropython-micro-gui

Files required on device
  color_setup.py          — CYD hardware config (same dir as this file)
  drivers/display/ili9341.py
  gui/core/nanogui.py
  gui/core/writer.py
  gui/widgets/label.py
  gui/fonts/freesans20.py
"""

from color_setup import ssd
from gui.core.nanogui import refresh
from gui.core.writer import CWriter
from gui.widgets.label import Label
import gui.fonts.freesans20 as font

# ── Init ─────────────────────────────────────────────────────────────────────

refresh(ssd, True)   # first call: initialise framebuf and blank the screen

# Writer binds a font to the display
wri = CWriter(ssd, font, fgcolor=ssd.WHITE, bgcolor=ssd.BLACK, verbose=False)
CWriter.set_textpos(ssd, 0, 0)

# ── Widgets ───────────────────────────────────────────────────────────────────

Label(wri, row=10,  col=10, text='CYDTest',          fgcolor=ssd.CYAN)
Label(wri, row=55,  col=10, text='micro-gui ready',  fgcolor=ssd.GREEN)
Label(wri, row=100, col=10, text='ESP32-2432S028',   fgcolor=ssd.WHITE)
Label(wri, row=140, col=10, text='ILI9341  320x240', fgcolor=ssd.WHITE)
Label(wri, row=180, col=10, text='XPT2046  touch',   fgcolor=ssd.YELLOW)

# ── Push framebuf to display ─────────────────────────────────────────────────

refresh(ssd)
print('[CYDTest] display updated')
