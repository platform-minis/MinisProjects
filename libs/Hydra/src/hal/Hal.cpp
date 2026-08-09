/**
 * Hydra — rejestr sterowników i obiekty puste.
 *
 * Obiekt pusty zamiast nullptra to świadoma decyzja: kod aplikacji i modułów
 * nie musi sprawdzać, czy dana peryferia w ogóle istnieje na tej płytce.
 * Operacja na nieobecnym sterowniku kończy się Err::NotSupported — czytelnym
 * błędem w normalnej ścieżce obsługi, a nie wyjątkiem twardym.
 */

#include "hydra/hal/Hal.hpp"

namespace hydra {
namespace hal {
namespace {

class NullGpio : public IGpio {
public:
    Status configure(PinNum, PinMode) override { return fail(Err::NotSupported); }
    Status write(PinNum, bool) override        { return fail(Err::NotSupported); }
    Result<bool> read(PinNum) override         { return unexpected(Err::NotSupported); }
    Status attachInterrupt(PinNum, Edge, IsrHandler, void*) override {
        return fail(Err::NotSupported);
    }
    Status detachInterrupt(PinNum) override { return fail(Err::NotSupported); }
};

class NullI2c : public II2cBus {
protected:
    Status doWrite(u8, CByteSpan) override            { return fail(Err::NotSupported); }
    Status doRead(u8, ByteSpan) override              { return fail(Err::NotSupported); }
    Status doWriteRead(u8, CByteSpan, ByteSpan) override { return fail(Err::NotSupported); }
};

class NullSpi : public ISpiBus {
protected:
    Status doConfigure(const SpiConfig&) override      { return fail(Err::NotSupported); }
    Status doTransfer(CByteSpan, ByteSpan) override    { return fail(Err::NotSupported); }
};

/**
 * Zaślepka I2S.
 *
 * `begin()` odmawia zamiast udawać, że działa. Milczący strumień audio jest
 * nierozróżnialny od ciszy w materiale, więc brak sterownika musi być widoczny
 * przy starcie, a nie po godzinie szukania, dlaczego „nic nie słychać".
 */
class NullI2s : public II2s {
public:
    Status begin(const I2sConfig&) override { return fail(Err::NotSupported); }
    void   end() override {}
    bool   running() const override { return false; }
    Status submit(ByteSpan) override { return fail(Err::NotSupported); }
    bool   reclaim(ByteSpan&, u32&) override { return false; }
    u8     queueDepth() const override { return 0; }
    u32    xruns() const override { return 0; }
    u32    actualSampleRate() const override { return 0; }
};

/**
 * Zaślepka kamery.
 *
 * `begin()` odmawia, a nie udaje. Kamera, która „działa" i nie oddaje klatek,
 * jest nierozróżnialna od kamery zasłoniętej — a to dwie zupełnie różne
 * rzeczy do naprawienia.
 */
class NullCamera : public ICamera {
public:
    Status begin(const CameraConfig&) override { return fail(Err::NotSupported); }
    void   end() override {}
    bool   running() const override { return false; }
    Result<CameraFrame> capture() override { return unexpected(Err::NotSupported); }
    void   release(CameraFrame&) override {}
    CameraConfig config() const override { return {}; }
    u32    dropped() const override { return 0; }
};

/** Zaślepka DAC — większość układów Hydry go nie ma i ma to być widoczne. */
class NullDac : public IDac {
public:
    Status enable(u8) override { return fail(Err::NotSupported); }
    void   disable(u8) override {}
    Status write(u8, u16) override { return fail(Err::NotSupported); }
    u8     resolutionBits() const override { return 0; }
    u8     channelCount() const override { return 0; }
};

/**
 * Zaślepka systemu plików.
 *
 * `mount()` odmawia. Aplikacja, która zapisuje do zaślepki udającej sukces,
 * dowiaduje się o braku nośnika dopiero wtedy, gdy sięgnie po dane — czyli
 * po wyłączeniu zasilania.
 */
class NullFs : public IFileSystem {
public:
    Status mount() override { return fail(Err::NotSupported); }
    void   unmount() override {}
    bool   mounted() const override { return false; }
    Status format() override { return fail(Err::NotSupported); }
    Result<IFile*>      open(const char*, OpenMode) override { return unexpected(Err::NotSupported); }
    Result<IDirectory*> openDir(const char*) override { return unexpected(Err::NotSupported); }
    bool   exists(const char*) override { return false; }
    Status remove(const char*) override { return fail(Err::NotSupported); }
    Status rename(const char*, const char*) override { return fail(Err::NotSupported); }
    Status mkdir(const char*) override { return fail(Err::NotSupported); }
    Result<size_t> fileSize(const char*) override { return unexpected(Err::NotSupported); }
    Result<u64> totalBytes() const override { return unexpected(Err::NotSupported); }
    Result<u64> usedBytes() const override { return unexpected(Err::NotSupported); }
};

class NullUart : public IUart {
public:
    Status begin(const UartConfig&) override { return fail(Err::NotSupported); }
    void   end() override {}
    size_t available() override { return 0; }
    void   flush() override {}

protected:
    size_t doWrite(CByteSpan) override { return 0; }
    size_t doRead(ByteSpan) override   { return 0; }
};

class NullPwm : public IPwm {
public:
    Status configure(PinNum, u32, u8) override        { return fail(Err::NotSupported); }
    Status setDutyPermille(PinNum, u16) override      { return fail(Err::NotSupported); }
    Status release(PinNum) override                   { return fail(Err::NotSupported); }
    u32    frequencyHz(PinNum) const override         { return 0; }
};

class NullAdc : public IAdc {
public:
    Status configure(PinNum, const AdcConfig&) override { return fail(Err::NotSupported); }
    Result<u16> readRaw(PinNum) override               { return unexpected(Err::NotSupported); }
    Result<u16> readPinMv(PinNum) override             { return unexpected(Err::NotSupported); }
    u8 resolutionBits() const override                 { return 0; }
};

class NullStorage : public IStorage {
public:
    Status begin(const char*, bool) override            { return fail(Err::NotSupported); }
    void   end() override {}
    Status setBlob(const char*, CByteSpan) override     { return fail(Err::NotSupported); }
    Result<size_t> getBlob(const char*, ByteSpan) override { return unexpected(Err::NotSupported); }
    Status erase(const char*) override                  { return fail(Err::NotSupported); }
    Status eraseAll() override                          { return fail(Err::NotSupported); }
    bool   has(const char*) override                    { return false; }
};

class NullTime : public ITime {
public:
    bool synchronized() const override      { return false; }
    Result<u64> epochSec() const override   { return unexpected(Err::NotInitialized); }
    Status setEpochSec(u64) override        { return fail(Err::NotSupported); }
};

struct Registry {
    Drivers drivers;
    bool    installed  = false;
    bool    installing = false;

    NullGpio    gpio;
    NullI2c     i2c;
    NullSpi     spi;
    NullUart    uart;
    NullI2s     i2s;
    NullPwm     pwm;
    NullDac     dac;
    NullCamera  camera;
    NullAdc     adc;
    NullStorage storage;
    NullFs      fs;
    NullTime    time;
};

Registry& reg() {
    static Registry r;
    return r;
}

/**
 * Leniwa instalacja backendu przy pierwszym użyciu. Flaga installing_ chroni
 * przed nieskończoną rekurencją, gdy backend sam sięgnie po Hal w trakcie
 * własnej inicjalizacji.
 */
Registry& ensure() {
    Registry& r = reg();
    if (!r.installed && !r.installing) {
        r.installing = true;
        installDefaultBackend();
        r.installing = false;
    }
    return r;
}

}  // namespace

Status Hal::install(const Drivers& drivers) {
    Registry& r = reg();
    r.drivers   = drivers;
    r.installed = true;
    return ok();
}

bool        Hal::ready()       { return reg().installed; }
const char* Hal::backendName() { return ensure().drivers.name; }
ResetReason Hal::resetReason() { return ensure().drivers.resetReason; }

IGpio& Hal::gpio() {
    Registry& r = ensure();
    return r.drivers.gpio ? *r.drivers.gpio : static_cast<IGpio&>(r.gpio);
}

II2cBus& Hal::i2c(u8 index) {
    Registry& r = ensure();
    if (index >= 2 || !r.drivers.i2c[index]) return r.i2c;
    return *r.drivers.i2c[index];
}

ISpiBus& Hal::spi(u8 index) {
    Registry& r = ensure();
    if (index >= 2 || !r.drivers.spi[index]) return r.spi;
    return *r.drivers.spi[index];
}

IUart& Hal::uart(u8 index) {
    Registry& r = ensure();
    if (index >= 3 || !r.drivers.uart[index]) return r.uart;
    return *r.drivers.uart[index];
}

II2s&     Hal::i2s()     { Registry& r = ensure(); return r.drivers.i2s ? *r.drivers.i2s : static_cast<II2s&>(r.i2s); }
IPwm&     Hal::pwm()     { Registry& r = ensure(); return r.drivers.pwm ? *r.drivers.pwm : static_cast<IPwm&>(r.pwm); }
ICamera&  Hal::camera()  { Registry& r = ensure(); return r.drivers.camera ? *r.drivers.camera : static_cast<ICamera&>(r.camera); }
IDac&     Hal::dac()     { Registry& r = ensure(); return r.drivers.dac ? *r.drivers.dac : static_cast<IDac&>(r.dac); }
IAdc&     Hal::adc()     { Registry& r = ensure(); return r.drivers.adc ? *r.drivers.adc : static_cast<IAdc&>(r.adc); }
IFileSystem& Hal::fileSystem() { Registry& r = ensure(); return r.drivers.fs ? *r.drivers.fs : static_cast<IFileSystem&>(r.fs); }
IStorage& Hal::storage() { Registry& r = ensure(); return r.drivers.storage ? *r.drivers.storage : static_cast<IStorage&>(r.storage); }
ITime&    Hal::time()    { Registry& r = ensure(); return r.drivers.time ? *r.drivers.time : static_cast<ITime&>(r.time); }

bool Hal::hasGpio()        { return ensure().drivers.gpio != nullptr; }
bool Hal::hasI2c(u8 i)     { return i < 2 && ensure().drivers.i2c[i] != nullptr; }
bool Hal::hasSpi(u8 i)     { return i < 2 && ensure().drivers.spi[i] != nullptr; }
bool Hal::hasUart(u8 i)    { return i < 3 && ensure().drivers.uart[i] != nullptr; }
bool Hal::hasI2s()         { return ensure().drivers.i2s != nullptr; }
bool Hal::hasPwm()         { return ensure().drivers.pwm != nullptr; }
bool Hal::hasCamera()      { return ensure().drivers.camera != nullptr; }
bool Hal::hasDac()         { return ensure().drivers.dac != nullptr; }
bool Hal::hasAdc()         { return ensure().drivers.adc != nullptr; }
bool Hal::hasFileSystem()  { return ensure().drivers.fs != nullptr; }
bool Hal::hasStorage()     { return ensure().drivers.storage != nullptr; }
bool Hal::hasTime()        { return ensure().drivers.time != nullptr; }

void Hal::reset() {
    Registry& r = reg();
    r.drivers   = Drivers{};
    r.installed = false;
    r.installing = false;
}

}  // namespace hal
}  // namespace hydra
