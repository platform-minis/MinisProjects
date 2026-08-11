#pragma once
/**
 * SerialPort.h — MinisLib
 * Cienka abstrakcja portu szeregowego + zegara.
 *
 * Powód istnienia:
 *  - driver LC29H nie zależy bezpośrednio od Arduino => parser da się
 *    testować jednostkowo na hoście (patrz test/test_nmea_parser.cpp),
 *  - availableForWrite() pozwala wysyłać RTCM bez blokowania loop().
 *
 * Na Arduino używaj ArduinoSerialPort (adapter na HardwareSerial).
 */

#include <stdint.h>
#include <stddef.h>

namespace minis {

/// Interfejs portu bajtowego (UART).
class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    /// Otwarcie portu. rxPin/txPin < 0 = użyj domyślnych pinów platformy.
    virtual bool open(uint32_t baudRate, int rxPin, int txPin) = 0;
    virtual void close() = 0;

    virtual int    available() = 0;
    /// -1 gdy brak danych.
    virtual int    read() = 0;
    virtual size_t write(const uint8_t* buf, size_t len) = 0;
    /// Ile bajtów można zapisać bez blokowania.
    virtual int    availableForWrite() = 0;
    /// Odrzuć zaległe bajty wejściowe.
    virtual void   flushInput() = 0;
};

/// Zegar monotoniczny w ms (odpowiednik millis()).
using MillisFn = uint32_t (*)();
/// Krótkie oczekiwanie (używane tylko w begin()/konfiguracji).
using DelayFn  = void (*)(uint32_t ms);

} // namespace minis

#if defined(ARDUINO)

#include <Arduino.h>
#include <HardwareSerial.h>

namespace minis {

inline uint32_t platformMillis() { return ::millis(); }
inline void     platformDelay(uint32_t ms) { ::delay(ms); }

/// Adapter HardwareSerial -> ISerialPort.
class ArduinoSerialPort final : public ISerialPort {
public:
    explicit ArduinoSerialPort(HardwareSerial& s) : _s(s) {}

    bool open(uint32_t baudRate, int rxPin, int txPin) override {
#if defined(ESP32) || defined(ESP_PLATFORM)
        _s.begin(baudRate, SERIAL_8N1, rxPin, txPin);
#else
        (void)rxPin; (void)txPin;
        _s.begin(baudRate);
#endif
        return true;
    }
    void close() override { _s.end(); }

    int    available() override { return _s.available(); }
    int    read() override { return _s.read(); }
    size_t write(const uint8_t* buf, size_t len) override { return _s.write(buf, len); }
    int    availableForWrite() override { return _s.availableForWrite(); }
    void   flushInput() override { while (_s.available() > 0) (void)_s.read(); }

private:
    HardwareSerial& _s;
};

} // namespace minis

#else // ---------------------------------------------- build hostowy (testy)

#include <chrono>
#include <thread>

namespace minis {

inline uint32_t platformMillis() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

inline void platformDelay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace minis

#endif // ARDUINO
