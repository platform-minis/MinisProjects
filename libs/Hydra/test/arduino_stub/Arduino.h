#pragma once
/**
 * Hydra — atrapa nagłówków Arduino do sprawdzania SKŁADNI backendu.
 *
 * NIE jest to emulacja Arduino i nie służy do uruchamiania czegokolwiek.
 * Jedyne zadanie: pozwolić kompilatorowi przejść przez pliki backendu Arduino
 * na maszynie bez toolchaina embedded, żeby literówki i błędy typów wychodziły
 * od razu, a nie dopiero w CI.
 *
 * Autorytetem pozostaje build `pio ci` na prawdziwych corach — sygnatury tutaj
 * odwzorowują arduino-esp32 3.x i mogą się różnić w szczegółach od pozostałych
 * platform.
 */

#include <stddef.h>
#include <stdint.h>

#include <functional>

// --- stałe -----------------------------------------------------------------
#define HIGH 1
#define LOW  0
#define INPUT           0x01
#define OUTPUT          0x03
#define INPUT_PULLUP    0x05
#define INPUT_PULLDOWN  0x09
#define OUTPUT_OPEN_DRAIN 0x13
// ANALOG celowo niezdefiniowane — sprawdzamy w ten sposób ścieżkę zapasową
// w toArduinoMode() dla corów, które tego trybu nie mają.

#define RISING  0x01
#define FALLING 0x02
#define CHANGE  0x03
#define ONLOW   0x04
#define ONHIGH  0x05

#define MSBFIRST 1
#define LSBFIRST 0

#define SERIAL_8N1 0x800001c
#define SERIAL_8N2 0x8000020
#define SERIAL_8E1 0x800003c
#define SERIAL_8O1 0x800005c

using u8_t = uint8_t;

// --- funkcje rdzenia -------------------------------------------------------
void     pinMode(uint8_t pin, uint8_t mode);
void     digitalWrite(uint8_t pin, uint8_t value);
int      digitalRead(uint8_t pin);
int      analogRead(uint8_t pin);
void     analogWrite(uint8_t pin, int value);
uint32_t millis();
uint32_t micros();
uint8_t  digitalPinToInterrupt(uint8_t pin);
void     detachInterrupt(uint8_t irq);
void     attachInterruptArg(uint8_t irq, void (*fn)(void*), void* arg, int mode);

void analogReadResolution(uint8_t bits);
// --- warianty specyficzne dla arduino-pico (Philhower) ---
using PinStatus = int;
void analogWriteFreq(uint32_t hz);
void analogWriteRange(uint32_t range);
void attachInterruptParam(uint8_t irq, void (*fn)(void*), PinStatus mode, void* arg);
// --- wariant stm32duino: callback jako std::function ---
void attachInterrupt(uint8_t irq, std::function<void(void)> cb, int mode);
void analogWriteResolution(uint8_t bits);
void analogWriteFrequency(uint32_t hz);

// --- specyficzne dla ESP32 -------------------------------------------------
enum adc_attenuation_t { ADC_0db, ADC_2_5db, ADC_6db, ADC_11db };
void     analogSetPinAttenuation(uint8_t pin, adc_attenuation_t att);
uint32_t analogReadMilliVolts(uint8_t pin);
// Rdzeń 3.x: kanał przypisywany do pinu automatycznie.
bool     ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool     ledcWrite(uint8_t pin, uint32_t duty);
bool     ledcDetach(uint8_t pin);
// Rdzeń 2.x: numer kanału podawany jawnie.
uint32_t ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution);
void     ledcAttachPin(uint8_t pin, uint8_t channel);
void     ledcDetachPin(uint8_t pin);
#define IRAM_ATTR

// --- strumienie ------------------------------------------------------------
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) { return 0; }
    virtual size_t write(const uint8_t* buf, size_t len);
    virtual void   flush() {}
};

class Stream : public Print {
public:
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
};

class HardwareSerial : public Stream {
public:
    void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rx = -1,
               int8_t tx = -1);
    void begin(unsigned long baud, uint8_t config);
    void end();
};

#ifdef ARDUINO_ARCH_RP2040
/**
 * Sprzętowy UART rdzenia Philhowera.
 *
 * setRX/setTX należą właśnie tutaj, a nie do HardwareSerial — wcześniej atrapa
 * trzymała je w klasie bazowej i dlatego przepuszczała kod, który przy
 * prawdziwej budowie się nie kompilował. Atrapa ma odwzorowywać rdzeń,
 * a nie ułatwiać życie.
 */
class SerialUART : public HardwareSerial {
public:
    void setRX(uint8_t pin);
    void setTX(uint8_t pin);
};
#endif

extern HardwareSerial Serial;

// --- nazwy pinów wariantu STM32 -------------------------------------------
// W prawdziwym stm32duino dostarcza je nagłówek wariantu płytki.
#ifdef ARDUINO_ARCH_STM32
#  define LED_BUILTIN 13
#  define PA5 5
#  define PA6 6
#  define PA7 7
#  define PB8 8
#  define PB9 9
#endif

// --- minimalne atrapy stosu sieciowego (patrz uwaga na górze pliku) --------
class IPAddress {
public:
    IPAddress() = default;
    operator uint32_t() const { return raw_; }
    bool operator!=(const IPAddress& o) const { return raw_ != o.raw_; }
private:
    uint32_t raw_ = 0;
};
