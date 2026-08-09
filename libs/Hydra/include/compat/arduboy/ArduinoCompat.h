/**
 * @file ArduinoCompat.h
 * @brief Podzbiór API Arduino potrzebny grom — tylko na cel natywny.
 *
 * Na układzie ten plik nie jest włączany: tam prawdziwe `Arduino.h` dostarcza
 * te same nazwy, i to lepiej. Tutaj chodzi o to, żeby gra pisana pod Arduino
 * zbudowała się i uruchomiła na PC, gdzie żadnego Arduino nie ma.
 *
 * ## PROGMEM i F() nie robią nic — i tak ma być
 *
 * Na ATmega pamięć programu leży w osobnej przestrzeni adresowej i sięga się
 * po nią osobnymi instrukcjami; stąd `PROGMEM`, `pgm_read_byte()` i `F()`.
 * Na ESP32, RP2350 i na PC pamięć jest jedna i odwzorowana w przestrzeni
 * adresowej, więc te makra są tożsamościowe. Nie jest to uproszczenie na
 * skróty: identycznie robi arduino-esp32 i arduino-pico.
 *
 * Wniosek dla przenoszonych gier: kod czytający dane przez `pgm_read_byte()`
 * zadziała, a kod czytający je **wprost** — co na AVR było błędem, a bywa
 * spotykane — też zadziała. Cel natywny jest więc łagodniejszy niż oryginał,
 * nigdy odwrotnie.
 */
#ifndef HYDRA_COMPAT_ARDUINO_H
#define HYDRA_COMPAT_ARDUINO_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hydra/core/Rtos.hpp"

// ── Typy Arduino ────────────────────────────────────────────────────────────

typedef uint8_t  byte;
typedef uint16_t word;
typedef bool     boolean;

// ── Pamięć programu ─────────────────────────────────────────────────────────

#ifndef PROGMEM
#  define PROGMEM
#endif
#ifndef PGM_P
#  define PGM_P const char*
#endif

#define pgm_read_byte(addr)      (*reinterpret_cast<const uint8_t*>(addr))
#define pgm_read_byte_near(addr) pgm_read_byte(addr)
#define pgm_read_word(addr)      (*reinterpret_cast<const uint16_t*>(addr))
#define pgm_read_word_near(addr) pgm_read_word(addr)
#define pgm_read_dword(addr)     (*reinterpret_cast<const uint32_t*>(addr))
#define pgm_read_float(addr)     (*reinterpret_cast<const float*>(addr))
#define pgm_read_ptr(addr)       (*reinterpret_cast<void* const*>(addr))

#define memcpy_P  memcpy
#define strcpy_P  strcpy
#define strlen_P  strlen
#define strcmp_P  strcmp

/** Na jednolitej przestrzeni adresowej łańcuch w pamięci programu to zwykły łańcuch. */
#define F(string_literal) (string_literal)
#define PSTR(string_literal) (string_literal)

// ── Poziomy i bity ──────────────────────────────────────────────────────────

#define HIGH 1
#define LOW  0
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2

#ifndef _BV
#  define _BV(bit) (1UL << (bit))
#endif

#define bitRead(value, bit)   (((value) >> (bit)) & 0x01)
#define bitSet(value, bit)    ((value) |= (1UL << (bit)))
#define bitClear(value, bit)  ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) \
    ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

#define lowByte(w)  (static_cast<uint8_t>((w) & 0xFF))
#define highByte(w) (static_cast<uint8_t>(((w) >> 8) & 0xFF))

// ── Czas ────────────────────────────────────────────────────────────────────

inline unsigned long millis() {
    return static_cast<unsigned long>(hydra::rtos::nowMs());
}

inline unsigned long micros() {
    return static_cast<unsigned long>(hydra::rtos::nowMs()) * 1000UL;
}

inline void delay(unsigned long ms) {
    hydra::rtos::delayMs(static_cast<uint32_t>(ms));
}

inline void delayMicroseconds(unsigned int us) {
    // Poniżej milisekundy nie ma na czym oprzeć czekania przenośnie, a gry
    // używają tego do opóźnień w sterowaniu wyświetlaczem, którego tu nie ma.
    if (us >= 1000) hydra::rtos::delayMs(us / 1000);
}

// ── Liczby losowe ───────────────────────────────────────────────────────────

/**
 * `random()` Arduino zwraca wartość ze znakiem i **nie** obejmuje górnej granicy.
 *
 * Różnica względem `rand() % n` jest istotna: gra losująca `random(4)` oczekuje
 * 0–3, a nie 0–4, i pomyłka o jeden objawia się dopiero jako rzadki wyjazd
 * poza tablicę.
 */
inline long random(long howbig) {
    if (howbig <= 0) return 0;
    return static_cast<long>(::rand()) % howbig;
}

inline long random(long howsmall, long howbig) {
    if (howbig <= howsmall) return howsmall;
    return howsmall + random(howbig - howsmall);
}

inline void randomSeed(unsigned long seed) {
    if (seed != 0) ::srand(static_cast<unsigned>(seed));
}

// ── Arytmetyka ──────────────────────────────────────────────────────────────

#ifndef min
#  define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef abs
#  define abs(x) ((x) > 0 ? (x) : -(x))
#endif
#ifndef constrain
#  define constrain(amt, low, high) \
      ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
    if (inMax == inMin) return outMin;
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// ── Wejścia-wyjścia, których na PC nie ma ───────────────────────────────────

// Gry wołają to przy starcie na nóżkach, których tutaj nie ma. Puste ciała są
// właściwą odpowiedzią: brak nóżki nie jest błędem gry.
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return 0; }
inline int  analogRead(uint8_t) { return 0; }
inline void analogWrite(uint8_t, int) {}

#endif  // HYDRA_COMPAT_ARDUINO_H
