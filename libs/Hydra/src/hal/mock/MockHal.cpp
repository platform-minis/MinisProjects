/**
 * Hydra — implementacja atrapowego backendu HAL (build hostowy).
 */

#include "hydra/hal/Mock.hpp"

#if HYDRA_PLAT_HOST

#include <string.h>

namespace hydra {
namespace hal {
namespace mock {

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------

Status MockGpio::configure(PinNum pin, PinMode mode) {
    if (!valid(pin)) return fail(Err::BadArgument);
    pins_[pin].mode       = mode;
    pins_[pin].configured = true;
    return ok();
}

Status MockGpio::write(PinNum pin, bool high) {
    if (!valid(pin)) return fail(Err::BadArgument);
    if (!pins_[pin].configured) return fail(Err::NotInitialized);
    pins_[pin].level = high;
    ++pins_[pin].writes;
    return ok();
}

Result<bool> MockGpio::read(PinNum pin) {
    if (!valid(pin)) return unexpected(Err::BadArgument);
    if (!pins_[pin].configured) return unexpected(Err::NotInitialized);
    return pins_[pin].level;
}

Status MockGpio::attachInterrupt(PinNum pin, Edge edge, IsrHandler handler, void* arg) {
    if (!valid(pin) || !handler) return fail(Err::BadArgument);
    pins_[pin].edge   = edge;
    pins_[pin].isr    = handler;
    pins_[pin].isrArg = arg;
    return ok();
}

Status MockGpio::detachInterrupt(PinNum pin) {
    if (!valid(pin)) return fail(Err::BadArgument);
    pins_[pin].edge   = Edge::None;
    pins_[pin].isr    = nullptr;
    pins_[pin].isrArg = nullptr;
    return ok();
}

void MockGpio::setInputLevel(PinNum pin, bool high) {
    if (valid(pin)) pins_[pin].level = high;
}

bool MockGpio::triggerInterrupt(PinNum pin) {
    if (!valid(pin) || !pins_[pin].isr) return false;
    pins_[pin].isr(pins_[pin].isrArg);
    return true;
}

const MockGpio::PinState& MockGpio::state(PinNum pin) const {
    static const PinState none{};
    return valid(pin) ? pins_[pin] : none;
}

void MockGpio::clear() {
    for (auto& p : pins_) p = PinState{};
}

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------

MockI2c::Device* MockI2c::find(u8 addr) {
    for (auto& d : devices_) {
        if (d.present && d.addr == addr) return &d;
    }
    return nullptr;
}

Status MockI2c::addDevice(u8 addr) {
    if (find(addr)) return ok();
    for (auto& d : devices_) {
        if (d.present) continue;
        d = Device{};
        d.addr    = addr;
        d.present = true;
        return ok();
    }
    return fail(Err::OutOfMemory);
}

void MockI2c::removeDevice(u8 addr) {
    if (Device* d = find(addr)) d->present = false;
}

MockI2c::Device* MockI2c::device(u8 addr) { return find(addr); }

namespace {
/** Przelicza adres rejestru na indeks w magazynie, zależnie od szerokości. */
size_t regSlot(u8 reg, u8 width) { return static_cast<size_t>(reg) * width; }
}  // namespace

void MockI2c::setReg(u8 addr, u8 reg, u8 value) {
    Device* d = find(addr);
    if (d) d->regs[reg] = value;
}

void MockI2c::setWordRegisters(u8 addr, bool on) {
    Device* d = find(addr);
    if (d) d->regWidth = on ? 2 : 1;
}

void MockI2c::setReg16(u8 addr, u8 reg, u16 value) {
    Device* d = find(addr);
    if (!d) return;
    const size_t slot = regSlot(reg, d->regWidth);
    if (slot + 1 >= kRegCount) return;
    d->regs[slot]     = static_cast<u8>(value >> 8);
    d->regs[slot + 1] = static_cast<u8>(value & 0xFF);
}

Result<u16> MockI2c::getReg16(u8 addr, u8 reg) {
    Device* d = find(addr);
    if (!d) return unexpected(Err::NotFound);
    const size_t slot = regSlot(reg, d->regWidth);
    if (slot + 1 >= kRegCount) return unexpected(Err::OutOfRange);
    return static_cast<u16>(static_cast<u16>(d->regs[slot]) << 8 | d->regs[slot + 1]);
}

Result<u8> MockI2c::getReg(u8 addr, u8 reg) {
    Device* d = find(addr);
    if (!d) return unexpected(Err::NotFound);
    return d->regs[reg];  // u8 nie wyjdzie poza 256-elementową tablicę
}

void MockI2c::failNext(u32 count, Err error) {
    failCount_ = count;
    failErr_   = error;
}

Status MockI2c::takeFailure() {
    if (failCount_ == 0) return ok();
    --failCount_;
    return fail(failErr_);
}

Status MockI2c::doWrite(u8 addr, CByteSpan data) {
    HYDRA_CHECK(takeFailure());
    Device* d = find(addr);
    if (!d) return fail(Err::NotFound);
    ++d->writes;

    // Zapis rejestrowy: pierwszy bajt to adres rejestru, reszta to dane.
    if (data.size() >= 2) {
        const u8 reg = data[0];
        d->regPtr    = reg;
        const size_t base = regSlot(reg, d->regWidth);
        for (size_t i = 1; i < data.size(); ++i) {
            const size_t target = base + i - 1;
            if (target < kRegCount) d->regs[target] = data[i];
        }
    } else if (data.size() == 1) {
        // Sam adres rejestru — układ zapamiętuje go do kolejnego odczytu.
        d->regPtr = data[0];
    }
    return ok();
}

Status MockI2c::doRead(u8 addr, ByteSpan out) {
    HYDRA_CHECK(takeFailure());
    Device* d = find(addr);
    if (!d) return fail(Err::NotFound);
    ++d->reads;

    const size_t base = regSlot(d->regPtr, d->regWidth);
    for (size_t i = 0; i < out.size(); ++i) {
        const size_t src = base + i;
        out[i] = src < kRegCount ? d->regs[src] : 0;
    }
    return ok();
}

Status MockI2c::doWriteRead(u8 addr, CByteSpan tx, ByteSpan rx) {
    HYDRA_CHECK(takeFailure());
    Device* d = find(addr);
    if (!d) return fail(Err::NotFound);
    ++d->reads;

    const size_t base = regSlot(tx.empty() ? 0 : tx[0], d->regWidth);
    for (size_t i = 0; i < rx.size(); ++i) {
        const size_t src = base + i;
        rx[i] = src < kRegCount ? d->regs[src] : 0;
    }
    return ok();
}

void MockI2c::clear() {
    for (auto& d : devices_) d = Device{};
    failCount_ = 0;
}

// ---------------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------------

void MockSpi::queueResponse(CByteSpan data) {
    rxLen_ = data.size() < kBufSize ? data.size() : kBufSize;
    if (rxLen_) memcpy(rxBuf_, data.data(), rxLen_);
    rxPos_ = 0;
}

Status MockSpi::doConfigure(const SpiConfig& cfg) {
    cfg_ = cfg;
    return ok();
}

Status MockSpi::doTransfer(CByteSpan tx, ByteSpan rx) {
    for (size_t i = 0; i < tx.size() && txLen_ < kBufSize; ++i) txBuf_[txLen_++] = tx[i];
    for (size_t i = 0; i < rx.size(); ++i) {
        rx[i] = rxPos_ < rxLen_ ? rxBuf_[rxPos_++] : 0xFF;
    }
    return ok();
}

void MockSpi::clear() {
    txLen_ = rxLen_ = rxPos_ = 0;
    cfg_ = SpiConfig{};
}

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------

Status MockUart::begin(const UartConfig& cfg) {
    cfg_  = cfg;
    open_ = true;
    return ok();
}

void MockUart::end() { open_ = false; }

size_t MockUart::available() { return rxLen_ - rxPos_; }

void MockUart::flush() {}

void MockUart::inject(CByteSpan data) {
    for (size_t i = 0; i < data.size() && rxLen_ < kBufSize; ++i) rxBuf_[rxLen_++] = data[i];
}

size_t MockUart::doWrite(CByteSpan data) {
    if (!open_) return 0;
    size_t n = 0;
    while (n < data.size() && txLen_ < kBufSize) txBuf_[txLen_++] = data[n++];
    return n;
}

size_t MockUart::doRead(ByteSpan out) {
    if (!open_) return 0;
    size_t n = 0;
    while (n < out.size() && rxPos_ < rxLen_) out[n++] = rxBuf_[rxPos_++];
    return n;
}

void MockUart::clear() {
    txLen_ = rxLen_ = rxPos_ = 0;
    open_  = false;
    cfg_   = UartConfig{};
}

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------

MockPwm::Channel* MockPwm::find(PinNum pin) {
    for (auto& c : channels_) {
        if (c.active && c.pin == pin) return &c;
    }
    return nullptr;
}

const MockPwm::Channel* MockPwm::find(PinNum pin) const {
    for (const auto& c : channels_) {
        if (c.active && c.pin == pin) return &c;
    }
    return nullptr;
}

Status MockPwm::configure(PinNum pin, u32 freqHz, u8 resolutionBits) {
    if (pin == kNoPin || freqHz == 0) return fail(Err::BadArgument);
    Channel* c = find(pin);
    if (!c) {
        for (auto& ch : channels_) {
            if (ch.active) continue;
            c = &ch;
            break;
        }
    }
    if (!c) return fail(Err::OutOfMemory);

    c->pin        = pin;
    c->freqHz     = freqHz;
    c->resolution = resolutionBits;
    c->active     = true;
    return ok();
}

Status MockPwm::setDutyPermille(PinNum pin, u16 permille) {
    if (permille > 1000) return fail(Err::OutOfRange);
    Channel* c = find(pin);
    if (!c) return fail(Err::NotInitialized);
    c->permille = permille;
    return ok();
}

Status MockPwm::release(PinNum pin) {
    Channel* c = find(pin);
    if (!c) return fail(Err::NotFound);
    *c = Channel{};
    return ok();
}

u32 MockPwm::frequencyHz(PinNum pin) const {
    const Channel* c = find(pin);
    return c ? c->freqHz : 0;
}

const MockPwm::Channel& MockPwm::channel(PinNum pin) const {
    const Channel* c = find(pin);
    return c ? *c : none_;
}

void MockPwm::clear() {
    for (auto& c : channels_) c = Channel{};
}

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------

Status MockAdc::configure(PinNum pin, const AdcConfig&) {
    for (auto& e : entries_) {
        if (e.pin == pin) return ok();
    }
    for (auto& e : entries_) {
        if (e.pin != kNoPin) continue;
        e.pin = pin;
        e.mv  = 0;
        return ok();
    }
    return fail(Err::OutOfMemory);
}

void MockAdc::setPinMv(PinNum pin, u16 mv) {
    for (auto& e : entries_) {
        if (e.pin == pin) {
            e.mv = mv;
            return;
        }
    }
    for (auto& e : entries_) {
        if (e.pin != kNoPin) continue;
        e.pin = pin;
        e.mv  = mv;
        return;
    }
}

Result<u16> MockAdc::readPinMv(PinNum pin) {
    for (const auto& e : entries_) {
        if (e.pin == pin) return e.mv;
    }
    return unexpected(Err::NotInitialized);
}

Result<u16> MockAdc::readRaw(PinNum pin) {
    auto mv = readPinMv(pin);
    if (!mv) return unexpected(mv.error());
    // Odwzorowanie 0–3300 mV na 12 bitów, jak w typowym przetworniku.
    return static_cast<u16>((static_cast<u32>(*mv) * 4095u) / 3300u);
}

void MockAdc::clear() {
    for (auto& e : entries_) e = Entry{};
}

// ---------------------------------------------------------------------------
// Pamięć trwała
// ---------------------------------------------------------------------------

MockStorage::Entry* MockStorage::find(const char* key) {
    if (!key) return nullptr;
    for (auto& e : entries_) {
        if (e.used && strncmp(e.key, key, kStorageKeyMax) == 0) return &e;
    }
    return nullptr;
}

Status MockStorage::begin(const char* nameSpace, bool readOnly) {
    if (!nameSpace) return fail(Err::BadArgument);
    // Zmiana przestrzeni nazw czyści widok — tak samo jak otwarcie innego
    // namespace w NVS pokazuje inny zbiór kluczy.
    if (strncmp(ns_, nameSpace, kStorageKeyMax) != 0) {
        for (auto& e : entries_) e = Entry{};
        strncpy(ns_, nameSpace, kStorageKeyMax);
        ns_[kStorageKeyMax] = '\0';
    }
    open_     = true;
    readOnly_ = readOnly;
    return ok();
}

void MockStorage::end() { open_ = false; }

Status MockStorage::setBlob(const char* key, CByteSpan data) {
    if (!open_) return fail(Err::NotInitialized);
    if (readOnly_) return fail(Err::NotSupported);
    if (!key || strlen(key) > kStorageKeyMax) return fail(Err::BadArgument);
    if (data.size() > kValueMax) return fail(Err::OutOfRange);

    Entry* e = find(key);
    if (!e) {
        for (auto& slot : entries_) {
            if (slot.used) continue;
            e = &slot;
            break;
        }
    }
    if (!e) return fail(Err::OutOfMemory);

    strncpy(e->key, key, kStorageKeyMax);
    e->key[kStorageKeyMax] = '\0';
    memcpy(e->value, data.data(), data.size());
    e->size = data.size();
    e->used = true;
    return ok();
}

Result<size_t> MockStorage::getBlob(const char* key, ByteSpan out) {
    if (!open_) return unexpected(Err::NotInitialized);
    Entry* e = find(key);
    if (!e) return unexpected(Err::NotFound);

    const size_t n = e->size < out.size() ? e->size : out.size();
    if (n) memcpy(out.data(), e->value, n);
    return e->size;
}

Status MockStorage::erase(const char* key) {
    if (!open_) return fail(Err::NotInitialized);
    Entry* e = find(key);
    if (!e) return fail(Err::NotFound);
    *e = Entry{};
    return ok();
}

Status MockStorage::eraseAll() {
    if (!open_) return fail(Err::NotInitialized);
    for (auto& e : entries_) e = Entry{};
    return ok();
}

bool MockStorage::has(const char* key) { return open_ && find(key) != nullptr; }

Status MockStorage::commit() {
    ++commits_;
    return ok();
}

void MockStorage::clear() {
    for (auto& e : entries_) e = Entry{};
    ns_[0]   = '\0';
    open_    = false;
    commits_ = 0;
}

// ---------------------------------------------------------------------------
// Czas
// ---------------------------------------------------------------------------

Result<u64> MockTime::epochSec() const {
    if (!synced_) return unexpected(Err::NotInitialized);
    // Zegar idzie razem z czasem monotonicznym od chwili ustawienia.
    return epoch_ + (rtos::nowMs() - setAtMs_) / 1000u;
}

Status MockTime::setEpochSec(u64 epoch) {
    epoch_   = epoch;
    setAtMs_ = rtos::nowMs();
    synced_  = true;
    return ok();
}

void MockTime::clear() {
    epoch_   = 0;
    setAtMs_ = 0;
    synced_  = false;
}

// ---------------------------------------------------------------------------
// Rejestracja
// ---------------------------------------------------------------------------

void Backend::clear() {
    gpio.clear();
    i2c.clear();
    spi.clear();
    uart.clear();
    pwm.clear();
    adc.clear();
    storage.clear();
    time.clear();
}

Backend& backend() {
    static Backend b;
    return b;
}

Status install(ResetReason reason) {
    Backend& b = backend();

    Drivers d;
    d.gpio        = &b.gpio;
    d.i2c[0]      = &b.i2c;
    d.spi[0]      = &b.spi;
    d.uart[0]     = &b.uart;
    d.pwm         = &b.pwm;
    d.adc         = &b.adc;
    d.storage     = &b.storage;
    d.time        = &b.time;
    d.resetReason = reason;
    d.name        = "mock";
    return Hal::install(d);
}

}  // namespace mock

/** Na hoście domyślnym backendem są atrapy. */
Status installDefaultBackend() { return mock::install(); }

}  // namespace hal
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST
