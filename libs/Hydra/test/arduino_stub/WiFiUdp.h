#pragma once
/** Atrapa WiFiUdp.h — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <Arduino.h>

class WiFiUDP {
public:
    uint8_t  begin(uint16_t port);
    uint8_t  beginMulticast(IPAddress group, uint16_t port);
    void     stop();
    int      beginPacket(IPAddress ip, uint16_t port);
    int      endPacket();
    size_t   write(const uint8_t* buf, size_t len);
    int      parsePacket();
    int      read(uint8_t* buf, size_t len);
    IPAddress remoteIP();
    uint16_t remotePort();
};
