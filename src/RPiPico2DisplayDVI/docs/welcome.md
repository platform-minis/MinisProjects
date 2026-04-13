# RPi Pico 2 Display DVI

Drive a **DVI/HDMI monitor** from a Raspberry Pi Pico 2 (RP2350) using the HSTX high-speed transceiver peripheral — no external chip required. Outputs **640×480 monochrome** at standard DVI timings via 4 differential pairs (8 GPIO pins).

## What you need

- **Raspberry Pi Pico 2** (RP2350, 4 MB Flash)
- DVI or HDMI monitor (DVI-D or HDMI with DVI adapter)
- **DVI breakout board** with HDMI connector and series resistors (e.g. from Pimoroni or DIY)
- 8 jumper wires for TMDS differential pairs
- CMake build environment + pico-sdk

## Skill level

⭐⭐⭐ Advanced — requires CMake build setup, RP2350-specific HSTX knowledge, DVI signal timing.

## What's included

| Sketch | Description |
|--------|-------------|
| `dvi_mono` | 640×480 monochrome framebuffer output — draws a test pattern |

## Quick start

1. Install pico-sdk and CMake (see the [Pico Getting Started Guide](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)).
2. In the project directory:
   ```bash
   mkdir build && cd build
   cmake -DPICO_BOARD=pico2 ..
   make -j4
   ```
3. Flash `dvi_mono.uf2` — hold BOOTSEL while plugging in, copy the file.
4. Connect TMDS pairs to your DVI breakout:
   - GPIO 12/13 → TMDS D0±
   - GPIO 14/15 → TMDS D1±
   - GPIO 16/17 → TMDS D2±
   - GPIO 18/19 → TMDS CLK±
5. Power on — the monitor should display the test pattern.

## How it works

The RP2350 HSTX peripheral generates the TMDS (Transition Minimised Differential Signalling) stream in hardware. A DMA channel feeds the 38,400-byte (640×480 ÷ 8) framebuffer directly to HSTX, which handles TMDS encoding, serialisation and differential output — all without CPU involvement during display.

## Key features

- Hardware TMDS via HSTX — zero CPU load during display
- 640×480 @ 60 Hz, DVI-compatible timings
- 38,400-byte framebuffer (1 bit per pixel)
- Based on the open-source [picodvi](https://github.com/Wren6991/PicoDVI) library adapted for RP2350 HSTX
