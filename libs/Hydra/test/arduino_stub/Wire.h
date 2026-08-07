#pragma once
/** Atrapa Wire.h — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <stddef.h>
#include <stdint.h>

class TwoWire {
public:
    bool     begin(int sda, int scl, uint32_t frequency = 0);
    bool     begin();
    void     setClock(uint32_t hz);
    void     setSDA(uint8_t pin);
    void     setSCL(uint8_t pin);
    void     beginTransmission(uint8_t addr);
    uint8_t  endTransmission(bool sendStop = true);
    size_t   write(const uint8_t* data, size_t len);
    size_t   requestFrom(uint8_t addr, uint8_t len);
    int      read();
};

extern TwoWire Wire;
