#pragma once
/**
 * Płytka: ESP32-C3 DevKitM-1.
 *
 * Jeden rdzeń RISC-V — pinowanie tasków do rdzeni jest przyjmowane przez API,
 * ale nie ma efektu (rozdz. 4.2). Kod aplikacji pozostaje bez zmian.
 */

#define HYDRA_BOARD_NAME "esp32c3-devkit"

#define HYDRA_BOARD_LED 8   ///< WS2812 na DevKitM-1; na DevKitC to zwykła dioda

#define HYDRA_BOARD_I2C0_ENABLE 1
#define HYDRA_BOARD_I2C0_SDA    8
#define HYDRA_BOARD_I2C0_SCL    9
#define HYDRA_BOARD_I2C0_HZ     400000

#define HYDRA_BOARD_SPI0_ENABLE 1
#define HYDRA_BOARD_SPI0_SCK    4
#define HYDRA_BOARD_SPI0_MISO   5
#define HYDRA_BOARD_SPI0_MOSI   6

#define HYDRA_BOARD_UART0_ENABLE 1
#define HYDRA_BOARD_UART0_BAUD   115200
