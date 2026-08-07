#pragma once
/** Atrapa Preferences.h (NVS na ESP32) — wyłącznie do sprawdzania składni. */

#include <stddef.h>
#include <stdint.h>

class Preferences {
public:
    bool   begin(const char* name, bool readOnly = false);
    void   end();
    size_t putBytes(const char* key, const void* value, size_t len);
    size_t getBytes(const char* key, void* buf, size_t maxLen);
    size_t getBytesLength(const char* key);
    bool   isKey(const char* key);
    bool   remove(const char* key);
    bool   clear();
};
