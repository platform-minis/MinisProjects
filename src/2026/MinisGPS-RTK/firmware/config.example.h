#pragma once
/**
 * config.example.h — MinisGPS-RTK
 * Skopiuj jako firmware/config.h i uzupełnij. config.h jest w .gitignore.
 */

// ------------------------------------------------------------------ WiFi
#define WIFI_SSID          "twoje-ssid"
#define WIFI_PASSWORD      "twoje-haslo"

// ------------------------------------------------------- NTRIP (ASG-EUPOS)
#define NTRIP_HOST         "system.asgeupos.pl"
#define NTRIP_PORT         2101
#define NTRIP_MOUNTPOINT   "RTN4G_VRS_RTCM32"   // strumień VRS RTCM 3.2
#define NTRIP_USER         "login-asgeupos"
#define NTRIP_PASSWORD     "haslo-asgeupos"
// VRS wymaga wysyłania własnej pozycji (GGA) do serwera, typowo co 10 s:
#define NTRIP_GGA_PERIOD_MS 10000

// ------------------------------------------------------------------ MQTT
#define MQTT_HOST          "192.168.1.10"       // broker na Proxmoksie
#define MQTT_PORT          1883
#define MQTT_USER          ""
#define MQTT_PASSWORD      ""
#define MQTT_CLIENT_ID     "minisgps-rtk-01"
#define MQTT_TOPIC_POS     "minis/sensors/location"
#define MQTT_TOPIC_STATUS  "minis/sensors/location/status"
#define MQTT_PUBLISH_MS    1000

// ------------------------------------------------------------- GNSS / piny
#define PIN_GNSS_RX        17    // ESP32 <- TX LC29H
#define PIN_GNSS_TX        18    // ESP32 -> RX LC29H
#define GNSS_BAUD          115200
#define GNSS_RATE_MS       1000  // <500 ms wymaga podniesienia GNSS_BAUD

// ------------------------------------------------------------- karta TF/PPK
#define SD_ENABLED         1
#define PIN_SD_CLK         14
#define PIN_SD_CMD         15
#define PIN_SD_D0          2
#define PPK_LOG_DIR        "/ppk"
