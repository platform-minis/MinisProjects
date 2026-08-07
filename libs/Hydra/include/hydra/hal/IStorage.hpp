#pragma once
/**
 * Hydra — trwała konfiguracja klucz→wartość (rozdz. 5, 13).
 *
 * Trzecia warstwa konfiguracji: sieć, kalibracje, poświadczenia — edytowalna
 * przez shell i panel WWW. Backendy: Preferences (ESP32, NVS), EEPROM lub
 * LittleFS (RP2), pamięć emulowana we Flash (STM32).
 *
 * Wartości typowane są nakładkami niewirtualnymi nad setBlob/getBlob, więc
 * backend implementuje tylko cztery operacje.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace hal {

/** Maksymalna długość klucza — wspólny mianownik NVS (15) i typowych FS. */
constexpr size_t kStorageKeyMax = 15;

class IStorage {
public:
    virtual ~IStorage() = default;

    /**
     * Otwiera przestrzeń nazw. Rozdzielenie przestrzeni (np. "net", "calib")
     * pozwala skasować konfigurację jednego podsystemu bez ruszania reszty.
     */
    virtual Status begin(const char* nameSpace, bool readOnly = false) = 0;
    virtual void   end() = 0;

    virtual Status         setBlob(const char* key, CByteSpan data)  = 0;
    virtual Result<size_t> getBlob(const char* key, ByteSpan out)    = 0;
    virtual Status         erase(const char* key)                    = 0;
    virtual Status         eraseAll()                                = 0;
    virtual bool           has(const char* key)                      = 0;

    /** Wymusza zapis na nośnik. Backendy buforujące nadpisują tę metodę. */
    virtual Status commit() { return ok(); }

    // --- nakładki typowane -------------------------------------------------

    Status setU32(const char* key, u32 value);
    Status setI32(const char* key, i32 value);
    Status setBool(const char* key, bool value);
    Status setFloat(const char* key, float value);
    Status setString(const char* key, const char* value);

    Result<u32>   getU32(const char* key, u32 fallback = 0);
    Result<i32>   getI32(const char* key, i32 fallback = 0);
    Result<bool>  getBool(const char* key, bool fallback = false);
    Result<float> getFloat(const char* key, float fallback = 0.0f);
    /** Kopiuje napis z terminatorem. Zwraca długość bez terminatora. */
    Result<size_t> getString(const char* key, char* out, size_t cap);
};

}  // namespace hal
}  // namespace hydra
