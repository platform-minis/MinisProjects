/**
 * MinisIoT.cpp
 *
 * MQTT transport: MQTT 3.1.1 over WebSocket via ESP-IDF esp_mqtt_client.
 * Broker URI format: ws://{host}:{port}/mqtt
 *
 * The esp_mqtt_client runs in its own FreeRTOS task.
 * All events are enqueued in _mqttEventHandler (MQTT task) and
 * consumed in loop() (Arduino / main task) to keep user callbacks
 * on the safe, single-threaded Arduino side.
 */

#include "MinisIoT.h"

#include <WiFi.h>
#include <cmath>
#include <cstdarg>
#include <cstring>

// ESP-IDF MQTT client (available in Arduino-ESP32 >= 2.x)
#include "mqtt_client.h"

// ─── MinisMetric factories ────────────────────────────────────────────────────

MinisMetric MinisMetric::Float(const char* key, float value, const char* unit) {
    MinisMetric m;
    m.key  = key;
    m.unit = unit;
    m.type = Type::FLOAT;
    m.num.f = value;
    m.str   = nullptr;
    return m;
}

MinisMetric MinisMetric::Bool(const char* key, bool value) {
    MinisMetric m;
    m.key  = key;
    m.unit = nullptr;
    m.type = Type::BOOL;
    m.num.b = value;
    m.str   = nullptr;
    return m;
}

MinisMetric MinisMetric::Str(const char* key, const char* value) {
    MinisMetric m;
    m.key  = key;
    m.unit = nullptr;
    m.type = Type::STR;
    m.str  = value;
    return m;
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

MinisIoT::MinisIoT(const char* host, uint16_t port,
                   const char* userId, const char* deviceId)
    : _port(port)
{
    strlcpy(_host,     host,     sizeof(_host));
    strlcpy(_userId,   userId,   sizeof(_userId));
    strlcpy(_deviceId, deviceId, sizeof(_deviceId));
    _buildTopics();
}

MinisIoT::~MinisIoT() {
    if (_mqttClient) {
        esp_mqtt_client_destroy(
            static_cast<esp_mqtt_client_handle_t>(_mqttClient));
    }
}

// ─── Configuration ────────────────────────────────────────────────────────────

void MinisIoT::setWifi(const char* ssid, const char* password) {
    strlcpy(_ssid,     ssid,     sizeof(_ssid));
    strlcpy(_password, password, sizeof(_password));
    _hasWifi = true;
}

void MinisIoT::onCommand(MinisCommandCallback cb) {
    _commandCb = cb;
}

void MinisIoT::setHeartbeatInterval(uint32_t seconds) {
    _heartbeatSec = seconds;
}

void MinisIoT::setDebug(bool enabled) {
    _debug = enabled;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

bool MinisIoT::begin(uint32_t timeoutMs) {
    if (_hasWifi) {
        if (!_connectWifi(timeoutMs)) {
            // WiFi timed out — start MQTT non-blocking so it connects
            // automatically once WiFi comes up via loop()
            _startMqtt(0);
            return false;
        }
    }
    return _startMqtt(timeoutMs);
}

void MinisIoT::loop() {
    // ── WiFi watchdog ─────────────────────────────────────────────────────────
    if (_hasWifi && WiFi.status() != WL_CONNECTED) {
        _connected = false;
        uint32_t now = millis();
        if (now - _lastWifiReconnectMs >= 5000) {
            _log("WiFi not connected, retrying...");
            WiFi.begin(_ssid, _password);
            _lastWifiReconnectMs = now;
        }
        return;
    }

    // ── Dispatch incoming messages (MQTT task → main task) ────────────────────
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        while (!_msgQueue.empty()) {
            auto msg = std::move(_msgQueue.front());
            _msgQueue.pop();
            // Unlock while dispatching so MQTT task can enqueue more
            _queueMutex.unlock();
            _handleMessage(msg.topic.c_str(),
                           msg.payload.c_str(),
                           (int)msg.payload.size());
            _queueMutex.lock();
        }
    }

    // ── Auto-heartbeat ────────────────────────────────────────────────────────
    if (_heartbeatSec > 0 && _connected) {
        uint32_t now = millis();
        if (now - _lastHeartbeatMs >= _heartbeatSec * 1000UL) {
            sendHeartbeat();
            _lastHeartbeatMs = now;
        }
    }
}

// ─── Telemetry ────────────────────────────────────────────────────────────────

bool MinisIoT::sendTelemetry(const MinisMetric* metrics, size_t count,
                              float battery, int rssi) {
    if (!_connected) return false;

    JsonDocument doc;
    JsonArray arr = doc["metrics"].to<JsonArray>();

    for (size_t i = 0; i < count; i++) {
        JsonObject m = arr.add<JsonObject>();
        m["key"] = metrics[i].key;
        switch (metrics[i].type) {
            case MinisMetric::Type::FLOAT: m["value"] = metrics[i].num.f; break;
            case MinisMetric::Type::BOOL:  m["value"] = metrics[i].num.b; break;
            case MinisMetric::Type::STR:   m["value"] = metrics[i].str;   break;
        }
        if (metrics[i].unit) m["unit"] = metrics[i].unit;
    }

    if (rssi == 0) rssi = WiFi.RSSI();
    doc["rssi"] = rssi;
    if (!std::isnan(battery)) doc["battery"] = battery;

    char buf[1024];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf) - 1) return false;

    return _publish(_topicTelemetry, buf);
}

bool MinisIoT::sendTelemetry(const MinisMetric& metric,
                              float battery, int rssi) {
    return sendTelemetry(&metric, 1, battery, rssi);
}

// ─── Heartbeat ────────────────────────────────────────────────────────────────

bool MinisIoT::sendHeartbeat(float battery) {
    if (!_connected) return false;

    JsonDocument doc;
    doc["uptime"] = millis() / 1000UL;
    doc["rssi"]   = WiFi.RSSI();
    if (!std::isnan(battery)) doc["battery"] = battery;

    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    return _publish(_topicHeartbeat, buf);
}

// ─── Command ACK ─────────────────────────────────────────────────────────────

bool MinisIoT::ackCommand(const char* id, bool success, const char* reason) {
    if (!_connected) return false;

    JsonDocument doc;
    doc["id"]     = id;
    doc["status"] = success ? "EXECUTED" : "FAILED";
    if (!success && reason) doc["reason"] = reason;

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    return _publish(_topicCommandAck, buf);
}

// ─── Status ───────────────────────────────────────────────────────────────────

bool MinisIoT::isConnected() const {
    return _connected.load();
}

const char* MinisIoT::brokerUri() const {
    return _brokerUri;
}

const char* MinisIoT::clientId() const {
    return _clientId;
}

// ─── Private: topic / string building ────────────────────────────────────────

void MinisIoT::_buildTopics() {
    snprintf(_topicTelemetry,  sizeof(_topicTelemetry),
             "minis/%s/%s/telemetry",   _userId, _deviceId);
    snprintf(_topicHeartbeat,  sizeof(_topicHeartbeat),
             "minis/%s/%s/heartbeat",   _userId, _deviceId);
    snprintf(_topicCommand,    sizeof(_topicCommand),
             "minis/%s/%s/command",     _userId, _deviceId);
    snprintf(_topicCommandAck, sizeof(_topicCommandAck),
             "minis/%s/%s/command/ack", _userId, _deviceId);
    snprintf(_brokerUri,       sizeof(_brokerUri),
             "ws://%s:%u/mqtt",         _host, _port);
    snprintf(_clientId,        sizeof(_clientId),
             "minis-%s",                _deviceId);
}

// ─── Private: WiFi ───────────────────────────────────────────────────────────

bool MinisIoT::_connectWifi(uint32_t timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;

    _log("Connecting to WiFi: %s", _ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) {
            _log("WiFi timeout");
            return false;
        }
        delay(250);
    }
    _log("WiFi connected  IP: %s", WiFi.localIP().toString().c_str());
    return true;
}

// ─── Private: MQTT start ─────────────────────────────────────────────────────

bool MinisIoT::_startMqtt(uint32_t timeoutMs) {
    _log("Connecting to broker: %s  clientId: %s", _brokerUri, _clientId);

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri        = _brokerUri;
    cfg.credentials.client_id     = _clientId;
    cfg.session.keepalive          = (_heartbeatSec > 0)
                                        ? (int)_heartbeatSec
                                        : 60;
    cfg.network.timeout_ms         = (timeoutMs > 0) ? timeoutMs : 10000;
    cfg.network.reconnect_timeout_ms = 5000;

    auto client = esp_mqtt_client_init(&cfg);
    if (!client) {
        _log("esp_mqtt_client_init failed");
        return false;
    }
    _mqttClient = client;

    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY,
                                   _mqttEventHandler, this);
    esp_mqtt_client_start(client);

    // Wait for first connection
    uint32_t start = millis();
    while (!_connected) {
        if (timeoutMs > 0 && (millis() - start) >= timeoutMs) {
            _log("MQTT connection timeout (client still running in background)");
            return false;
        }
        delay(100);
    }
    return true;
}

// ─── Private: publish ────────────────────────────────────────────────────────

bool MinisIoT::_publish(const char* topic, const char* payload, int qos) {
    auto client = static_cast<esp_mqtt_client_handle_t>(_mqttClient);
    if (!client || !_connected) return false;

    int ret = esp_mqtt_client_publish(client, topic, payload,
                                      (int)strlen(payload), qos, 0);
    _log("PUB [%s] %s", topic, payload);
    return ret >= 0;
}

// ─── Private: incoming message dispatcher ────────────────────────────────────

void MinisIoT::_handleMessage(const char* topic, const char* data, int dataLen) {
    _log("MSG [%s] %.*s", topic, dataLen, data);

    if (strcmp(topic, _topicCommand) != 0) return;
    if (!_commandCb) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, dataLen);
    if (err) {
        _log("JSON error: %s", err.c_str());
        return;
    }

    const char* id      = doc["id"]   | "";
    const char* name    = doc["name"] | "";
    JsonObjectConst pl  = doc["payload"].as<JsonObjectConst>();

    _commandCb(id, name, pl);
}

// ─── Private: logging ────────────────────────────────────────────────────────

void MinisIoT::_log(const char* fmt, ...) const {
    if (!_debug) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[MinisIoT] %s\n", buf);
}

// ─── Static MQTT event handler (runs in MQTT task) ───────────────────────────

void MinisIoT::_mqttEventHandler(void* arg, esp_event_base_t /*base*/,
                                  int32_t eventId, void* eventData) {
    auto* self  = static_cast<MinisIoT*>(arg);
    auto* event = static_cast<esp_mqtt_event_handle_t>(eventData);
    auto  client = static_cast<esp_mqtt_client_handle_t>(self->_mqttClient);

    switch (static_cast<esp_mqtt_event_id_t>(eventId)) {

        case MQTT_EVENT_CONNECTED:
            self->_connected = true;
            self->_log("MQTT connected");
            // Subscribe to command topic (QoS 1)
            esp_mqtt_client_subscribe(client, self->_topicCommand, 1);
            self->_log("Subscribed: %s", self->_topicCommand);
            // Reset heartbeat timer so first heartbeat fires after the interval
            self->_lastHeartbeatMs = millis();
            break;

        case MQTT_EVENT_DISCONNECTED:
            self->_connected = false;
            self->_log("MQTT disconnected — will auto-reconnect");
            break;

        case MQTT_EVENT_DATA: {
            // Guard: partial messages (large payload split across packets)
            if (event->data_len < event->total_data_len) {
                self->_log("Partial message skipped (len=%d/%d)",
                           event->data_len, event->total_data_len);
                break;
            }
            std::string topic(event->topic,   event->topic_len);
            std::string data (event->data,    event->data_len);
            std::lock_guard<std::mutex> lock(self->_queueMutex);
            self->_msgQueue.push({std::move(topic), std::move(data)});
            break;
        }

        case MQTT_EVENT_ERROR:
            self->_log("MQTT error (error_type=%d)",
                       event->error_handle ? event->error_handle->error_type : -1);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            self->_log("MQTT subscribed (msg_id=%d)", event->msg_id);
            break;

        default:
            break;
    }
}
