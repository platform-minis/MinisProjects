#pragma once
/** Atrapa LEAmDNS.h (arduino-pico) — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <stdint.h>

class MDNSResponder {
public:
    bool begin(const char* hostname);
    bool addService(const char* service, const char* proto, uint16_t port);
    void end();
};

extern MDNSResponder MDNS;
