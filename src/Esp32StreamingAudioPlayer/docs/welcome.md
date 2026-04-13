# ESP32 Streaming Audio Player

A **WiFi audio streaming platform** for ESP32-S3 that works in both directions — receive internet radio, Snapcast, or DLNA streams, and transmit audio from SD card or microphone as a web server, RTSP stream, or UDP multicast.

## What you need

- **ESP32-S3 Pico** (Waveshare, 8 MB PSRAM)
- I²S DAC — PCM5102A or MAX98357A for output
- I²S ADC or I²S microphone (INMP441) for transmit sketches
- WiFi access point with internet (for radio sketches)

## Skill level

⭐⭐⭐ Advanced — WiFi streaming, I²S audio codecs, HTTP/RTSP protocols.

## What's included

| Sketch | Description |
|--------|-------------|
| `http_radio` | Stream an Icecast/SHOUTcast radio URL |
| `icy_metadata` | Radio with track title displayed over Serial |
| `dlna_receiver` | Play audio from a DLNA/UPnP server on the LAN |
| `snapcast_client` | Synchronised multi-room audio (Snapcast protocol) |
| `wav_server` | Serve a WAV file over HTTP from SD card |
| `mp3_server` | Serve MP3 over HTTP |
| `rtsp_server` | Stream audio as RTSP |
| `udp_multicast` | Low-latency LAN audio broadcast |
| `bt_a2dp_source` | Stream SD card audio to Bluetooth speaker |
| `web_radio_ui` | Full web interface — browse and play radio stations |

## Quick start

1. Wire the I²S DAC: BCK → GPIO 12, LRCK → GPIO 13, DATA → GPIO 11.
2. Open `http_radio`, set your WiFi credentials and a radio stream URL.
3. Compile and flash. Audio starts within 5 seconds.

## Key features

- Receive: HTTP, ICY metadata, DLNA, Snapcast, Bluetooth A2DP sink
- Transmit: HTTP WAV/MP3 server, RTSP, UDP multicast, Bluetooth A2DP source
- Formats: MP3 (Helix), AAC, FLAC, WAV, Opus
- Built on [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) streaming pipeline
