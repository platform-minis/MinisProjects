#pragma once
/**
 * Hydra — magistrale I2C / SPI / UART (rozdz. 5).
 *
 * Kluczowa decyzja architektoniczna: magistrale są zawsze obiektami z mutexem.
 * Nie istnieje publiczne API pozwalające dotknąć magistrali bez blokady —
 * jedyną drogą jest transaction(), które przyjmuje ciało operacji i wykonuje
 * je pod blokadą, przekazując uchwyt sesji. Sesja jest ważna wyłącznie wewnątrz
 * tego wywołania.
 *
 * To eliminuje całą klasę błędów wynikającą z faktu, że stm32duino i biblioteki
 * Arduino nie są thread-safe: nie da się o blokadzie zapomnieć, bo bez sesji
 * nie ma jak wykonać transferu.
 *
 *     bus.transaction([&](II2cBus::Session& s) -> Status {
 *         u8 who = 0;
 *         HYDRA_CHECK(s.readReg(0x68, 0x75, {&who, 1}));
 *         return who == 0x71 ? ok() : fail(Err::NotFound);
 *     });
 */

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/hal/Pin.hpp"

namespace hydra {
namespace hal {

/** Domyślny limit oczekiwania na zwolnienie magistrali. */
constexpr u32 kBusTimeoutMs = 1000;

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------

class II2cBus {
public:
    /** Uchwyt transferów. Ważny wyłącznie w obrębie transaction(). */
    class Session : NonCopyable {
    public:
        Status write(u8 addr, CByteSpan data);
        Status read(u8 addr, ByteSpan out);
        Status writeRead(u8 addr, CByteSpan tx, ByteSpan rx);

        /** Zapis do rejestru: [reg][dane...] w jednym transferze. */
        Status writeReg(u8 addr, u8 reg, CByteSpan data);
        Status writeReg8(u8 addr, u8 reg, u8 value) { return writeReg(addr, reg, {&value, 1}); }
        /** Odczyt rejestru: zapis adresu rejestru, powtórzony start, odczyt. */
        Status readReg(u8 addr, u8 reg, ByteSpan out);
        Result<u8> readReg8(u8 addr, u8 reg);

        /** Sprawdzenie obecności układu pod adresem (transfer zerowej długości). */
        Status ping(u8 addr);

    private:
        friend class II2cBus;
        explicit Session(II2cBus& bus) : bus_(bus) {}
        II2cBus& bus_;
    };

    using Body = Delegate<Status(Session&)>;

    virtual ~II2cBus() = default;

    /**
     * Wykonuje ciało pod blokadą magistrali. Zwraca wynik ciała albo
     * Err::Timeout, gdy magistrala nie zwolniła się w zadanym czasie.
     */
    Status transaction(Body body, u32 timeoutMs = kBusTimeoutMs);

    /**
     * Skan magistrali — wypełnia tablicę adresami odpowiadających układów.
     * Cały skan odbywa się pod jedną blokadą. Zwraca liczbę znalezionych.
     */
    Result<u8> scan(u8* found, u8 capacity, u32 timeoutMs = kBusTimeoutMs);

    /** Częstotliwość zegara magistrali w Hz (0 = nieznana). */
    virtual u32 clockHz() const { return 0; }

protected:
    /** Backend implementuje wyłącznie te trzy operacje. Wołane pod blokadą. */
    virtual Status doWrite(u8 addr, CByteSpan data)                   = 0;
    virtual Status doRead(u8 addr, ByteSpan out)                      = 0;
    virtual Status doWriteRead(u8 addr, CByteSpan tx, ByteSpan rx)    = 0;

    rtos::Mutex mtx_;
};

// ---------------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------------

struct SpiConfig {
    u32  clockHz   = 1000000;
    u8   mode      = 0;      ///< 0..3 (CPOL/CPHA)
    bool msbFirst  = true;
};

class ISpiBus {
public:
    class Session : NonCopyable {
    public:
        /** Pełny transfer dupleksowy. rx może być puste (sam zapis). */
        Status transfer(CByteSpan tx, ByteSpan rx);
        Status write(CByteSpan tx) { return transfer(tx, ByteSpan{}); }
        Status read(ByteSpan rx)   { return transfer(CByteSpan{}, rx); }

    private:
        friend class ISpiBus;
        explicit Session(ISpiBus& bus) : bus_(bus) {}
        ISpiBus& bus_;
    };

    using Body = Delegate<Status(Session&)>;

    virtual ~ISpiBus() = default;

    /**
     * Wykonuje ciało pod blokadą, z ustawioną konfiguracją zegara i opuszczonym
     * sygnałem CS. CS wraca w stan wysoki także wtedy, gdy ciało zwróci błąd —
     * zapomniana deselekcja zawiesiłaby magistralę dla pozostałych układów.
     * cs = kNoPin oznacza, że wyborem układu zarządza wołający.
     */
    Status transaction(PinNum cs, const SpiConfig& cfg, Body body,
                       u32 timeoutMs = kBusTimeoutMs);

protected:
    virtual Status doConfigure(const SpiConfig& cfg)            = 0;
    virtual Status doTransfer(CByteSpan tx, ByteSpan rx)        = 0;

    rtos::Mutex mtx_;
};

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------

struct UartConfig {
    u32    baud     = 115200;
    u8     dataBits = 8;
    char   parity   = 'N';  ///< 'N', 'E', 'O'
    u8     stopBits = 1;
    PinNum rx       = kNoPin;
    PinNum tx       = kNoPin;
};

/**
 * Strumień znakowy i ramki binarne. Zapis jest serializowany wewnętrznym
 * mutexem, żeby log i shell diagnostyczny nie przeplatały się w połowie linii.
 */
class IUart {
public:
    virtual ~IUart() = default;

    virtual Status begin(const UartConfig& cfg) = 0;
    virtual void   end()                        = 0;

    /** Zapis całości danych. Zwraca liczbę zapisanych bajtów. */
    size_t write(CByteSpan data, u32 timeoutMs = kBusTimeoutMs);
    /** Odczyt do zapełnienia bufora albo do upływu timeoutu. */
    size_t read(ByteSpan out, u32 timeoutMs = 0);

    virtual size_t available()  = 0;
    virtual void   flush()      = 0;

protected:
    virtual size_t doWrite(CByteSpan data) = 0;
    virtual size_t doRead(ByteSpan out)    = 0;

    rtos::Mutex mtx_;
};

}  // namespace hal
}  // namespace hydra
