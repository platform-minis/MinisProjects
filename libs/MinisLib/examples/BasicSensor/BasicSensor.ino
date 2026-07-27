/**
 * BasicSensor — MinisIoT example
 *
 * Sends temperature and humidity to MyCastle every 10 seconds.
 * No command handling.
 *
 * Hardware: ESP32 + DHT22 or any sensor.
 *           Replace readTemperature() / readHumidity() with your own code.
 *
 * When compiled via MyCastle "Compile" button with a Device SN selected,
 * a MinisIotArchitecture.h header is injected automatically with:
 *   MINIS_DEVICE_SN, MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD,
 *   MINIS_IOT_ARCHITECTURE
 * The values below are used as fallbacks when compiling manually.
 */

#include <MinisIoT.h>

// ─── Configuration (override via MinisIotArchitecture.h or define before include) ─
#ifndef MINIS_WIFI_SSID
#  define MINIS_WIFI_SSID     "YourWiFiNetwork"
#endif
#ifndef MINIS_WIFI_PASSWORD
#  define MINIS_WIFI_PASSWORD "YourWiFiPassword"
#endif
#ifndef MINIS_DEVICE_SN
#  define MINIS_DEVICE_SN     "YourDeviceSerialNumber"
#endif

const char* MYCASTLE_HOST    = "192.168.0.89";  // hostname or IP
const uint16_t MYCASTLE_PORT = 1894;
const char* USER_ID          = "marcin";        // MyCastle user ID

// ─── Telemetry interval ───────────────────────────────────────────────────────
const uint32_t TELEMETRY_INTERVAL_MS = 10000;  // 10 seconds

// ─── MinisIoT instance ────────────────────────────────────────────────────────
MinisIoT minis(MYCASTLE_HOST, MYCASTLE_PORT, USER_ID, MINIS_DEVICE_SN);

uint32_t lastTelemetryMs = 0;

// ─── Sensor stubs — replace with real readings ────────────────────────────────
float readTemperature() { return 22.5f; }
float readHumidity()    { return 60.0f; }

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    minis.setDebug(true);                              // verbose Serial output
    minis.setWifi(MINIS_WIFI_SSID, MINIS_WIFI_PASSWORD);
    minis.setHeartbeatInterval(60);                    // send heartbeat every 60s

    Serial.println("Connecting to MyCastle...");
    if (!minis.begin(15000)) {
        // begin() timed out — client still running in background
        Serial.println("Initial connect failed, will retry in loop()");
    } else {
        Serial.printf("Connected!  broker: %s  clientId: %s\n",
                      minis.brokerUri(), minis.clientId());
    }
}

void loop() {
    minis.loop();  // MUST be called every loop iteration

    uint32_t now = millis();
    if (minis.isConnected() && (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS)) {
        lastTelemetryMs = now;

        float temp = readTemperature();
        float hum  = readHumidity();

        MinisMetric metrics[] = {
            MinisMetric::Float("temperature", temp, "°C"),
            MinisMetric::Float("humidity",    hum,  "%"),
        };

        bool ok = minis.sendTelemetry(metrics, 2);
        if (ok) {
            Serial.printf("Sent  temp=%.1f°C  hum=%.0f%%\n", temp, hum);
        } else {
            Serial.println("sendTelemetry failed");
        }
    }
}
