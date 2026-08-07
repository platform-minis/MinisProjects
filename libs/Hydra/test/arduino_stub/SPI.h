#pragma once
/** Atrapa SPI.h — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <stdint.h>

#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

class SPISettings {
public:
    SPISettings() = default;
    SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode);
};

class SPIClass {
public:
    void    begin(int sck = -1, int miso = -1, int mosi = -1, int ss = -1);
    void    setSCK(uint8_t pin);
    void    setRX(uint8_t pin);
    void    setTX(uint8_t pin);
    void    beginTransaction(const SPISettings& settings);
    void    endTransaction();
    uint8_t transfer(uint8_t data);
};

extern SPIClass SPI;
