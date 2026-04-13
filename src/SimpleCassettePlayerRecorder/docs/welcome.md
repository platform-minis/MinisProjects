# Simple Cassette Player & Recorder

Extends the Simple Cassette Player with **recording capability**. Adds a microphone preamplifier, a 60–100 kHz bias oscillator (required for linear magnetic recording), and an erase head — a complete analog recorder for ~80–150 PLN.

## What you need

Everything from **Simple Cassette Player**, plus:

| Component | Qty | Notes |
|-----------|-----|-------|
| NPN transistor (BC547 / 2N3904) | 2 | Microphone preamp (Q1) + bias oscillator |
| Electret microphone module | 1 | Built-in FET, 3-pin |
| Erase head | 1 | Salvaged from cassette deck |
| 100 nH inductor | 1 | Bias oscillator tank circuit |
| DPDT switch | 1 | Play/Record mode selector |
| 4.7 kΩ, 47 kΩ resistors | several | Biasing and gain |

## Skill level

⭐⭐ Intermediate — builds directly on the Player project. Understanding of oscillators and amplifier biasing is helpful.

## What's included

Full assembly guide covering:
- Theory of AC bias recording and why it linearises tape magnetisation
- Microphone preamplifier design (2-stage NPN amplifier)
- Bias oscillator circuit (65 kHz ± 10 kHz is ideal for Type I tape)
- Erase head wiring and blank tape procedure
- Recording level calibration

## Quick start

1. Build the Simple Cassette Player first — it is the playback stage.
2. Add the microphone preamp: electret mic → Q1 → coupling cap → record amplifier.
3. Build the bias oscillator and connect its output summed with the audio to the record head.
4. Wire the DPDT switch to toggle between Play (LM386 input) and Record (record head) paths.
5. To record: flip switch to Record, press REC + PLAY on the mechanism, speak into mic.
6. To play back: flip switch to Play, press PLAY — you should hear the recording.

## How bias recording works

```
Microphone → preamp → mixer ─────────────────→ record head → tape
                              ↑
                       bias oscillator (65 kHz)
```

The bias frequency is well above audible range and magnetises the tape in the linear region of its B-H curve, dramatically reducing distortion compared to direct (unbiased) recording.

## Key features

- Complete record + playback in one circuit
- Bias oscillator prevents tape saturation distortion
- Erase head blanks tape before overwriting
- Switchable Play/Record modes via DPDT switch
- Builds incrementally on the Simple Cassette Player
