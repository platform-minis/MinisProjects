#pragma once
/** Atrapa ESPmDNS.h — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <stdint.h>

class MDNSResponder {
public:
    bool begin(const char* hostname);
    void addService(const char* service, const char* proto, uint16_t port);
    void end();
};

extern MDNSResponder MDNS;
