/*
 * Atrapa nagłówka ESP-IDF <driver/i2s.h> dla kontroli składni backendu.
 *
 * Nie jest implementacją ani dokumentacją API — odwzorowuje tylko te symbole,
 * których używa src/hal/arduino/ArduinoI2s.cpp, żeby dało się go skompilować
 * na maszynie bez ESP-IDF. Prawdziwy build (`pio ci`) bierze nagłówek z SDK.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0

typedef enum { I2S_NUM_0 = 0, I2S_NUM_1 = 1 } i2s_port_t;

#define I2S_MODE_MASTER 0x01
#define I2S_MODE_TX     0x02
#define I2S_MODE_RX     0x04
#define I2S_MODE_PDM    0x40
typedef int i2s_mode_t;

typedef enum {
    I2S_BITS_PER_SAMPLE_16BIT = 16,
    I2S_BITS_PER_SAMPLE_24BIT = 24,
    I2S_BITS_PER_SAMPLE_32BIT = 32,
} i2s_bits_per_sample_t;

typedef enum {
    I2S_CHANNEL_FMT_RIGHT_LEFT = 0,
    I2S_CHANNEL_FMT_ONLY_LEFT  = 2,
} i2s_channel_fmt_t;

typedef enum {
    I2S_COMM_FORMAT_STAND_I2S = 1,
    I2S_COMM_FORMAT_STAND_MSB = 2,
} i2s_comm_format_t;

#define I2S_PIN_NO_CHANGE (-1)

typedef struct {
    i2s_mode_t            mode;
    int                   sample_rate;
    i2s_bits_per_sample_t bits_per_sample;
    i2s_channel_fmt_t     channel_format;
    i2s_comm_format_t     communication_format;
    int                   intr_alloc_flags;
    int                   dma_buf_count;
    int                   dma_buf_len;
    bool                  use_apll;
    bool                  tx_desc_auto_clear;
} i2s_config_t;

typedef struct {
    int mck_io_num;
    int bck_io_num;
    int ws_io_num;
    int data_out_num;
    int data_in_num;
} i2s_pin_config_t;

esp_err_t i2s_driver_install(i2s_port_t port, const i2s_config_t* cfg,
                             int queueSize, void* queue);
esp_err_t i2s_driver_uninstall(i2s_port_t port);
esp_err_t i2s_set_pin(i2s_port_t port, const i2s_pin_config_t* pins);
esp_err_t i2s_write(i2s_port_t port, const void* src, size_t size,
                    size_t* written, uint32_t ticks);
esp_err_t i2s_read(i2s_port_t port, void* dst, size_t size,
                   size_t* read, uint32_t ticks);
float     i2s_get_clk(i2s_port_t port);
