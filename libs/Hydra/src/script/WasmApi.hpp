#pragma once
/**
 * Hydra — treść importów WebAssembly, niezależna od runtime'u.
 *
 * Nagłówek wewnętrzny warstwy skryptowej. Każda funkcja robi to, co import
 * naprawdę ma zrobić, i przyjmuje wyłącznie typy, które WebAssembly zna:
 * liczby i wskaźniki **już sprawdzone** wobec granic pamięci modułu.
 *
 * **Po co.** wasm3 i WAMR podają argumenty zupełnie inaczej: pierwszy przez
 * makra zdejmujące ze stosu, drugi przez zwykłe argumenty funkcji C. Gdyby
 * treść siedziała w tych makrach, powstałyby dwie implementacje `gpio_write`,
 * dwie `adc_mv` i dwie okazje, żeby się rozjechały — a rozjazd tutaj oznacza
 * moduł, który na jednej płytce robi co innego niż na drugiej.
 *
 * Tutaj treść jest jedna. Po stronie silnika zostaje wyłącznie zdjęcie
 * argumentów i sprawdzenie wskaźników — czyli dokładnie to, co jest w każdym
 * runtimie inne.
 *
 * Funkcje są `inline`, więc nie ma tu warstwy pośredniej w kodzie wynikowym.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/script/Bindings.hpp"

namespace hydra {
namespace script {
namespace wasmapi {

// --- rdzeń -----------------------------------------------------------------

inline u32 millis() { return static_cast<u32>(rtos::nowMs()); }
inline u32 micros() { return static_cast<u32>(rtos::nowUs()); }
inline void delay(u32 ms) { rtos::delayMs(ms); }

// --- log -------------------------------------------------------------------

/** `text` nie jest zakończony zerem — pamięć liniowa modułu terminatorów nie ma. */
inline void logText(u32 level, const char* text, u32 length) {
    char line[HYDRA_LOG_LINE_MAX];
    const size_t n = length < sizeof(line) - 1 ? length : sizeof(line) - 1;
    memcpy(line, text, n);
    line[n] = '\0';

    const LogLevel lv = (level <= static_cast<u32>(LogLevel::Error))
                            ? static_cast<LogLevel>(level)
                            : LogLevel::Info;
    HYDRA_LOG_AT(lv, "wasm", "%s", line);
}

// --- GPIO ------------------------------------------------------------------
//
// Tryb pinu jest liczbą, a nie napisem jak w Lua: napis kosztowałby parę
// (offset, długość) i porównanie tekstu przy każdym wywołaniu, a wartości
// `hal::PinMode` są i tak częścią API frameworka.

inline u32 gpioMode(u32 pin, u32 mode) {
    if (mode > static_cast<u32>(hal::PinMode::Analog)) return 0;
    return hal::Hal::gpio().configure(static_cast<hal::PinNum>(pin),
                                      static_cast<hal::PinMode>(mode)) ? 1u : 0u;
}

inline u32 gpioWrite(u32 pin, u32 value) {
    return hal::Hal::gpio().write(static_cast<hal::PinNum>(pin), value != 0) ? 1u : 0u;
}

/**
 * Stan pinu albo -1 przy błędzie.
 *
 * WebAssembly nie zwraca dwóch wyników, więc para (wartość, błąd) znana z Lua
 * odpada. Stan pinu jest zawsze 0 albo 1, więc wartość spoza zakresu jest
 * jednoznaczna i nie da się jej pomylić z odczytem.
 */
inline i32 gpioRead(u32 pin) {
    auto v = hal::Hal::gpio().read(static_cast<hal::PinNum>(pin));
    return v ? (*v ? 1 : 0) : -1;
}

inline u32 gpioToggle(u32 pin) {
    return hal::Hal::gpio().toggle(static_cast<hal::PinNum>(pin)) ? 1u : 0u;
}

// --- ADC -------------------------------------------------------------------

inline i32 adcRaw(u32 pin) {
    auto v = hal::Hal::adc().readRaw(static_cast<hal::PinNum>(pin));
    return v ? static_cast<i32>(*v) : -1;
}

inline i32 adcMv(u32 pin) {
    auto v = hal::Hal::adc().readMv(static_cast<hal::PinNum>(pin));
    return v ? static_cast<i32>(*v) : -1;
}

// --- PWM -------------------------------------------------------------------

inline u32 pwmSetup(u32 pin, u32 freqHz) {
    return hal::Hal::pwm().configure(static_cast<hal::PinNum>(pin), freqHz, 10) ? 1u : 0u;
}

inline u32 pwmDuty(u32 pin, u32 permille) {
    return hal::Hal::pwm().setDutyPermille(static_cast<hal::PinNum>(pin),
                                           static_cast<u16>(permille)) ? 1u : 0u;
}

inline u32 pwmUs(u32 pin, u32 us) {
    return hal::Hal::pwm().writeMicroseconds(static_cast<hal::PinNum>(pin),
                                             static_cast<u16>(us)) ? 1u : 0u;
}

inline u32 pwmRelease(u32 pin) {
    return hal::Hal::pwm().release(static_cast<hal::PinNum>(pin)) ? 1u : 0u;
}

// --- zdarzenia -------------------------------------------------------------

/**
 * Skrót nazwy liczony nad parą (offset, długość).
 *
 * Musi dawać dokładnie to samo, co `nameId()` z `Events.hpp` — po tej samej
 * liczbie host rozpoznaje sygnał. Osobna funkcja, bo tamta chodzi po napisie
 * zakończonym zerem, a w pamięci liniowej modułu zera nie ma.
 */
inline u16 nameIdOfSpan(const char* text, u32 length) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < length; ++i) {
        h ^= static_cast<u8>(text[i]);
        h *= 16777619u;
    }
    return static_cast<u16>((h >> 16) ^ (h & 0xFFFF));
}

inline void eventEmit(const char* namePtr, u32 nameLen, float value, i32 data) {
    ScriptSignal signal{};
    signal.nameId = nameIdOfSpan(namePtr, nameLen);
    signal.value  = value;
    signal.data   = data;
    EventBus::publish(signal);
}

// --- I2C -------------------------------------------------------------------
//
// Wskaźniki są tu **już sprawdzone** wobec granic pamięci modułu — robi to
// silnik, bo każdy runtime robi to inaczej.

inline u32 i2cPing(u32 bus, u32 addr) {
    bool present = false;
    (void)hal::Hal::i2c(static_cast<u8>(bus)).transaction([&present, addr](hal::II2cBus::Session& s) {
        present = s.ping(static_cast<u8>(addr)).has_value();
        return ok();
    });
    return present ? 1u : 0u;
}

inline i32 i2cScan(u32 bus, u8* out, u32 capacity) {
    if (capacity == 0 || capacity > 128) return -1;
    auto found = hal::Hal::i2c(static_cast<u8>(bus)).scan(out, static_cast<u8>(capacity));
    return found ? static_cast<i32>(*found) : -1;
}

inline i32 i2cRead(u32 bus, u32 addr, u32 reg, u8* out, u32 length) {
    if (length == 0) return -1;

    // Argumenty w strukturze, a w domknięciu jeden wskaźnik: `Delegate` ma
    // stałą pojemność, a przechwycenie pięciu zmiennych ją przekracza.
    struct Op { u8 addr; u8 reg; u8* out; u32 len; bool done; } op{
        static_cast<u8>(addr), static_cast<u8>(reg), out, length, false};

    (void)hal::Hal::i2c(static_cast<u8>(bus)).transaction([&op](hal::II2cBus::Session& s) {
        op.done = s.readReg(op.addr, op.reg, ByteSpan{op.out, op.len}).has_value();
        return ok();
    });
    return op.done ? static_cast<i32>(length) : -1;
}

inline i32 i2cWrite(u32 bus, u32 addr, u32 reg, const u8* data, u32 length) {
    struct Op { u8 addr; u8 reg; const u8* data; u32 len; bool done; } op{
        static_cast<u8>(addr), static_cast<u8>(reg), data, length, false};

    (void)hal::Hal::i2c(static_cast<u8>(bus)).transaction([&op](hal::II2cBus::Session& s) {
        op.done = s.writeReg(op.addr, op.reg, CByteSpan{op.data, op.len}).has_value();
        return ok();
    });
    return op.done ? 1 : -1;
}

}  // namespace wasmapi
}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
