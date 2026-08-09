#pragma once
/**
 * Hydra — atrapowy backend HAL dla buildu hostowego.
 *
 * Pozwala testować kod, który sięga po sprzęt, bez sprzętu: atrapa I2C
 * odpowiada z konfigurowalnej mapy rejestrów, więc sterownik czujnika
 * (BME280, INA219, …) można w całości przetestować na PC. To dzięki temu
 * moduły sense/net/motion będą testowalne w kolejnych etapach.
 *
 * Nagłówek istnieje wyłącznie w buildzie hostowym.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_PLAT_HOST

#include "hydra/hal/Hal.hpp"

namespace hydra {
namespace hal {
namespace mock {

// ---------------------------------------------------------------------------

class MockGpio : public IGpio {
public:
    struct PinState {
        PinMode    mode       = PinMode::Input;
        bool       configured = false;
        bool       level      = false;
        Edge       edge       = Edge::None;
        IsrHandler isr        = nullptr;
        void*      isrArg     = nullptr;
        u32        writes     = 0;
    };

    static constexpr PinNum kMaxPins = 48;

    Status configure(PinNum pin, PinMode mode) override;
    Status write(PinNum pin, bool high) override;
    Result<bool> read(PinNum pin) override;
    Status attachInterrupt(PinNum pin, Edge edge, IsrHandler handler, void* arg) override;
    Status detachInterrupt(PinNum pin) override;

    // --- sterowanie atrapą z poziomu testu ---
    /** Wymusza poziom na wejściu, tak jakby zmienił go świat zewnętrzny. */
    void setInputLevel(PinNum pin, bool high);
    /** Wywołuje zarejestrowany handler, jakby przyszło przerwanie od zbocza. */
    bool triggerInterrupt(PinNum pin);
    const PinState& state(PinNum pin) const;
    void clear();

private:
    bool valid(PinNum pin) const { return pin >= 0 && pin < kMaxPins; }
    PinState pins_[kMaxPins];
};

// ---------------------------------------------------------------------------

/**
 * Atrapa magistrali I2C z mapą rejestrów. Odczyt rejestru zwraca wartość
 * ustawioną przez test; zapis ją nadpisuje — dokładnie tak, jak zachowuje się
 * większość układów peryferyjnych.
 */
class MockI2c : public II2cBus {
public:
    static constexpr u8  kMaxDevices = 8;
    /** Pełna 8-bitowa przestrzeń rejestrów — realne układy używają adresów
     *  w rodzaju 0x75 (WHO_AM_I) czy 0xF7 (BME280), nie tylko niskich. */
    static constexpr u16 kRegCount   = 256;

    struct Device {
        u8   addr    = 0;
        bool present = false;
        u8   regs[kRegCount] = {};
        u8   regPtr  = 0;   ///< wskaźnik rejestru ustawiony ostatnim zapisem
        /**
         * Szerokość rejestru w bajtach. Układy pokroju INA219 adresują rejestry
         * 16-bitowe: adres 0x02 to jedno słowo, a nie dwa sąsiednie bajty.
         * Przy szerokości 2 magazyn indeksowany jest jako reg*2.
         */
        u8   regWidth = 1;
        u32  reads   = 0;
        u32  writes  = 0;
    };

    /** Dodaje układ pod wskazanym adresem. */
    Status addDevice(u8 addr);
    void   removeDevice(u8 addr);
    Device* device(u8 addr);

    void setReg(u8 addr, u8 reg, u8 value);
    Result<u8> getReg(u8 addr, u8 reg);

    /** Przełącza układ w tryb rejestrów 16-bitowych (big-endian). */
    void setWordRegisters(u8 addr, bool on = true);
    void setReg16(u8 addr, u8 reg, u16 value);
    Result<u16> getReg16(u8 addr, u8 reg);

    /** Wymusza błąd na kolejnych n transferach — do testów obsługi awarii. */
    void failNext(u32 count, Err error = Err::IoError);
    void clear();

    u32 clockHz() const override { return 400000; }

protected:
    Status doWrite(u8 addr, CByteSpan data) override;
    Status doRead(u8 addr, ByteSpan out) override;
    Status doWriteRead(u8 addr, CByteSpan tx, ByteSpan rx) override;

private:
    Device* find(u8 addr);
    Status  takeFailure();

    Device devices_[kMaxDevices];
    u32    failCount_ = 0;
    Err    failErr_   = Err::IoError;
};

// ---------------------------------------------------------------------------

/** Atrapa SPI: zapamiętuje wysłane bajty i oddaje wcześniej wstawioną odpowiedź. */
class MockSpi : public ISpiBus {
public:
    static constexpr size_t kBufSize = 128;

    /** Dane, które magistrala zwróci przy kolejnych odczytach. */
    void queueResponse(CByteSpan data);
    CByteSpan captured() const { return CByteSpan{txBuf_, txLen_}; }
    const SpiConfig& lastConfig() const { return cfg_; }
    void clear();

protected:
    Status doConfigure(const SpiConfig& cfg) override;
    Status doTransfer(CByteSpan tx, ByteSpan rx) override;

private:
    SpiConfig cfg_{};
    u8     txBuf_[kBufSize] = {};
    size_t txLen_           = 0;
    u8     rxBuf_[kBufSize] = {};
    size_t rxLen_           = 0;
    size_t rxPos_           = 0;
};

// ---------------------------------------------------------------------------

/** Atrapa UART: bufor wyjściowy do inspekcji i bufor wejściowy do wstrzykiwania. */
class MockUart : public IUart {
public:
    static constexpr size_t kBufSize = 512;

    Status begin(const UartConfig& cfg) override;
    void   end() override;
    size_t available() override;
    void   flush() override;

    /** Wstawia dane tak, jakby przyszły z drugiej strony łącza. */
    void inject(CByteSpan data);
    /** Wszystko, co urządzenie wysłało od ostatniego clear(). */
    CByteSpan sent() const { return CByteSpan{txBuf_, txLen_}; }
    const UartConfig& config() const { return cfg_; }
    void clear();

protected:
    size_t doWrite(CByteSpan data) override;
    size_t doRead(ByteSpan out) override;

private:
    UartConfig cfg_{};
    bool   open_            = false;
    u8     txBuf_[kBufSize] = {};
    size_t txLen_           = 0;
    u8     rxBuf_[kBufSize] = {};
    size_t rxLen_           = 0;
    size_t rxPos_           = 0;
};

// ---------------------------------------------------------------------------

class MockPwm : public IPwm {
public:
    struct Channel {
        PinNum pin        = kNoPin;
        u32    freqHz     = 0;
        u16    permille   = 0;
        u8     resolution = 0;
        bool   active     = false;
    };
    static constexpr u8 kMaxChannels = 8;

    Status configure(PinNum pin, u32 freqHz, u8 resolutionBits) override;
    Status setDutyPermille(PinNum pin, u16 permille) override;
    Status release(PinNum pin) override;
    u32    frequencyHz(PinNum pin) const override;

    const Channel& channel(PinNum pin) const;
    void clear();

private:
    Channel* find(PinNum pin);
    const Channel* find(PinNum pin) const;
    Channel channels_[kMaxChannels];
    Channel none_{};
};

// ---------------------------------------------------------------------------

class MockAdc : public IAdc {
public:
    static constexpr u8 kMaxPins = 8;

    Status configure(PinNum pin, const AdcConfig& cfg) override;
    Result<u16> readRaw(PinNum pin) override;
    Result<u16> readPinMv(PinNum pin) override;
    u8 resolutionBits() const override { return 12; }

    /** Ustawia napięcie, jakie atrapa zwróci na wskazanym pinie. */
    void setPinMv(PinNum pin, u16 mv);
    void clear();

private:
    struct Entry {
        PinNum pin = kNoPin;
        u16    mv  = 0;
    };
    Entry entries_[kMaxPins];
};

// ---------------------------------------------------------------------------

/** Pamięć trwała w RAM — znika po restarcie procesu, jak nieudany zapis NVS. */
class MockStorage : public IStorage {
public:
    static constexpr u8     kMaxEntries = 16;
    static constexpr size_t kValueMax   = 64;

    Status begin(const char* nameSpace, bool readOnly) override;
    void   end() override;
    Status setBlob(const char* key, CByteSpan data) override;
    Result<size_t> getBlob(const char* key, ByteSpan out) override;
    Status erase(const char* key) override;
    Status eraseAll() override;
    bool   has(const char* key) override;

    u32 commits() const { return commits_; }
    Status commit() override;
    void clear();

private:
    struct Entry {
        char   key[kStorageKeyMax + 1] = {};
        u8     value[kValueMax]        = {};
        size_t size                    = 0;
        bool   used                    = false;
    };
    Entry* find(const char* key);

    Entry entries_[kMaxEntries];
    char  ns_[kStorageKeyMax + 1] = {};
    bool  open_     = false;
    bool  readOnly_ = false;
    u32   commits_  = 0;
};

// ---------------------------------------------------------------------------

class MockTime : public ITime {
public:
    bool synchronized() const override { return synced_; }
    Result<u64> epochSec() const override;
    Status setEpochSec(u64 epoch) override;
    void clear();

private:
    u64  epoch_  = 0;
    u32  setAtMs_ = 0;
    bool synced_ = false;
};

// ---------------------------------------------------------------------------

/**
 * Atrapa kamery generująca wzorzec testowy.
 *
 * Oddaje pionowe pasy o rosnącej jasności, przesuwane o jeden piksel na klatkę.
 * Ruch jest tu istotny: obraz nieruchomy przechodzi przez skalowanie i konwersję
 * tak samo, jak przechodziłby zamrożony bufor, więc nie odróżniłby działającego
 * potoku od zaciętego.
 *
 * Pilnuje też umowy o własności: `capture()` bez wcześniejszego `release()`
 * kończy się `Busy` po wyczerpaniu buforów — tak samo jak na sprzęcie,
 * gdzie zatrzymana klatka zatrzymuje strumień.
 */
class MockCamera : public ICamera {
public:
    static constexpr u8     kMaxBuffers = 3;
    static constexpr size_t kMaxFrameBytes = 320 * 240 * 2;

    Status begin(const CameraConfig& cfg) override;
    void   end() override;
    bool   running() const override { return running_; }

    Result<CameraFrame> capture() override;
    void   release(CameraFrame& frame) override;
    CameraConfig config() const override { return cfg_; }
    u32    dropped() const override { return dropped_; }

    /** Ile klatek wydano od `clear()`. */
    u32  produced() const { return produced_; }
    /** Ile klatek jest w tej chwili pożyczonych. */
    u8   borrowed() const;
    void clear();

private:
    void paint(u8* buffer, u16 width, u16 height, u8 phase);

    struct Slot {
        u8   data[kMaxFrameBytes] = {};
        bool lent = false;
    };

    CameraConfig cfg_{};
    bool  running_ = false;
    u32   produced_ = 0;
    u32   dropped_ = 0;
    u8    phase_ = 0;
    Slot  slots_[kMaxBuffers];
};

// ---------------------------------------------------------------------------

/** Atrapa DAC: zapamiętuje ostatnią wartość i liczy zapisy. */
class MockDac : public IDac {
public:
    static constexpr u8 kChannels = 2;

    Status enable(u8 channel) override;
    void   disable(u8 channel) override;
    Status write(u8 channel, u16 value) override;
    u8     resolutionBits() const override { return 8; }
    u8     channelCount() const override { return kChannels; }

    u16  value(u8 channel) const { return channel < kChannels ? value_[channel] : 0; }
    u32  writes(u8 channel) const { return channel < kChannels ? writes_[channel] : 0; }
    bool enabled(u8 channel) const { return channel < kChannels && on_[channel]; }
    void clear();

private:
    u16  value_[kChannels] = {};
    u32  writes_[kChannels] = {};
    bool on_[kChannels] = {};
};

// ---------------------------------------------------------------------------

/**
 * Atrapa I2S z pętlą zwrotną.
 *
 * Bufory nadane przez `Tx` trafiają do dziennika i **jednocześnie** do
 * strumienia, z którego czyta `Rx`. Dzięki temu cały łańcuch — źródło,
 * filtry, ujście, z powrotem na wejście — da się przepuścić na hoście
 * i porównać próbki co do bitu. Bez tego elementy I2S sprawdzałoby się
 * dopiero na płytce, czyli za późno.
 *
 * Bufory oddaje natychmiast, tak jak backend kopiujący (ESP-IDF). Opóźnienie
 * zwrotu da się wymusić przez `setLatency()` — po to, żeby test mógł sprawdzić
 * zachowanie elementu, który czeka na bufor.
 */
class MockI2s : public II2s {
public:
    static constexpr u8   kMaxQueued  = 8;
    static constexpr size_t kLogBytes = 8192;

    Status begin(const I2sConfig& cfg) override;
    void   end() override;
    bool   running() const override { return running_; }

    Status submit(ByteSpan buffer) override;
    bool   reclaim(ByteSpan& buffer, u32& bytes) override;
    u8     queueDepth() const override { return kMaxQueued; }
    u32    xruns() const override { return xruns_; }
    u32    actualSampleRate() const override { return cfg_.sampleRate; }

    // --- sterowanie z testu --------------------------------------------------

    /** Ile wywołań `reclaim()` musi minąć, zanim bufor wróci. 0 = natychmiast. */
    void setLatency(u8 calls) { latency_ = calls; }

    /** Dane, które dostanie strona odbiorcza. Zastępuje pętlę zwrotną. */
    void feed(CByteSpan data);

    /** Wszystko, co zostało nadane od `clear()`. */
    CByteSpan captured() const { return CByteSpan{log_, logLen_}; }

    const I2sConfig& config() const { return cfg_; }
    void clear();

private:
    struct Slot {
        ByteSpan buffer{};
        u32      bytes = 0;
        u8       age   = 0;
        bool     used  = false;
    };

    I2sConfig cfg_{};
    bool      running_ = false;
    u8        latency_ = 0;
    u32       xruns_ = 0;

    Slot slots_[kMaxQueued];

    /** Dziennik nadanych bajtów. */
    u8     log_[kLogBytes] = {};
    size_t logLen_ = 0;

    /** Strumień dla strony odbiorczej — pętla zwrotna albo `feed()`. */
    u8     rx_[kLogBytes] = {};
    size_t rxLen_ = 0;
    size_t rxRead_ = 0;
};

// ---------------------------------------------------------------------------

/** Komplet atrap. Test sięga po nie, żeby wstrzyknąć stan i sprawdzić efekty. */
struct Backend {
    MockGpio    gpio;
    MockI2c     i2c;
    MockSpi     spi;
    MockUart    uart;
    MockPwm     pwm;
    MockAdc     adc;
    MockI2s     i2s;
    MockDac     dac;
    MockCamera  camera;
    MockStorage storage;
    MockTime    time;

    /** Przywraca wszystkie atrapy do stanu początkowego. */
    void clear();
};

/** Jedyna instancja atrap. */
Backend& backend();

/** Rejestruje atrapy w Hal. Na hoście robi to installDefaultBackend(). */
Status install(ResetReason reason = ResetReason::PowerOn);

}  // namespace mock
}  // namespace hal
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST
