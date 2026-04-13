# ESP32 Vinyl Caster

A **wireless analog digitizer** for ESP32-S3. Connects a turntable, cassette deck or FM tuner and streams the audio over WiFi or saves it as WAV/FLAC to a microSD card — high-quality digital archiving without a PC.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB PSRAM)
- I²S ADC — **PCM1808** recommended (24-bit, 99 dB SNR)
- Analog audio source: turntable, cassette deck, FM tuner, or aux output
- Optional: RIAA phono preamp for turntables (passive EQ correction)
- Optional: MicroSD card for local recording

## Skill level

⭐⭐⭐ Advanced — analog signal conditioning, I²S ADC wiring, audio streaming protocols.

## What's included

| Sketch | Description |
|--------|-------------|
| `wav_server` | Serve live audio as WAV over HTTP — open in VLC or Audacity |
| `rtsp_stream` | Stream via RTSP — compatible with most media players |
| `udp_multicast` | Low-latency LAN broadcast |
| `sd_recorder` | Record WAV/FLAC directly to SD card |
| `flac_recorder` | Lossless FLAC recording to SD (requires more CPU) |

## Quick start

1. Wire PCM1808 ADC: BCK/SCK → GPIO 12, LRCK → GPIO 13, DATA → GPIO 14, FMT → GND (I²S mode).
2. Connect analog output of your source to the PCM1808 input (via 10 kΩ resistor for impedance matching).
3. Open `wav_server`, set WiFi credentials, compile and flash.
4. Open VLC → Media → Open Network Stream → `http://<ESP32-IP>/audio.wav`.
5. Press Play on your turntable/tape deck — audio streams in real time.

## ADC options

| ADC | Resolution | SNR | Notes |
|-----|-----------|-----|-------|
| **PCM1808** (recommended) | 24-bit | 99 dB | Standalone ADC, I²S output |
| ES8388 | 24-bit | 90 dB | Codec chip, also has DAC |
| CS5343 | 24-bit | 100 dB | Professional grade |
| Internal ESP32 | 12-bit | ~50 dB | Very noisy — not recommended |

## Key features

- Up to 24-bit/96kHz audio digitization
- WiFi streaming: WAV server, RTSP, UDP multicast
- Local recording: WAV and FLAC to SD card
- RIAA equalization guidance included (for gramophone/vinyl)
- No PC required — fully standalone operation
