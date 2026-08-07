/**
 * Hydra — część wspólna warstwy HAL, niezależna od backendu.
 *
 * Tutaj mieszka logika, której nie ma sensu powtarzać w każdym backendzie:
 * blokowanie magistral, składanie transferów rejestrowych, kalibracja ADC,
 * typowane nakładki na pamięć trwałą i konwersje czasu. Backend dostarcza
 * wyłącznie surowe operacje sprzętowe.
 */

#include <string.h>

#include "hydra/hal/Hal.hpp"

namespace hydra {
namespace hal {

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------

namespace {
/** Największy zapis rejestrowy mieszczący się w buforze na stosie. */
constexpr size_t kRegWriteMax = 32;
}  // namespace

Status II2cBus::Session::write(u8 addr, CByteSpan data)  { return bus_.doWrite(addr, data); }
Status II2cBus::Session::read(u8 addr, ByteSpan out)     { return bus_.doRead(addr, out); }

Status II2cBus::Session::writeRead(u8 addr, CByteSpan tx, ByteSpan rx) {
    return bus_.doWriteRead(addr, tx, rx);
}

Status II2cBus::Session::writeReg(u8 addr, u8 reg, CByteSpan data) {
    if (data.size() > kRegWriteMax) return fail(Err::OutOfRange);
    // Adres rejestru i dane muszą pójść jednym transferem — rozdzielenie ich
    // wstawiłoby warunek STOP, którego większość układów nie zaakceptuje.
    u8 buf[kRegWriteMax + 1];
    buf[0] = reg;
    if (!data.empty()) memcpy(buf + 1, data.data(), data.size());
    return bus_.doWrite(addr, CByteSpan{buf, data.size() + 1});
}

Status II2cBus::Session::readReg(u8 addr, u8 reg, ByteSpan out) {
    return bus_.doWriteRead(addr, CByteSpan{&reg, 1}, out);
}

Result<u8> II2cBus::Session::readReg8(u8 addr, u8 reg) {
    u8 value = 0;
    auto r = readReg(addr, reg, ByteSpan{&value, 1});
    if (!r) return unexpected(r.error());
    return value;
}

Status II2cBus::Session::ping(u8 addr) { return bus_.doWrite(addr, CByteSpan{}); }

Status II2cBus::transaction(Body body, u32 timeoutMs) {
    if (!body) return fail(Err::BadArgument);
    rtos::LockGuard guard(mtx_, timeoutMs);
    if (!guard.held()) return fail(Err::Timeout);

    Session session(*this);
    return body(session);
}

Result<u8> II2cBus::scan(u8* found, u8 capacity, u32 timeoutMs) {
    if (!found || capacity == 0) return unexpected(Err::BadArgument);

    rtos::LockGuard guard(mtx_, timeoutMs);
    if (!guard.held()) return unexpected(Err::Timeout);

    u8 count = 0;
    // Adresy 0x00–0x07 i 0x78–0x7F są zarezerwowane przez specyfikację I2C.
    for (u8 addr = 0x08; addr <= 0x77 && count < capacity; ++addr) {
        if (doWrite(addr, CByteSpan{})) found[count++] = addr;
    }
    return count;
}

// ---------------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------------

Status ISpiBus::Session::transfer(CByteSpan tx, ByteSpan rx) {
    return bus_.doTransfer(tx, rx);
}

Status ISpiBus::transaction(PinNum cs, const SpiConfig& cfg, Body body, u32 timeoutMs) {
    if (!body) return fail(Err::BadArgument);
    rtos::LockGuard guard(mtx_, timeoutMs);
    if (!guard.held()) return fail(Err::Timeout);

    HYDRA_CHECK(doConfigure(cfg));

    if (cs != kNoPin) Hal::gpio().write(cs, false);
    Session session(*this);
    const Status result = body(session);
    // CS wraca w górę także po błędzie — inaczej układ zostałby wybrany
    // na stałe i zablokował magistralę pozostałym.
    if (cs != kNoPin) Hal::gpio().write(cs, true);

    return result;
}

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------

size_t IUart::write(CByteSpan data, u32 timeoutMs) {
    if (data.empty()) return 0;
    rtos::LockGuard guard(mtx_, timeoutMs);
    if (!guard.held()) return 0;

    size_t sent = 0;
    const u32 deadline = rtos::nowMs() + timeoutMs;
    while (sent < data.size()) {
        const size_t n = doWrite(CByteSpan{data.data() + sent, data.size() - sent});
        if (n == 0) {
            if (static_cast<i32>(rtos::nowMs() - deadline) >= 0) break;
            rtos::delayMs(1);
            continue;
        }
        sent += n;
    }
    return sent;
}

size_t IUart::read(ByteSpan out, u32 timeoutMs) {
    if (out.empty()) return 0;
    rtos::LockGuard guard(mtx_, timeoutMs ? timeoutMs : kBusTimeoutMs);
    if (!guard.held()) return 0;

    size_t got = 0;
    const u32 deadline = rtos::nowMs() + timeoutMs;
    for (;;) {
        got += doRead(ByteSpan{out.data() + got, out.size() - got});
        if (got >= out.size()) break;
        if (timeoutMs == 0 || static_cast<i32>(rtos::nowMs() - deadline) >= 0) break;
        rtos::delayMs(1);
    }
    return got;
}

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------

Status IPwm::writeMicroseconds(PinNum pin, u16 us) {
    const u32 freq = frequencyHz(pin);
    if (freq == 0) return fail(Err::NotInitialized);

    // Okres w mikrosekundach; wypełnienie w promilach względem okresu.
    const u32 periodUs = 1000000u / freq;
    if (us > periodUs) return fail(Err::OutOfRange);

    const u16 permille = static_cast<u16>((static_cast<u32>(us) * 1000u) / periodUs);
    return setDutyPermille(pin, permille);
}

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------

Status IAdc::setCalibration(PinNum pin, const AdcCalibration& cal) {
    if (cal.dividerDen == 0 || cal.dividerNum == 0) return fail(Err::BadArgument);

    for (auto& e : cal_) {
        if (e.pin == pin) {
            e.cal = cal;
            return ok();
        }
    }
    for (auto& e : cal_) {
        if (e.pin != kNoPin) continue;
        e.pin = pin;
        e.cal = cal;
        return ok();
    }
    return fail(Err::OutOfMemory);
}

AdcCalibration IAdc::calibration(PinNum pin) const {
    for (const auto& e : cal_) {
        if (e.pin == pin) return e.cal;
    }
    return AdcCalibration{};  // brak wpisu = przelicznik neutralny
}

Result<u32> IAdc::readMv(PinNum pin) {
    auto raw = readPinMv(pin);
    if (!raw) return unexpected(raw.error());

    const AdcCalibration c = calibration(pin);
    // Całość na liczbach całkowitych — RP2040 nie ma FPU (rozdz. 15).
    u64 mv = static_cast<u64>(*raw) * c.dividerNum / c.dividerDen;
    mv     = mv * c.gainPermille / 1000u;

    const i64 adjusted = static_cast<i64>(mv) + c.offsetMv;
    return static_cast<u32>(adjusted < 0 ? 0 : adjusted);
}

// ---------------------------------------------------------------------------
// Pamięć trwała — nakładki typowane
// ---------------------------------------------------------------------------

Status IStorage::setU32(const char* key, u32 v)   { return setBlob(key, CByteSpan{reinterpret_cast<const u8*>(&v), sizeof(v)}); }
Status IStorage::setI32(const char* key, i32 v)   { return setBlob(key, CByteSpan{reinterpret_cast<const u8*>(&v), sizeof(v)}); }
Status IStorage::setFloat(const char* key, float v) { return setBlob(key, CByteSpan{reinterpret_cast<const u8*>(&v), sizeof(v)}); }

Status IStorage::setBool(const char* key, bool v) {
    const u8 byte = v ? 1 : 0;
    return setBlob(key, CByteSpan{&byte, 1});
}

Status IStorage::setString(const char* key, const char* value) {
    if (!value) return fail(Err::BadArgument);
    return setBlob(key, CByteSpan{reinterpret_cast<const u8*>(value), strlen(value) + 1});
}

namespace {
/** Wspólny odczyt wartości o stałym rozmiarze. */
template <typename T>
Result<T> readFixed(IStorage& s, const char* key, T fallback) {
    T value{};
    auto r = s.getBlob(key, ByteSpan{reinterpret_cast<u8*>(&value), sizeof(T)});
    if (!r) return fallback;                 // brak klucza to nie błąd
    if (*r != sizeof(T)) return unexpected(Err::Protocol);  // zapisano inny typ
    return value;
}
}  // namespace

Result<u32>   IStorage::getU32(const char* key, u32 fb)     { return readFixed<u32>(*this, key, fb); }
Result<i32>   IStorage::getI32(const char* key, i32 fb)     { return readFixed<i32>(*this, key, fb); }
Result<float> IStorage::getFloat(const char* key, float fb) { return readFixed<float>(*this, key, fb); }

Result<bool> IStorage::getBool(const char* key, bool fb) {
    u8 byte = 0;
    auto r = getBlob(key, ByteSpan{&byte, 1});
    if (!r) return fb;
    return byte != 0;
}

Result<size_t> IStorage::getString(const char* key, char* out, size_t cap) {
    if (!out || cap == 0) return unexpected(Err::BadArgument);
    auto r = getBlob(key, ByteSpan{reinterpret_cast<u8*>(out), cap});
    if (!r) return unexpected(r.error());

    size_t len = *r;
    if (len == 0) {
        out[0] = '\0';
        return static_cast<size_t>(0);
    }
    // Zapis zawierał terminator; jeśli bufor był ciasny, dokładamy go sami.
    if (out[len - 1] != '\0') {
        if (len >= cap) len = cap - 1;
        out[len] = '\0';
        return len;
    }
    return len - 1;
}

// ---------------------------------------------------------------------------
// Czas kalendarzowy
// ---------------------------------------------------------------------------

Result<DateTime> ITime::utc() const {
    auto e = epochSec();
    if (!e) return unexpected(e.error());
    return toDateTime(*e);
}

DateTime toDateTime(u64 epochSec) {
    // Algorytm civil_from_days: przesunięcie epoki na 1 marca 0000 upraszcza
    // obsługę lat przestępnych do jednego wyrażenia.
    const u64 days = epochSec / 86400u;
    const u32 secs = static_cast<u32>(epochSec % 86400u);

    i64 z = static_cast<i64>(days) + 719468;
    const i64 era = (z >= 0 ? z : z - 146096) / 146097;
    const u64 doe = static_cast<u64>(z - era * 146097);
    const u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const i64 y   = static_cast<i64>(yoe) + era * 400;
    const u64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const u64 mp  = (5 * doy + 2) / 153;
    const u64 d   = doy - (153 * mp + 2) / 5 + 1;
    const u64 m   = mp < 10 ? mp + 3 : mp - 9;

    DateTime dt;
    dt.year   = static_cast<u16>(y + (m <= 2 ? 1 : 0));
    dt.month  = static_cast<u8>(m);
    dt.day    = static_cast<u8>(d);
    dt.hour   = static_cast<u8>(secs / 3600);
    dt.minute = static_cast<u8>((secs % 3600) / 60);
    dt.second = static_cast<u8>(secs % 60);
    return dt;
}

u64 toEpochSec(const DateTime& dt) {
    const i64 y   = static_cast<i64>(dt.year) - (dt.month <= 2 ? 1 : 0);
    const i64 era = (y >= 0 ? y : y - 399) / 400;
    const u64 yoe = static_cast<u64>(y - era * 400);
    const u64 mp  = dt.month > 2 ? dt.month - 3u : dt.month + 9u;
    const u64 doy = (153 * mp + 2) / 5 + dt.day - 1;
    const u64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const i64 days = era * 146097 + static_cast<i64>(doe) - 719468;

    return static_cast<u64>(days) * 86400u + dt.hour * 3600u + dt.minute * 60u + dt.second;
}

// ---------------------------------------------------------------------------
// Nakładki na pojedyncze piny
// ---------------------------------------------------------------------------

Status OutputPin::begin(bool initialHigh) {
    if (pin_ == kNoPin) return fail(Err::BadArgument);
    HYDRA_CHECK(Hal::gpio().configure(pin_, PinMode::Output));
    return Hal::gpio().write(pin_, initialHigh);
}

Status OutputPin::set(bool high) {
    if (pin_ == kNoPin) return fail(Err::BadArgument);
    return Hal::gpio().write(pin_, high);
}

Status OutputPin::toggle() {
    if (pin_ == kNoPin) return fail(Err::BadArgument);
    return Hal::gpio().toggle(pin_);
}

Status InputPin::begin(PinMode mode) {
    if (pin_ == kNoPin) return fail(Err::BadArgument);
    return Hal::gpio().configure(pin_, mode);
}

Result<bool> InputPin::read() {
    if (pin_ == kNoPin) return unexpected(Err::BadArgument);
    return Hal::gpio().read(pin_);
}

}  // namespace hal
}  // namespace hydra
