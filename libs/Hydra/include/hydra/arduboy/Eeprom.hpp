/**
 * @file Eeprom.hpp
 * @brief Pamięć nieulotna gier — rekordy i ustawienia.
 *
 * Trzymamy pełne 1024 bajty oryginału w RAM-ie i zrzucamy je na nośnik, bo
 * tego wymaga interfejs: `EEPROM.read()` jest funkcją bez kodu błędu, więc
 * musi mieć odpowiedź od ręki. Zapis idzie do pliku dopiero przy `commit()`
 * albo — i to jest ważniejsze — sam, gdy od ostatniej zmiany minie chwila.
 *
 * Automatyczny zrzut nie jest wygodą, tylko koniecznością: gry na Arduboya
 * nie wołają `commit()`, bo na ATmega zapis do EEPROM-u był natychmiastowy.
 * Gdybyśmy czekali na jawne żądanie, rekord gracza znikałby przy każdym
 * zamknięciu okna — czyli zawsze.
 */
#pragma once

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include <string.h>

#include "hydra/core/Types.hpp"
#include "hydra/hal/IFileSystem.hpp"

namespace hydra {
namespace arduboy {

/** Rozmiar pamięci oryginału. Gry liczą na tę wartość przy adresowaniu. */
constexpr size_t kEepromBytes = 1024;

class EepromClass {
public:
    EepromClass() { memset(data_, 0xFF, sizeof(data_)); }

    /** Wczytuje zawartość z nośnika. Brak pliku to nie błąd — puste EEPROM. */
    Status begin(hal::IFileSystem& fs, const char* path = "eeprom.bin");

    /** Zrzuca zawartość, jeśli coś się zmieniło. */
    Status commit();

    /** Zrzuca, gdy od ostatniej zmiany minęło `kFlushDelayMs`. */
    void tick();

    u8   read(int address) const;
    void write(int address, u8 value);
    /** Zapisuje tylko przy różnicy — oszczędza cykle kasowania pamięci flash. */
    void update(int address, u8 value);

    static constexpr size_t length() { return kEepromBytes; }

    /** Odczyt dowolnego typu prostego, jak w bibliotece Arduino. */
    template <typename T>
    T& get(int address, T& value) const {
        if (address < 0 || address + static_cast<int>(sizeof(T)) > static_cast<int>(kEepromBytes)) {
            return value;
        }
        memcpy(&value, data_ + address, sizeof(T));
        return value;
    }

    template <typename T>
    const T& put(int address, const T& value) {
        if (address < 0 || address + static_cast<int>(sizeof(T)) > static_cast<int>(kEepromBytes)) {
            return value;
        }
        if (memcmp(data_ + address, &value, sizeof(T)) != 0) {
            memcpy(data_ + address, &value, sizeof(T));
            markDirty();
        }
        return value;
    }

    /** Dostęp indeksowany — `EEPROM[0]` z biblioteki Arduino. */
    u8 operator[](int address) const { return read(address); }

private:
    void markDirty();

    /** Ile czekamy z zapisem po ostatniej zmianie. */
    static constexpr u32 kFlushDelayMs = 1000;

    u8   data_[kEepromBytes];
    hal::IFileSystem* fs_   = nullptr;
    const char* path_       = nullptr;
    bool dirty_             = false;
    u32  dirtySinceMs_      = 0;
};

/**
 * Jedyna instancja pamięci nieulotnej.
 *
 * Funkcja, a nie obiekt globalny: nagłówek zgodności `<EEPROM.h>` rozwija
 * nazwę `EEPROM` właśnie na to wywołanie, więc runtime może sięgnąć po tę samą
 * instancję, nie włączając niczego z katalogu `compat`. Obiekt powstaje przy
 * pierwszym użyciu — gra, która nie tyka EEPROM-u, nie płaci za kilobajt.
 */
EepromClass& eeprom();

}  // namespace arduboy
}  // namespace hydra

#endif  // HYDRA_ENABLE_ARDUBOY
