#pragma once
/** Atrapa Update.h — wyłącznie do sprawdzania składni (patrz Arduino.h). */

#include <Arduino.h>

#define U_FLASH 0
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF

class UpdateClass {
public:
    bool   begin(size_t size, int command = U_FLASH);
    size_t write(uint8_t* data, size_t len);
    bool   end(bool evenIfRemaining = false);
    void   abort();
};

extern UpdateClass Update;
