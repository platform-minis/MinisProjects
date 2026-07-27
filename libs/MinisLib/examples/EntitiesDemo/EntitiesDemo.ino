/**
 * EntitiesDemo — MinisIoT entity system example
 *
 * Demonstrates registering IoT entities so MyCastle renders the correct
 * UI controls automatically. No manual onCommand() plumbing needed for
 * writable entities — they auto-dispatch and auto-acknowledge.
 *
 * Entities declared:
 *   temperature  — MinisSensor   (read-only, °C)
 *   humidity     — MinisSensor   (read-only, %)
 *   motion       — MinisBinarySensor (read-only)
 *   relay        — MinisSwitch   (writable toggle)
 *   brightness   — MinisNumber   (writable 0–100 %)
 *   restart      — MinisButton   (writable momentary)
 *   mode         — MinisSelect   (writable enum)
 *
 * Compiled via MyCastle the injected MinisIotArchitecture.h provides
 * MINIS_DEVICE_SN, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD automatically.
 */

#include <MinisIoT.h>

// ─── Configuration fallbacks ──────────────────────────────────────────────────
#ifndef MINIS_WIFI_SSID
#  define MINIS_WIFI_SSID     "YourWiFiNetwork"
#endif
#ifndef MINIS_WIFI_PASSWORD
#  define MINIS_WIFI_PASSWORD "YourWiFiPassword"
#endif
#ifndef MINIS_DEVICE_SN
#  define MINIS_DEVICE_SN     "dev-entities-demo"
#endif

const char*    MYCASTLE_HOST = "192.168.0.89";
const uint16_t MYCASTLE_PORT = 1894;
const char*    USER_ID       = "marcin";

const uint32_t TELEMETRY_INTERVAL_MS = 10000;

// ─── Hardware ─────────────────────────────────────────────────────────────────
const uint8_t RELAY_PIN = 26;

// ─── Read-only entities (values reported via sendTelemetry) ──────────────────
MinisSensor       tempEntity    ("temperature", "Temperature", "°C");
MinisSensor       humEntity     ("humidity",    "Humidity",    "%");
MinisBinarySensor motionEntity  ("motion",      "Motion",      "Detected", "Clear");

// ─── Writable entities ────────────────────────────────────────────────────────
MinisSwitch relayEntity("relay", "Relay",
    [](bool state) {
        Serial.printf("Relay -> %s\n", state ? "ON" : "OFF");
        digitalWrite(RELAY_PIN, state ? HIGH : LOW);
    }
);

MinisNumber brightnessEntity("brightness", "Brightness",
    /*min*/ 0, /*max*/ 100, /*step*/ 1, /*unit*/ "%",
    [](float value) {
        Serial.printf("Brightness -> %.0f%%\n", value);
        // analogWrite(LED_PIN, (int)(value * 2.55f));
    }
);

MinisButton restartEntity("restart", "Restart",
    []() {
        Serial.println("Restarting...");
        delay(500);
        ESP.restart();
    }
);

MinisSelect modeEntity("mode", "Mode",
    {"auto", "manual", "eco"},
    [](const char* option) {
        Serial.printf("Mode -> %s\n", option);
    }
);

// ─── MinisIoT instance ────────────────────────────────────────────────────────
MinisIoT minis(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_SN);

uint32_t lastTelemetryMs = 0;

// ─── Sensor stubs — replace with real readings ────────────────────────────────
float readTemperature() { return 22.5f; }
float readHumidity()    { return 58.0f; }
bool  readMotion()      { return false; }

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    // Register all entities before begin()
    minis.addEntity(&tempEntity);
    minis.addEntity(&humEntity);
    minis.addEntity(&motionEntity);
    minis.addEntity(&relayEntity);
    minis.addEntity(&brightnessEntity);
    minis.addEntity(&restartEntity);
    minis.addEntity(&modeEntity);

    minis.setDebug(true);
    minis.setWifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD);
    minis.setHeartbeatInterval(60);

    Serial.println("Connecting to MyCastle...");
    if (!minis.begin(15000)) {
        Serial.println("Initial connect failed, will retry in loop()");
    } else {
        Serial.printf("Connected!  broker: %s\n", minis.brokerUri());
    }
}

void loop() {
    minis.loop();

    uint32_t now = millis();
    if (minis.isConnected() && (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS)) {
        lastTelemetryMs = now;

        MinisMetric metrics[] = {
            MinisMetric::Float("temperature", readTemperature(), "°C"),
            MinisMetric::Float("humidity",    readHumidity(),    "%"),
            MinisMetric::Bool ("motion",      readMotion()),
        };
        minis.sendTelemetry(metrics, 3);
    }
}
