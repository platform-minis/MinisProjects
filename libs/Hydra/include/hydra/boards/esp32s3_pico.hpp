#pragma once
/**
 * Płytka: Waveshare ESP32-S3-Pico (moduł `esp32-s3-pico` z modules.json).
 *
 * 8 MB Flash, 2 MB OPI PSRAM, natywne USB (HWCDC).
 * Uwaga sprzętowa: układ ESP32-S3R2 wymaga opcji PSRAM=opi — bez niej wysypuje
 * się przy starcie.
 *
 * Piny I2C i SPI to domyślne mapowanie arduino-esp32 dla S3. Jeśli Twoja wersja
 * płytki ma inne, nadpisz je w pliku płytki projektu — nie w kodzie aplikacji.
 */

#define HYDRA_BOARD_NAME "esp32s3-pico"

// Płytka ma wyłącznie diodę adresowalną WS2812 na GPIO21 — sterowanie nią
// wymaga sterownika, a nie zwykłego digitalWrite, więc dla HAL to brak diody.
#define HYDRA_BOARD_LED ::hydra::hal::kNoPin
#define HYDRA_BOARD_WS2812_PIN 21

#define HYDRA_BOARD_I2C0_ENABLE 1
#define HYDRA_BOARD_I2C0_SDA    8
#define HYDRA_BOARD_I2C0_SCL    9
#define HYDRA_BOARD_I2C0_HZ     400000

#define HYDRA_BOARD_SPI0_ENABLE 1
#define HYDRA_BOARD_SPI0_SCK    12
#define HYDRA_BOARD_SPI0_MISO   13
#define HYDRA_BOARD_SPI0_MOSI   11

// Konsola idzie przez natywne USB CDC — piny RX/TX nie dotyczą tego portu.
#define HYDRA_BOARD_UART0_ENABLE 1
#define HYDRA_BOARD_UART0_BAUD   115200

#define HYDRA_BOARD_STORAGE_NS "hydra"
