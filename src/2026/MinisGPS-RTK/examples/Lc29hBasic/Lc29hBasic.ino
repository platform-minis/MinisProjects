/**
 * Lc29hBasic.ino — przykład użycia Lc29hSensor (ESP32-S3 + LC29HDA).
 * Publikacja stanu jako JSON — gotowe pod MQTT (payload wg MinisLib).
 *
 * Board: ESP32S3 Dev Module, Flash 16 MB, PSRAM OPI, USB CDC On Boot: Enabled.
 * Skopiuj firmware/config.example.h -> firmware/config.h i uzupełnij dane.
 */

#include <Arduino.h>
#include <Lc29hSensor.h>
#include <LocationJson.h>

using namespace minis;

// Dopasuj do serigrafii płytki (typowo UART1 na S3):
static constexpr int PIN_GNSS_RX = 17; // ESP32 <- TX LC29H
static constexpr int PIN_GNSS_TX = 18; // ESP32 -> RX LC29H

static ArduinoSerialPort gnssPort(Serial1);

static Lc29hSensor::Config makeConfig() {
    Lc29hSensor::Config c;                 // pola mają wartości domyślne
    c.withPort(gnssPort)
     .withPins(PIN_GNSS_RX, PIN_GNSS_TX)
     .withBaudRate(115200)
     .withRateMs(1000);
    return c;
}

static Lc29hSensor gnss(makeConfig());

static void onLocation(const LocationData& d, void* /*ctx*/) {
    if (!d.valid) return;
    char json[320];
    if (toJson(d, json, sizeof(json)) == 0) return;
    Serial.println(json);
    // tu: mqttClient.publish("minis/sensors/location", json);
}

void setup() {
    Serial.begin(115200);
    if (!gnss.begin()) {
        Serial.println("LC29H: init failed");
        return;
    }
    // callback bez alokacji (działa też z MINIS_NO_STD_FUNCTION)
    gnss.onUpdateRaw(onLocation);
}

void loop() {
    gnss.update();

    // co 10 s krótka diagnostyka łącza i RTK
    static uint32_t last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        const auto& s = gnss.stats();
        Serial.printf("[gnss] talker=%s fix=%d rtk=%d ok=%lu crcErr=%lu "
                      "ovf=%lu epochs=%lu timeouts=%lu rtcmAge=%.1f\n",
                      gnss.talker(), gnss.hasFix() ? 1 : 0, gnss.hasRtk() ? 1 : 0,
                      (unsigned long)s.sentencesOk, (unsigned long)s.checksumErrors,
                      (unsigned long)s.overflows, (unsigned long)s.epochsPublished,
                      (unsigned long)s.epochsTimedOut, gnss.data().rtcmAgeS);
    }

    delay(5);
}
