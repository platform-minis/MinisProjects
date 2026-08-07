#pragma once
/** Atrapa WiFi.h — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <Arduino.h>

#define WIFI_STA     1
#define WL_CONNECTED 3

class WiFiClient : public Stream {
public:
    bool   connect(const char* host, uint16_t port);
    bool   connect(const char* host, uint16_t port, int32_t timeoutMs);
    void   stop();
    bool   connected();
    size_t write(const uint8_t* buf, size_t len) override;
    int    read(uint8_t* buf, size_t len);
    int    available() override;
};

class WiFiClass {
public:
    void      mode(int m);
    void      setAutoReconnect(bool on);
    void      begin(const char* ssid, const char* psk);
    void      disconnect();
    int       status();
    IPAddress localIP();
    long      RSSI();
};

extern WiFiClass WiFi;
