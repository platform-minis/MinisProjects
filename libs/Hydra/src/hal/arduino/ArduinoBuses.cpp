/**
 * Hydra — backend Arduino: I2C, SPI, UART.
 *
 * Metody doXxx() są wołane wyłącznie spod blokady magistrali (patrz IBus.hpp),
 * więc same nie zakładają żadnych zabezpieczeń. To jedyne miejsce, w którym
 * wołane są nie-thread-safe biblioteki Arduino — i właśnie dlatego blokada
 * jest w kontrakcie interfejsu, a nie w gestii wołającego.
 */

#include "ArduinoBackend.hpp"

#if !HYDRA_PLAT_HOST

#include <SPI.h>
#include <Wire.h>

namespace hydra {
namespace hal {
namespace arduino {

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------

Status ArduinoI2c::begin(u8 wireIndex, PinNum sda, PinNum scl, u32 clockHz) {
    if (wireIndex == 0) {
        wire_ = &Wire;
#if defined(WIRE_INTERFACES_COUNT) && (WIRE_INTERFACES_COUNT > 1)
    } else if (wireIndex == 1) {
        wire_ = &Wire1;
#endif
    } else {
        return fail(Err::NotSupported);
    }

#if HYDRA_PLAT_ESP32
    if (sda != kNoPin && scl != kNoPin) {
        if (!wire_->begin(static_cast<int>(sda), static_cast<int>(scl), clockHz)) {
            return fail(Err::IoError);
        }
    } else {
        wire_->begin();
        wire_->setClock(clockHz);
    }
#elif HYDRA_PLAT_RP2
    if (sda != kNoPin) wire_->setSDA(static_cast<u8>(sda));
    if (scl != kNoPin) wire_->setSCL(static_cast<u8>(scl));
    wire_->begin();
    wire_->setClock(clockHz);
#else
    if (sda != kNoPin && scl != kNoPin) {
        wire_->setSDA(static_cast<u32>(sda));
        wire_->setSCL(static_cast<u32>(scl));
    }
    wire_->begin();
    wire_->setClock(clockHz);
#endif

    clockHz_ = clockHz;
    return ok();
}

Status ArduinoI2c::doWrite(u8 addr, CByteSpan data) {
    if (!wire_) return fail(Err::NotInitialized);

    wire_->beginTransmission(addr);
    if (!data.empty()) wire_->write(data.data(), data.size());

    // Kody zwracane przez endTransmission są wspólne dla wszystkich corów:
    // 0 = OK, 2 = NACK na adresie, 3 = NACK na danych, 4 = inny błąd.
    switch (wire_->endTransmission(true)) {
        case 0: return ok();
        case 1: return fail(Err::OutOfRange);   // dane nie zmieściły się w buforze
        case 2:
        case 3: return fail(Err::NotFound);     // układ nie odpowiedział
        case 5: return fail(Err::Timeout);
        default: return fail(Err::IoError);
    }
}

Status ArduinoI2c::doRead(u8 addr, ByteSpan out) {
    if (!wire_) return fail(Err::NotInitialized);
    if (out.empty()) return ok();

    const size_t got = wire_->requestFrom(addr, static_cast<u8>(out.size()));
    if (got != out.size()) return fail(Err::IoError);

    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<u8>(wire_->read());
    return ok();
}

Status ArduinoI2c::doWriteRead(u8 addr, CByteSpan tx, ByteSpan rx) {
    if (!wire_) return fail(Err::NotInitialized);

    // Zapis bez STOP-u, potem powtórzony START — inaczej większość układów
    // zgubi ustawiony adres rejestru.
    wire_->beginTransmission(addr);
    if (!tx.empty()) wire_->write(tx.data(), tx.size());
    if (wire_->endTransmission(false) != 0) return fail(Err::NotFound);

    return doRead(addr, rx);
}

// ---------------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------------

Status ArduinoSpi::begin(PinNum sck, PinNum miso, PinNum mosi) {
#if HYDRA_PLAT_ESP32
    SPI.begin(static_cast<int>(sck), static_cast<int>(miso), static_cast<int>(mosi), -1);
#elif HYDRA_PLAT_RP2
    if (sck != kNoPin)  SPI.setSCK(static_cast<u8>(sck));
    if (miso != kNoPin) SPI.setRX(static_cast<u8>(miso));
    if (mosi != kNoPin) SPI.setTX(static_cast<u8>(mosi));
    SPI.begin();
#else
    HYDRA_UNUSED(sck);
    HYDRA_UNUSED(miso);
    HYDRA_UNUSED(mosi);
    SPI.begin();
#endif
    started_ = true;
    return ok();
}

Status ArduinoSpi::doConfigure(const SpiConfig& cfg) {
    if (!started_) return fail(Err::NotInitialized);
    if (cfg.mode > 3) return fail(Err::BadArgument);
    cfg_ = cfg;
    return ok();
}

Status ArduinoSpi::doTransfer(CByteSpan tx, ByteSpan rx) {
    if (!started_) return fail(Err::NotInitialized);

    const SPISettings settings(cfg_.clockHz, cfg_.msbFirst ? MSBFIRST : LSBFIRST,
                               cfg_.mode == 0   ? SPI_MODE0
                               : cfg_.mode == 1 ? SPI_MODE1
                               : cfg_.mode == 2 ? SPI_MODE2
                                                : SPI_MODE3);
    SPI.beginTransaction(settings);

    const size_t n = tx.size() > rx.size() ? tx.size() : rx.size();
    for (size_t i = 0; i < n; ++i) {
        const u8 out = i < tx.size() ? tx[i] : 0xFF;
        const u8 in  = SPI.transfer(out);
        if (i < rx.size()) rx[i] = in;
    }

    SPI.endTransaction();
    return ok();
}

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------

Status ArduinoUart::begin(const UartConfig& cfg) {
    if (!io_) return fail(Err::NotInitialized);

    if (!hw_) {
        // Port wirtualny (USB CDC): prędkość i format nie mają znaczenia,
        // a otwarcie strumienia robi za nas core.
        return ok();
    }

    u32 format = SERIAL_8N1;
    if (cfg.dataBits == 8 && cfg.stopBits == 1) {
        format = cfg.parity == 'E' ? SERIAL_8E1 : (cfg.parity == 'O' ? SERIAL_8O1 : SERIAL_8N1);
    } else if (cfg.dataBits == 8 && cfg.stopBits == 2) {
        format = SERIAL_8N2;
    }

#if HYDRA_PLAT_ESP32
    hw_->begin(cfg.baud, format, static_cast<int>(cfg.rx), static_cast<int>(cfg.tx));
#elif HYDRA_PLAT_RP2
    // Piny da się przypisać tylko sprzętowemu UART-owi. Port wirtualny (USB CDC)
    // ich nie ma, a żądanie konkretnych wyprowadzeń jest wtedy pomyłką
    // konfiguracyjną — lepiej ją zgłosić niż po cichu zignorować.
    if (cfg.rx != kNoPin || cfg.tx != kNoPin) {
        if (!uart_) return fail(Err::NotSupported);
        if (cfg.rx != kNoPin) uart_->setRX(static_cast<u8>(cfg.rx));
        if (cfg.tx != kNoPin) uart_->setTX(static_cast<u8>(cfg.tx));
    }
    hw_->begin(cfg.baud, format);
#else
    hw_->begin(cfg.baud, static_cast<u8>(format));
#endif
    return ok();
}

void   ArduinoUart::end()       { if (hw_) hw_->end(); }
size_t ArduinoUart::available() { return io_ ? static_cast<size_t>(io_->available()) : 0; }
void   ArduinoUart::flush()     { if (io_) io_->flush(); }

size_t ArduinoUart::doWrite(CByteSpan data) {
    if (!io_) return 0;
    return io_->write(data.data(), data.size());
}

size_t ArduinoUart::doRead(ByteSpan out) {
    if (!io_) return 0;

    size_t n = 0;
    while (n < out.size() && io_->available() > 0) {
        const int c = io_->read();
        if (c < 0) break;
        out[n++] = static_cast<u8>(c);
    }
    return n;
}

}  // namespace arduino
}  // namespace hal
}  // namespace hydra

#endif  // !HYDRA_PLAT_HOST
