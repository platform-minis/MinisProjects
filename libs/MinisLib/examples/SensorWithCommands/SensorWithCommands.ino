/**
 * SensorWithCommands — MinisIoT example
 *
 * Reads temperature, publishes telemetry with relay state, and handles
 * "set_relay" commands sent from MyCastle dashboard.
 *
 * Demonstrates:
 *   - onCommand() callback
 *   - ackCommand() for success and failure
 *   - Mixed metric types (float + bool)
 *
 * Setup in MyCastle (one-time):
 *   PUT /api/users/{userId}/devices/{deviceId}/iot-config
 *   {
 *     "heartbeatIntervalSec": 60,
 *     "capabilities": [
 *       { "type": "sensor",   "metricKey": "temperature", "unit": "°C", "label": "Temperature" },
 *       { "type": "sensor",   "metricKey": "relay_state",               "label": "Relay state"  },
 *       { "type": "actuator", "commandName": "set_relay", "label": "Toggle relay",
 *         "payloadSchema": { "state": "boolean" } }
 *     ]
 *   }
 *
 * Send a command via curl:
 *   curl -X POST http://{host}:1902/api/users/{userId}/devices/{deviceId}/commands \
 *        -H 'Content-Type: application/json' \
 *        -d '{"name":"set_relay","payload":{"state":true}}'
 */

#include <MinisIoT.h>

// ─── Configuration (override via MinisIotArchitecture.h or define before include) ─
#ifndef MINIS_WIFI_SSID
#  define MINIS_WIFI_SSID     "MY_WIFI_SSID"
#endif
#ifndef MINIS_WIFI_PASSWORD
#  define MINIS_WIFI_PASSWORD "MY_WIFI_PASSWORD"
#endif
#ifndef MINIS_DEVICE_SN
#  define MINIS_DEVICE_SN     "dev-relay1"
#endif

const char*    MYCASTLE_HOST  = "192.168.1.100";
const uint16_t MYCASTLE_PORT  = 1902;

const char*    USER_ID        = "user1";

const uint8_t  RELAY_PIN      = 26;
const uint32_t TELEMETRY_INTERVAL_MS = 10000;

// ─── MinisIoT instance ────────────────────────────────────────────────────────
MinisIoT minis(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_SN);

uint32_t lastTelemetryMs = 0;

// ─── Sensor stub — replace with real reading ──────────────────────────────────
float readTemperature() { return 23.0f; }

// ─── Command handler ─────────────────────────────────────────────────────────
void handleCommand(const char* id, const char* name, JsonObjectConst payload) {
    Serial.printf("[CMD] %s  id=%s\n", name, id);

    if (strcmp(name, "set_relay") == 0) {
        // payload: { "state": true | false }
        if (!payload.containsKey("state")) {
            minis.ackCommand(id, false, "Missing 'state' field");
            return;
        }
        bool state = payload["state"].as<bool>();
        digitalWrite(RELAY_PIN, state ? HIGH : LOW);
        Serial.printf("Relay -> %s\n", state ? "ON" : "OFF");
        minis.ackCommand(id, true);

    } else {
        // Unknown command — always ACK, even if we can't execute
        char reason[64];
        snprintf(reason, sizeof(reason), "Unknown command: %s", name);
        minis.ackCommand(id, false, reason);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    minis.setDebug(true);
    minis.setWifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD);
    minis.onCommand(handleCommand);
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

        bool relayOn = digitalRead(RELAY_PIN) == HIGH;

        MinisMetric metrics[] = {
            MinisMetric::Float("temperature", readTemperature(), "°C"),
            MinisMetric::Bool ("relay_state", relayOn),
        };

        minis.sendTelemetry(metrics, 2);
    }
}
