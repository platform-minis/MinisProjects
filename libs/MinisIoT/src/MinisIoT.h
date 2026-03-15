/**
 * MinisIoT - Arduino library for MyCastle IoT platform
 *
 * Connects ESP32 to the MyCastle backend via MQTT over WebSocket.
 * Handles telemetry publishing, heartbeat, and command reception.
 *
 * Dependencies:
 *   - ArduinoJson >= 7.x  (https://arduinojson.org)
 *   - ESP32 Arduino core >= 2.x  (includes esp_mqtt / ESP-IDF)
 *
 * MyCastle MQTT broker: ws://{host}:1902/mqtt  (Aedes, WebSocket only)
 */

#pragma once

// When compiled via MyCastle, a generated header is injected into the sketch
// directory with device-specific defines:
//   MINIS_DEVICE_SN        – device serial number
//   MINIS_WIFI_SSID        – WiFi network name
//   MINIS_WIFI_PASSWORD    – WiFi password
//   MINIS_IOT_ARCHITECTURE – full architecture JSON string
//
// You can also define these manually before including this header.
#if __has_include("MinisIotArchitecture.h")
#  include "MinisIotArchitecture.h"
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <atomic>
#include <mutex>
#include <queue>
#include <string>

// ─── MinisMetric ─────────────────────────────────────────────────────────────
/**
 * Represents a single sensor reading.
 *
 * Usage:
 *   MinisMetric::Float("temperature", 23.5f, "°C")
 *   MinisMetric::Bool("motion", true)
 *   MinisMetric::Str("status", "ok")
 */
struct MinisMetric {
    const char* key;
    const char* unit;   // optional, nullptr = omit from payload

    enum class Type { FLOAT, BOOL, STR } type;

    union {
        float f;
        bool  b;
    } num;
    const char* str;

    static MinisMetric Float(const char* key, float value, const char* unit = nullptr);
    static MinisMetric Bool (const char* key, bool value);
    static MinisMetric Str  (const char* key, const char* value);
};

// ─── Command callback ─────────────────────────────────────────────────────────
/**
 * Called when MyCastle sends a command to this device.
 *
 * @param id      Command UUID — pass to ackCommand()
 * @param name    Command name  (e.g. "set_relay", "restart")
 * @param payload Command parameters as JSON object
 *
 * Always call ackCommand(id, ...) after processing, even on failure.
 */
using MinisCommandCallback =
    std::function<void(const char* id, const char* name, JsonObjectConst payload)>;

// ─── MinisIoT ────────────────────────────────────────────────────────────────
class MinisIoT {
public:
    /**
     * @param host      MyCastle hostname or IP  (e.g. "192.168.1.100")
     * @param port      WebSocket MQTT port      (default: 1902)
     * @param userId    User ID in MyCastle       (e.g. "user1")
     * @param deviceId  Device ID in MyCastle     (e.g. "dev-iot1")
     */
    MinisIoT(const char* host, uint16_t port,
             const char* userId, const char* deviceId);
    ~MinisIoT();

    // Disable copy
    MinisIoT(const MinisIoT&)            = delete;
    MinisIoT& operator=(const MinisIoT&) = delete;

    // ── Configuration (call before begin()) ──────────────────────────────────

    /**
     * Set WiFi credentials.
     * If not called, WiFi must already be connected before calling begin().
     */
    void setWifi(const char* ssid, const char* password);

    /**
     * Register callback for commands received from MyCastle dashboard / REST API.
     * Always call ackCommand() after processing.
     */
    void onCommand(MinisCommandCallback cb);

    /**
     * Heartbeat interval in seconds (default: 60).
     * MyCastle marks the device OFFLINE after interval × 2.5 without any message.
     * Telemetry also resets the presence timer — if you send telemetry more often
     * than this interval you can set it to 0 to disable the separate heartbeat.
     */
    void setHeartbeatInterval(uint32_t seconds);

    /** Enable verbose logging to Serial. */
    void setDebug(bool enabled);

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * Connect WiFi (if credentials set) + connect MQTT broker.
     * Blocks until connected or timeoutMs elapses.
     *
     * @param timeoutMs  Total connection timeout (0 = wait forever)
     * @return true on success
     *
     * If begin() returns false the MQTT client continues running in the
     * background and loop() will process the connection once it succeeds.
     */
    bool begin(uint32_t timeoutMs = 15000);

    /**
     * Must be called every iteration of Arduino loop().
     * - Dispatches incoming command callbacks to the main thread
     * - Sends periodic heartbeats
     * - Auto-reconnects on loss of WiFi / MQTT
     */
    void loop();

    // ── Telemetry ─────────────────────────────────────────────────────────────

    /**
     * Publish sensor readings to MyCastle.
     * Also resets the device presence timer (same effect as heartbeat).
     *
     * @param metrics  Pointer to array of MinisMetric
     * @param count    Number of entries in the array
     * @param battery  Battery voltage in volts (NAN = omit)
     * @param rssi     WiFi RSSI dBm (0 = auto-read from WiFi.RSSI())
     * @return true if published successfully
     */
    bool sendTelemetry(const MinisMetric* metrics, size_t count,
                       float battery = NAN, int rssi = 0);

    /** Convenience overload for a single metric. */
    bool sendTelemetry(const MinisMetric& metric,
                       float battery = NAN, int rssi = 0);

    // ── Heartbeat ─────────────────────────────────────────────────────────────

    /**
     * Manually send a heartbeat to keep the device ONLINE.
     * Not needed if sendTelemetry() is called at least every heartbeatInterval.
     *
     * @param battery  Battery voltage in volts (NAN = omit)
     */
    bool sendHeartbeat(float battery = NAN);

    // ── Command ACK ───────────────────────────────────────────────────────────

    /**
     * Acknowledge a command received via onCommand callback.
     * MUST be called after every command, even on failure.
     *
     * @param id      Command UUID (from the callback)
     * @param success true → "EXECUTED",  false → "FAILED"
     * @param reason  Error description (only used when success == false)
     */
    bool ackCommand(const char* id, bool success = true, const char* reason = nullptr);

    // ── Status ────────────────────────────────────────────────────────────────

    /** True when MQTT is connected to MyCastle broker. */
    bool isConnected() const;

    /** Full WebSocket broker URI used by this client. */
    const char* brokerUri() const;

    /** MQTT client ID (format: minis-{deviceId}). */
    const char* clientId() const;

private:
    // ── Config ────────────────────────────────────────────────────────────────
    char     _host[64];
    uint16_t _port;
    char     _userId[64];
    char     _deviceId[64];
    char     _ssid[64];
    char     _password[64];
    bool     _hasWifi          = false;
    uint32_t _heartbeatSec     = 60;
    bool     _debug            = false;

    // ── Derived strings ───────────────────────────────────────────────────────
    char _topicTelemetry [128];
    char _topicHeartbeat [128];
    char _topicCommand   [128];
    char _topicCommandAck[128];
    char _brokerUri      [128];
    char _clientId       [64];

    // ── MQTT (ESP-IDF) ────────────────────────────────────────────────────────
    void* _mqttClient = nullptr;   // esp_mqtt_client_handle_t, opaque here

    // ── Thread-safe state ─────────────────────────────────────────────────────
    std::atomic<bool> _connected{false};
    std::atomic<bool> _pendingSubscribe{false};  // subscribe after connect

    struct IncomingMsg {
        std::string topic;
        std::string payload;
    };
    std::queue<IncomingMsg> _msgQueue;
    std::mutex              _queueMutex;

    // ── Heartbeat timer ───────────────────────────────────────────────────────
    uint32_t _lastHeartbeatMs      = 0;
    uint32_t _lastWifiReconnectMs  = 0;

    // ── Callback ──────────────────────────────────────────────────────────────
    MinisCommandCallback _commandCb;

    // ── Private helpers ───────────────────────────────────────────────────────
    void _buildTopics();
    bool _connectWifi(uint32_t timeoutMs);
    bool _startMqtt(uint32_t timeoutMs);
    bool _publish(const char* topic, const char* payload, int qos = 1);
    void _handleMessage(const char* topic, const char* data, int dataLen);
    void _log(const char* fmt, ...) const;

    static void _mqttEventHandler(void* arg, esp_event_base_t base,
                                  int32_t eventId, void* eventData);
};
