# RPi Pico 2 Test Platform

A **GPIO stimulus and capture controller** for hardware testing. The Raspberry Pi Pico 2 acts as a precision instrument: 8 output pins can generate programmable pulses, sequences and PWM; 8 input pins capture edge transitions at 150 MHz via PIO. Controlled entirely over USB-CDC with a simple ASCII protocol.

## What you need

- **Raspberry Pi Pico 2** (RP2350, 4 MB Flash)
- USB cable
- **74LVC245** octal bus transceiver (for output protection — optional but recommended)
- DUT (Device Under Test) connected to GP0–GP7 (outputs) and GP8–GP15 (inputs)
- CMake build environment + pico-sdk

## Skill level

⭐⭐⭐ Advanced — hardware testing, PIO programming, ASCII protocol design.

## What's included

| Sketch | Description |
|--------|-------------|
| `gpio_stimulus` | Full stimulus/capture platform with USB-CDC command interface |

## Quick start

1. Build and flash:
   ```bash
   mkdir build && cd build && cmake -DPICO_BOARD=pico2 .. && make -j4
   ```
2. Open a serial terminal at 115200 baud (USB-CDC).
3. Type `HELP` to list all commands.
4. Set a pin high:  `SET 0 1`
5. Generate a 1 ms pulse: `PULSE 2 1000`
6. Read captured edges on GP8: `READ 8`

## Command summary

| Command | Example | Description |
|---------|---------|-------------|
| `SET p v` | `SET 0 1` | Drive GP_p to value v (0 or 1) |
| `PULSE p us` | `PULSE 2 500` | Output pulse on GP_p, width in µs |
| `SEQUENCE p data rate` | `SEQUENCE 3 0b10110 1000` | Bit sequence at specified bit rate |
| `PWM p freq duty` | `PWM 4 1000 50` | PWM output, frequency + duty cycle |
| `READ p` | `READ 8` | Read captured edge timestamps on input GP_p |
| `RESET` | `RESET` | Reset all pins and capture buffers |

## Key features

- 8 stimulus outputs (GP0–GP7) with four output modes
- 8 capture inputs (GP8–GP15) at 150 MHz via PIO edge detection
- Timestamped edge capture with sub-µs resolution
- 265 built-in unit tests — run `TEST` to verify platform integrity
- 74LVC245 buffer protection for safe connection to 3.3 V and 5 V logic
