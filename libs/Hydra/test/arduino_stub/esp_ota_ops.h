#pragma once
/** Atrapa esp_ota_ops.h — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <stdint.h>

#define ESP_OK 0

typedef struct { int dummy; } esp_partition_t;

typedef enum {
    ESP_OTA_IMG_NEW = 0,
    ESP_OTA_IMG_PENDING_VERIFY,
    ESP_OTA_IMG_VALID,
    ESP_OTA_IMG_INVALID,
} esp_ota_img_states_t;

const esp_partition_t* esp_ota_get_running_partition();
int  esp_ota_get_state_partition(const esp_partition_t* partition,
                                 esp_ota_img_states_t* state);
int  esp_ota_mark_app_valid_cancel_rollback();
void esp_ota_mark_app_invalid_rollback_and_reboot();
