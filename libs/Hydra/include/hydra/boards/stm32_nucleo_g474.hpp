#pragma once
/**
 * Płytka: ST Nucleo-G474RE (STM32G4, Cortex-M4F).
 *
 * Jedyna z platform docelowych, na której scheduler FreeRTOS trzeba wystartować
 * ręcznie — robi to App::begin() i nigdy z niego nie wraca, więc loop() jest
 * tu martwy z definicji (rozdz. 4.1).
 *
 * Pamięć trwała to emulowany EEPROM we Flashu; jest wolniejsza od NVS i ma
 * ograniczoną liczbę cykli zapisu, więc zapisujemy tylko konfigurację.
 */

#define HYDRA_BOARD_NAME "nucleo-g474re"

/**
 * LD2 na PA5, czyli D13 w numeracji Arduino.
 *
 * Liczba, a nie LED_BUILTIN: Board.hpp wystawia `board::led` jako constexpr
 * i trafia do każdej jednostki kompilacji, także takiej, która nie widzi
 * nagłówków Arduino — i widzieć ich nie może, bo zabrania tego reguła
 * zależności. Pozostałe piny poniżej używają nazw wariantu, bo czyta je
 * wyłącznie backend HAL.
 */
#define HYDRA_BOARD_LED 13

// Wyprowadzenia zgodne z układem Arduino na złączu Nucleo.
#define HYDRA_BOARD_I2C0_ENABLE 1
#define HYDRA_BOARD_I2C0_SDA    PB9
#define HYDRA_BOARD_I2C0_SCL    PB8
#define HYDRA_BOARD_I2C0_HZ     400000

#define HYDRA_BOARD_SPI0_ENABLE 1
#define HYDRA_BOARD_SPI0_SCK    PA5
#define HYDRA_BOARD_SPI0_MISO   PA6
#define HYDRA_BOARD_SPI0_MOSI   PA7

#define HYDRA_BOARD_UART0_ENABLE 1
#define HYDRA_BOARD_UART0_BAUD   115200

#define HYDRA_BOARD_EEPROM_SIZE 2048
