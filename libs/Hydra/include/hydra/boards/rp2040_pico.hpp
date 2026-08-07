#pragma once
/**
 * Płytka: Raspberry Pi Pico (RP2040) na core arduino-pico (Philhower).
 * Ten sam plik obsługuje Pico 2 (RP2350) — różnice dotyczą rdzenia, nie pinów.
 *
 * Uwaga wydajnościowa: RP2040 nie ma FPU (rozdz. 15). Pętle regulacji używają
 * real_t, które na tej platformie jest typem stałoprzecinkowym Q16.16.
 */

#define HYDRA_BOARD_NAME "rp2040-pico"

#define HYDRA_BOARD_LED 25

#define HYDRA_BOARD_I2C0_ENABLE 1
#define HYDRA_BOARD_I2C0_SDA    4
#define HYDRA_BOARD_I2C0_SCL    5
#define HYDRA_BOARD_I2C0_HZ     400000

#define HYDRA_BOARD_SPI0_ENABLE 1
#define HYDRA_BOARD_SPI0_SCK    18
#define HYDRA_BOARD_SPI0_MISO   16
#define HYDRA_BOARD_SPI0_MOSI   19

#define HYDRA_BOARD_UART0_ENABLE 1
#define HYDRA_BOARD_UART0_BAUD   115200

// Pamięć trwała to emulowany EEPROM w ostatnim sektorze Flash.
#define HYDRA_BOARD_EEPROM_SIZE 4096
