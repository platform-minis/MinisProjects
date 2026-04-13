# RPi Pico 2 Display VGA

A **80×60 character VGA terminal** on Raspberry Pi Pico 2, driven by PIO state machines. Core 1 handles VGA signal generation (640×480 @ 60 Hz, R3G3B2 colour), Core 0 handles PS/2 keyboard input and text rendering — a complete retro text computer.

## What you need

- **Raspberry Pi Pico 2** (RP2350, 4 MB Flash)
- **Waveshare VGA PS2 Board** (provides VGA DAC resistors + PS/2 connector)
- VGA monitor
- PS/2 keyboard (or USB keyboard with PS/2 adapter)
- CMake build environment + pico-sdk

## Skill level

⭐⭐⭐ Advanced — dual-core programming, PIO state machines, VGA timing.

## What's included

| Sketch | Description |
|--------|-------------|
| `vga_terminal` | 80×60 text terminal with PS/2 keyboard input and colour character rendering |

## Quick start

1. Build with CMake (pico-sdk must be in `PICO_SDK_PATH`):
   ```bash
   mkdir build && cd build
   cmake -DPICO_BOARD=pico2 ..
   make -j4
   ```
2. Flash `vga_terminal.uf2` to the Pico 2 (BOOTSEL mode).
3. Connect the Waveshare VGA PS2 Board to the Pico 2 GPIO header.
4. Connect VGA monitor and PS/2 keyboard.
5. Power on — a ">" prompt appears, type to interact.

## How it works

```
Core 1: PIO0 → R3G3B2 pixels → VGA DAC → monitor (640×480 @ 60 Hz)
Core 0: PIO1 → PS/2 clock/data → keyboard decoding → text buffer update
```

The 80×60 character framebuffer (4800 bytes) is rendered using an 8×8 pixel bitmap font. Each character cell stores ASCII code and colour attribute. Core 1 scans the buffer continuously at 60 Hz with no Core 0 involvement.

## Key features

- 640×480 VGA output at 60 Hz, 8-bit colour (R3G3B2)
- 80×60 character terminal (8×8 px font)
- PS/2 keyboard support via PIO (no bitbanging)
- Dual-core: display on Core 1, logic on Core 0
- Based on [PicoVGA](https://github.com/codaris/picovga-cmake) + [ps2kbd-lib](https://github.com/lurk101/ps2kbd-lib)
