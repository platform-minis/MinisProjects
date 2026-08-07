/**
 * Hydra — backend Arduino: GPIO, PWM, ADC.
 *
 * Trzy peryferia, trzy różne sposoby, w jakie cory Arduino rozjeżdżają się
 * między platformami:
 *   - przerwania: ESP32 ma attachInterruptArg, Philhower attachInterruptParam,
 *     stm32duino przyjmuje std::function,
 *   - PWM: ESP32 3.x to ledcAttach na pinie, RP2 to analogWriteFreq/Range,
 *     STM32 analogWriteFrequency,
 *   - ADC: ESP32 daje skalibrowane analogReadMilliVolts, reszta surowe
 *     zliczenia, które trzeba przeliczyć samemu.
 */

#include "ArduinoBackend.hpp"

#if !HYDRA_PLAT_HOST

namespace hydra {
namespace hal {
namespace arduino {
namespace {

/** Napięcie odniesienia przetwornika tam, gdzie core nie zwraca miliwoltów. */
[[maybe_unused]] constexpr u32 kVrefMv = 3300;

int toArduinoMode(PinMode mode) {
    switch (mode) {
        case PinMode::Input:           return INPUT;
        case PinMode::InputPullUp:     return INPUT_PULLUP;
        case PinMode::InputPullDown:
#ifdef INPUT_PULLDOWN
            return INPUT_PULLDOWN;
#else
            return INPUT;  // RP2040 przez Arduino API nie wystawia pulldownu
#endif
        case PinMode::Output:          return OUTPUT;
        case PinMode::OutputOpenDrain:
#ifdef OUTPUT_OPEN_DRAIN
            return OUTPUT_OPEN_DRAIN;
#else
            return OUTPUT;
#endif
        case PinMode::Analog:
#ifdef ANALOG
            return ANALOG;
#else
            return INPUT;
#endif
    }
    return INPUT;
}

/**
 * Odwzorowanie rodzaju wyzwalania na stałą Arduino.
 *
 * Zwraca -1, gdy platforma danego trybu nie obsługuje. Wyzwalanie poziomem
 * (ONLOW/ONHIGH) mają wyłącznie rdzenie ESP32; na RP2 i STM32 podstawienie
 * w to miejsce CHANGE wyglądałoby na działające, a dawało lawinę przerwań
 * przy każdej zmianie zamiast jednego zgłoszenia przy utrzymanym poziomie.
 */
int toArduinoEdge(Edge edge) {
    switch (edge) {
        case Edge::Rising:    return RISING;
        case Edge::Falling:   return FALLING;
        case Edge::Both:      return CHANGE;
        case Edge::LevelLow:
#if HYDRA_PLAT_ESP32
            return ONLOW;
#else
            return -1;
#endif
        case Edge::LevelHigh:
#if HYDRA_PLAT_ESP32
            return ONHIGH;
#else
            return -1;
#endif
        case Edge::None:      break;
    }
    return -1;
}

}  // namespace

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------

Status ArduinoGpio::configure(PinNum pin, PinMode mode) {
    if (pin == kNoPin) return fail(Err::BadArgument);
    pinMode(static_cast<u8>(pin), toArduinoMode(mode));
    return ok();
}

Status ArduinoGpio::write(PinNum pin, bool high) {
    if (pin == kNoPin) return fail(Err::BadArgument);
    digitalWrite(static_cast<u8>(pin), high ? HIGH : LOW);
    return ok();
}

Result<bool> ArduinoGpio::read(PinNum pin) {
    if (pin == kNoPin) return unexpected(Err::BadArgument);
    return digitalRead(static_cast<u8>(pin)) == HIGH;
}

Status ArduinoGpio::attachInterrupt(PinNum pin, Edge edge, IsrHandler handler, void* arg) {
    if (pin == kNoPin || !handler || edge == Edge::None) return fail(Err::BadArgument);
    const auto irq  = digitalPinToInterrupt(static_cast<u8>(pin));
    const int  mode = toArduinoEdge(edge);
    if (mode < 0) return fail(Err::NotSupported);

#if HYDRA_PLAT_ESP32
    ::attachInterruptArg(irq, handler, arg, mode);
#elif HYDRA_PLAT_RP2
    ::attachInterruptParam(irq, handler, static_cast<PinStatus>(mode), arg);
#else
    // stm32duino przyjmuje std::function — domknięcie powstaje raz, przy
    // rejestracji, a nie w przerwaniu.
    ::attachInterrupt(irq, [handler, arg]() { handler(arg); }, mode);
#endif
    return ok();
}

Status ArduinoGpio::detachInterrupt(PinNum pin) {
    if (pin == kNoPin) return fail(Err::BadArgument);
    ::detachInterrupt(digitalPinToInterrupt(static_cast<u8>(pin)));
    return ok();
}

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------

ArduinoPwm::Channel* ArduinoPwm::find(PinNum pin) {
    for (auto& c : channels_) {
        if (c.active && c.pin == pin) return &c;
    }
    return nullptr;
}

const ArduinoPwm::Channel* ArduinoPwm::find(PinNum pin) const {
    for (const auto& c : channels_) {
        if (c.active && c.pin == pin) return &c;
    }
    return nullptr;
}

Status ArduinoPwm::configure(PinNum pin, u32 freqHz, u8 resolutionBits) {
    if (pin == kNoPin || freqHz == 0 || resolutionBits == 0 || resolutionBits > 16) {
        return fail(Err::BadArgument);
    }

    Channel* c = find(pin);
    if (!c) {
        for (auto& ch : channels_) {
            if (ch.active) continue;
            c = &ch;
            break;
        }
    }
    if (!c) return fail(Err::OutOfMemory);

#if HYDRA_PLAT_ESP32
#  if HYDRA_ESP_ARDUINO_3
    // Rdzeń 3.x sam przypisuje kanał LEDC do pinu.
    if (!ledcAttach(static_cast<u8>(pin), freqHz, resolutionBits)) {
        return fail(Err::NotSupported);
    }
#  else
    // Rdzeń 2.x wymaga jawnego numeru kanału: najpierw konfiguracja kanału,
    // potem przypięcie pinu. Numer bierzemy z pozycji w tablicy, więc jest
    // stały przez cały czas życia przypisania.
    c->ledcChannel = static_cast<u8>(c - channels_);
    ledcSetup(c->ledcChannel, freqHz, resolutionBits);
    ledcAttachPin(static_cast<u8>(pin), c->ledcChannel);
#  endif
#elif HYDRA_PLAT_RP2
    analogWriteFreq(freqHz);
    analogWriteRange((1u << resolutionBits) - 1u);
    pinMode(static_cast<u8>(pin), OUTPUT);
#else
    analogWriteFrequency(freqHz);
    analogWriteResolution(resolutionBits);
    pinMode(static_cast<u8>(pin), OUTPUT);
#endif

    c->pin        = pin;
    c->freqHz     = freqHz;
    c->resolution = resolutionBits;
    c->active     = true;
    return ok();
}

Status ArduinoPwm::setDutyPermille(PinNum pin, u16 permille) {
    if (permille > 1000) return fail(Err::OutOfRange);
    Channel* c = find(pin);
    if (!c) return fail(Err::NotInitialized);

    const u32 maxValue = (1u << c->resolution) - 1u;
    const u32 duty     = (static_cast<u32>(permille) * maxValue) / 1000u;

#if HYDRA_PLAT_ESP32
#  if HYDRA_ESP_ARDUINO_3
    ledcWrite(static_cast<u8>(pin), duty);
#  else
    ledcWrite(c->ledcChannel, duty);
#  endif
#else
    analogWrite(static_cast<u8>(pin), static_cast<int>(duty));
#endif
    return ok();
}

Status ArduinoPwm::release(PinNum pin) {
    Channel* c = find(pin);
    if (!c) return fail(Err::NotFound);

#if HYDRA_PLAT_ESP32
#  if HYDRA_ESP_ARDUINO_3
    ledcDetach(static_cast<u8>(pin));
#  else
    ledcDetachPin(static_cast<u8>(pin));
#  endif
#else
    analogWrite(static_cast<u8>(pin), 0);
    pinMode(static_cast<u8>(pin), INPUT);
#endif

    *c = Channel{};
    return ok();
}

u32 ArduinoPwm::frequencyHz(PinNum pin) const {
    const Channel* c = find(pin);
    return c ? c->freqHz : 0;
}

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------

Status ArduinoAdc::configure(PinNum pin, const AdcConfig& cfg) {
    if (pin == kNoPin) return fail(Err::BadArgument);
    samples_ = cfg.samples ? cfg.samples : 1;

#if HYDRA_PLAT_ESP32
    analogReadResolution(12);
    resolution_ = 12;
    adc_attenuation_t att = ADC_11db;
    switch (cfg.attenuation) {
        case AdcAttenuation::Db0:   att = ADC_0db;   break;
        case AdcAttenuation::Db2_5: att = ADC_2_5db; break;
        case AdcAttenuation::Db6:   att = ADC_6db;   break;
        case AdcAttenuation::Db11:  att = ADC_11db;  break;
    }
    analogSetPinAttenuation(static_cast<u8>(pin), att);
#else
    // RP2 i STM32 mają liniowy przetwornik 12-bitowy bez regulacji tłumienia.
    analogReadResolution(12);
    resolution_ = 12;
#endif
    return ok();
}

Result<u16> ArduinoAdc::readRaw(PinNum pin) {
    if (pin == kNoPin) return unexpected(Err::BadArgument);

    u32 sum = 0;
    for (u8 i = 0; i < samples_; ++i) sum += static_cast<u32>(analogRead(static_cast<u8>(pin)));
    return static_cast<u16>(sum / samples_);
}

Result<u16> ArduinoAdc::readPinMv(PinNum pin) {
    if (pin == kNoPin) return unexpected(Err::BadArgument);

#if HYDRA_PLAT_ESP32
    // ESP32 ma nieliniowy przetwornik z fabryczną kalibracją w eFuse —
    // przeliczanie zliczeń samodzielnie dałoby błąd rzędu kilku procent.
    u32 sum = 0;
    for (u8 i = 0; i < samples_; ++i) {
        sum += static_cast<u32>(analogReadMilliVolts(static_cast<u8>(pin)));
    }
    return static_cast<u16>(sum / samples_);
#else
    auto raw = readRaw(pin);
    if (!raw) return unexpected(raw.error());
    const u32 maxCount = (1u << resolution_) - 1u;
    return static_cast<u16>((static_cast<u32>(*raw) * kVrefMv) / maxCount);
#endif
}

}  // namespace arduino
}  // namespace hal
}  // namespace hydra

#endif  // !HYDRA_PLAT_HOST
