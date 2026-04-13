# Simple Cassette Player

A **minimalist DIY cassette player** built around the LM386 audio amplifier. Ideal as a first analog electronics project — teaches magnetic tape principles, audio amplification and passive filter design with a handful of components costing ~40–80 PLN.

## What you need

| Component | Qty | Notes |
|-----------|-----|-------|
| Magnetic tape head | 1 | Salvaged from old cassette deck |
| DC motor 3–9 V | 1 | Capstan drive for tape speed |
| LM386 amplifier IC | 1 | DIP-8, 0.5 W output |
| 250 µF electrolytic cap | 1 | Output coupling |
| 10 µF electrolytic cap | 1 | Gain setting (adds 46 dB) |
| 10 kΩ potentiometer | 1 | Volume control |
| 8 Ω speaker | 1 | Or 3.5 mm headphone jack |
| 9 V battery / adapter | 1 | |

## Skill level

⭐ Beginner — ideal first electronics project. No programming. No microcontroller.

## What's included

Full assembly guide in the detailed documentation, covering:
- Theory of magnetic recording and playback
- Step-by-step wiring diagram
- Breadboard and stripboard layouts
- Troubleshooting guide (no sound, distortion, hum)
- Upgrade paths (tone control, headphone output, better motor)

## Quick start

1. Mount the tape head and capstan motor in a salvaged cassette mechanism.
2. Wire: tape head → C1 → potentiometer → LM386 pin 3.
3. Connect 10 µF between pins 1 and 8 of LM386 for full gain.
4. Add 250 µF output cap and speaker on pin 5.
5. Power with 9 V. Insert a cassette, press Play, adjust volume.

## Signal path

```
Tape head → 47 nF coupling cap → volume pot → LM386 input
LM386 output → 250 µF coupling cap → 8 Ω speaker
```

## Key features

- Only ~10 passive components — BOM fits on one line
- LM386 provides 40–46 dB voltage gain — no preamp needed for cassette levels
- Complete theory section explains *why* each component is needed
- Step-by-step troubleshooting for every common failure mode
