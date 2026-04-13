# ESP32 Audio Player

A modular audio platform for ESP32-S3 covering ten complete use cases — from a simple WAV file player to an internet radio with Bluetooth streaming. Built on the [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) library.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB PSRAM)
- MicroSD card module (SPI)
- I²S DAC — PCM5102A or MAX98357A recommended
- 4–8 Ω speaker or 3.5 mm audio jack
- Optional: Bluetooth A2DP speaker/headphones

## Skill level

⭐⭐ Intermediate — requires I²S wiring and familiarity with the Arduino audio ecosystem.

## What's included

| Sketch | Description |
|--------|-------------|
| `wav_player` | Plays WAV files from SD card via I²S DAC |
| `mp3_player` | MP3 playback using the Helix decoder |
| `internet_radio` | Streams Icecast/SHOUTcast HTTP radio |
| `bluetooth_receiver` | A2DP sink — play phone audio through ESP32 |
| `bluetooth_sender` | A2DP source — stream SD card to BT speaker |
| `fx_processor` | Real-time audio effects (reverb, equaliser) |
| `audio_recorder` | Record microphone to WAV on SD |
| `aac_player` | AAC file playback |
| `flac_player` | Lossless FLAC file playback |
| `loopback` | ADC → DAC pass-through for testing |

## Quick start

1. Wire I²S DAC: BCK → GPIO 12, LRCK → GPIO 13, DATA → GPIO 11.
2. Connect MicroSD (SPI): MOSI 35 / MISO 37 / SCK 36 / CS 34.
3. Open the `wav_player` sketch, set `SD_CS_PIN` and compile.
4. Copy `.wav` files to the root of your SD card.
5. Flash and listen.

## Key features

- Supports MP3, AAC, FLAC, WAV, Opus formats
- I²S, internal DAC (8-bit), and Bluetooth A2DP output
- ADC input via I²S codec or internal ADC
- Streaming over HTTP, ICY metadata, Snapcast and RTSP
- Modular pipeline — sources, transforms, and sinks are interchangeable
