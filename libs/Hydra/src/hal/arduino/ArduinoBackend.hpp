#pragma once
/**
 * Hydra — prywatne deklaracje backendu Arduino.
 *
 * JEDYNY katalog w całym projekcie, w którym wolno włączać nagłówki Arduino
 * (rozdz. 3, reguła 2). Pilnuje tego tools/check_includes.sh uruchamiany w CI.
 *
 * Nagłówek nie jest instalowany do include/ — nie jest częścią publicznego API.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "hydra/hal/Hal.hpp"

/**
 * Generacja rdzenia arduino-esp32.
 *
 * Wersje 2.x i 3.x różnią się w miejscach, których nie da się obejść jednym
 * zapisem — przede wszystkim w LEDC: 2.x operuje numerem kanału, 3.x przypina
 * kanał do pinu samodzielnie. Framework nie powinien wymuszać wersji rdzenia,
 * więc rozróżniamy je tutaj i obsługujemy obie.
 *
 * Nagłówek esp_arduino_version.h istnieje dopiero od 2.0.3; jego brak też jest
 * informacją — to rdzeń starszy niż 3.x.
 */
#if HYDRA_PLAT_ESP32
#  if __has_include(<esp_arduino_version.h>)
#    include <esp_arduino_version.h>
#  endif
#  if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#    define HYDRA_ESP_ARDUINO_3 1
#  else
#    define HYDRA_ESP_ARDUINO_3 0
#  endif
#endif

namespace hydra {
namespace hal {
namespace arduino {

#if HYDRA_PLAT_ESP32
/**
 * Kontroler I2S ESP32.
 *
 * Zwracany jako interfejs, nie jako typ konkretny: definicja siedzi
 * w ArduinoI2s.cpp razem z nagłówkami ESP-IDF, a te nie mają prawa wejść
 * do reszty backendu (reguła 2 z check_includes.sh).
 */
II2s& i2sBackend();
#endif

// ---------------------------------------------------------------------------

class ArduinoGpio : public IGpio {
public:
    Status configure(PinNum pin, PinMode mode) override;
    Status write(PinNum pin, bool high) override;
    Result<bool> read(PinNum pin) override;
    Status attachInterrupt(PinNum pin, Edge edge, IsrHandler handler, void* arg) override;
    Status detachInterrupt(PinNum pin) override;
};

// ---------------------------------------------------------------------------

class ArduinoPwm : public IPwm {
public:
    static constexpr u8 kMaxChannels = 8;

    Status configure(PinNum pin, u32 freqHz, u8 resolutionBits) override;
    Status setDutyPermille(PinNum pin, u16 permille) override;
    Status release(PinNum pin) override;
    u32    frequencyHz(PinNum pin) const override;

private:
    struct Channel {
        PinNum pin        = kNoPin;
        u32    freqHz     = 0;
        u8     resolution = 10;
        bool   active     = false;
        /** Numer kanału LEDC — używany tylko przez rdzeń ESP32 w wersji 2.x. */
        u8     ledcChannel = 0;
    };
    Channel* find(PinNum pin);
    const Channel* find(PinNum pin) const;
    Channel channels_[kMaxChannels];
};

// ---------------------------------------------------------------------------

class ArduinoAdc : public IAdc {
public:
    Status configure(PinNum pin, const AdcConfig& cfg) override;
    Result<u16> readRaw(PinNum pin) override;
    Result<u16> readPinMv(PinNum pin) override;
    u8 resolutionBits() const override { return resolution_; }

private:
    u8 resolution_ = 12;
    u8 samples_    = 1;
};

// ---------------------------------------------------------------------------

class ArduinoI2c : public II2cBus {
public:
    /** wire: 0 = Wire, 1 = Wire1 (o ile platforma ją ma). */
    Status begin(u8 wireIndex, PinNum sda, PinNum scl, u32 clockHz);
    u32 clockHz() const override { return clockHz_; }

protected:
    Status doWrite(u8 addr, CByteSpan data) override;
    Status doRead(u8 addr, ByteSpan out) override;
    Status doWriteRead(u8 addr, CByteSpan tx, ByteSpan rx) override;

private:
    TwoWire* wire_    = nullptr;
    u32      clockHz_ = 100000;
};

// ---------------------------------------------------------------------------

class ArduinoSpi : public ISpiBus {
public:
    Status begin(PinNum sck, PinNum miso, PinNum mosi);

protected:
    Status doConfigure(const SpiConfig& cfg) override;
    Status doTransfer(CByteSpan tx, ByteSpan rx) override;

private:
    bool      started_ = false;
    SpiConfig cfg_{};
};

// ---------------------------------------------------------------------------

/**
 * Port szeregowy. Dwa konstruktory, bo `Serial` nie wszędzie znaczy to samo:
 * na ESP32-S3 z USB CDC to HWCDC, na arduino-pico SerialUSB, a na stm32duino
 * zwykły HardwareSerial. Wszystkie trzy dziedziczą po Stream, więc wejście
 * i wyjście obsługujemy przez Stream, a prędkość i piny ustawiamy tylko tam,
 * gdzie jest co ustawiać. Przeciążenie wybiera się samo — dla prawdziwego
 * UART-u wariant HardwareSerial* pasuje lepiej niż Stream*.
 */
class ArduinoUart : public IUart {
public:
    explicit ArduinoUart(HardwareSerial* port) : hw_(port), io_(port) {}
    explicit ArduinoUart(Stream* stream) : hw_(nullptr), io_(stream) {}

#if HYDRA_PLAT_RP2
    /**
     * Wariant dla sprzętowego UART-u na RP2040/RP2350.
     *
     * Przypisanie pinów (setRX/setTX) udostępnia dopiero SerialUART, a nie
     * wspólna klasa bazowa HardwareSerial — trzymanie samego wskaźnika na
     * bazę wystarcza do transmisji, ale nie pozwala skonfigurować wyprowadzeń.
     */
    explicit ArduinoUart(SerialUART* port) : hw_(port), io_(port), uart_(port) {}
#endif

    Status begin(const UartConfig& cfg) override;
    void   end() override;
    size_t available() override;
    void   flush() override;

protected:
    size_t doWrite(CByteSpan data) override;
    size_t doRead(ByteSpan out) override;

private:
    HardwareSerial* hw_;  ///< nullptr, gdy port jest wirtualny (USB CDC)
    Stream*         io_;
#if HYDRA_PLAT_RP2
    SerialUART*     uart_ = nullptr;  ///< niepusty tylko dla sprzętowego UART-u
#endif
};

// ---------------------------------------------------------------------------

class ArduinoStorage : public IStorage {
public:
    Status begin(const char* nameSpace, bool readOnly) override;
    void   end() override;
    Status setBlob(const char* key, CByteSpan data) override;
    Result<size_t> getBlob(const char* key, ByteSpan out) override;
    Status erase(const char* key) override;
    Status eraseAll() override;
    bool   has(const char* key) override;
    Status commit() override;

private:
    bool open_ = false;
};

// ---------------------------------------------------------------------------

class ArduinoTime : public ITime {
public:
    bool synchronized() const override { return synced_; }
    Result<u64> epochSec() const override;
    Status setEpochSec(u64 epoch) override;

private:
    u64  epoch_    = 0;
    u32  setAtMs_  = 0;
    bool synced_   = false;
};

// ---------------------------------------------------------------------------

/** Odczyt przyczyny resetu z rejestrów platformy (rozdz. 13). */
ResetReason readResetReason();

}  // namespace arduino
}  // namespace hal
}  // namespace hydra

#endif  // !HYDRA_PLAT_HOST
