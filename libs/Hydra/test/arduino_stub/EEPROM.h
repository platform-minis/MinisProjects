#pragma once
/** Atrapa EEPROM.h — wyłącznie do sprawdzania składni. */

#include <stddef.h>
#include <stdint.h>

/**
 * Dwa rdzenie, dwa różne API — atrapa musi je rozróżniać, inaczej przepuszcza
 * kod, który przy prawdziwej budowie się nie kompiluje.
 */
#ifdef ARDUINO_ARCH_RP2040

// arduino-pico: zawartość w RAM-ie, zapis do flasha dopiero przy commit().
class EEPROMClass {
public:
    void    begin(size_t size);
    uint8_t read(int address);
    void    write(int address, uint8_t value);
    bool    commit();

    template <typename T> T& get(int address, T& value) { (void)address; return value; }
    template <typename T> const T& put(int address, const T& value) { (void)address; return value; }
};

#else  // STM32duino

// begin() zwraca iterator, nie inicjuje pamięci; commit() nie istnieje.
// Zapisy idą przez warstwę buforowaną, żeby nie kasować strony flasha
// przy każdym bajcie.
class EEPROMClass {
public:
    uint8_t read(int address);
    void    write(int address, uint8_t value);
    uint16_t length();
};

void     eeprom_buffer_fill();
void     eeprom_buffer_flush();
uint8_t  eeprom_buffered_read_byte(uint32_t pos);
void     eeprom_buffered_write_byte(uint32_t pos, uint8_t value);

#endif

extern EEPROMClass EEPROM;
