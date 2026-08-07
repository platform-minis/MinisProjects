#pragma once
/** Atrapa Ethernet.h — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <Arduino.h>

#define LinkON 1
#define INADDR_NONE IPAddress()

class EthernetClient : public Stream {
public:
    bool   connect(const char* host, uint16_t port);
    void   stop();
    bool   connected();
    size_t write(const uint8_t* buf, size_t len) override;
    int    read(uint8_t* buf, size_t len);
    int    available() override;
};

class EthernetClass {
public:
    int       begin(uint8_t* mac);
    int       linkStatus();
    IPAddress localIP();
};

extern EthernetClass Ethernet;
