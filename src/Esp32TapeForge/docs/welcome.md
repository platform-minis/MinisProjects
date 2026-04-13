# ESP32 Tape Forge

Write **digital data and audio to real magnetic cassette tapes** using an ESP32-S3 and its DAC. Supports C64 TAP, ZX Spectrum TZX, MSX/Kansas City Standard BIN files, and plain WAV audio — turn cheap blank cassettes into a retro computing archive.

## What you need

- **ESP32 DevKit-C** (or ESP32-S3 Pico)
- MicroSD card module (SPI) with data files
- Simple filter circuit: 100 nF capacitor + 10 kΩ resistor on DAC output
- Cassette recorder with AUX/Line input (or modified tape head connection)
- Optional: SSD1306 OLED display (I²C, 128×64)

## Skill level

⭐⭐⭐ Advanced — requires knowledge of analog signal conditioning and retro file formats.

## What's included

| Sketch | Description |
|--------|-------------|
| `tape_c64` | Write `.TAP` files — C64 pulse-width modulation format |
| `tape_spectrum` | Write `.TZX` files — ZX Spectrum ROM timings |
| `tape_kcs` | Write `.BIN` files — Kansas City Standard FSK (MSX, Apple II) |
| `tape_wav` | Write any `.WAV` file to tape (e.g. pre-encoded tape images) |
| `tape_ui` | Full OLED file browser + VU-meter + progress bar |

## Quick start

1. Copy `.TAP` / `.TZX` / `.WAV` files to the root of an SD card.
2. Connect SD card (SPI) and optionally an OLED display.
3. Wire DAC output (GPIO 25) through the RC filter to the cassette recorder AUX input.
4. Open `tape_ui`, select a file, press Play.
5. Press PLAY + REC on the cassette recorder simultaneously, then tap the ESP32 button.

## Signal path

```
ESP32 DAC (GPIO 25) → RC filter → cassette recorder AUX IN → tape head → magnetic tape
```

The RC filter (100 nF + 10 kΩ) removes DC offset and limits high-frequency noise to protect older tape mechanisms.

## Key features

- Accurate FSK and pulse-width signal generation for three retro platforms
- OLED UI with file browser, waveform visualiser and recording progress
- WAV playback allows any pre-encoded tape image to be written
- Works with standard blank C-60 or C-90 cassettes
