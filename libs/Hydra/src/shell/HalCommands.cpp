/**
 * Hydra — komendy warstwy sprzętowej dla shella (rozdz. 13).
 *
 * Skan magistrali, podgląd i zmiana konfiguracji, odczyt napięć. To ten
 * zestaw odpowiada na pytanie „czy w ogóle podłączyłem to poprawnie",
 * zanim ktokolwiek zacznie szukać błędu w kodzie.
 */

#include <stdlib.h>
#include <string.h>

#include "hydra/hal/Hal.hpp"
#include "hydra/shell/Shell.hpp"

namespace hydra {
namespace shell {
namespace {

/** Skan magistrali I2C — pierwsza komenda, po którą sięga się przy nowym układzie. */
Status cmdI2c(int argc, char** argv, Output& out) {
    u8 bus = 0;
    if (argc > 2) bus = static_cast<u8>(strtoul(argv[2], nullptr, 10));

    if (argc < 2 || strcmp(argv[1], "scan") != 0) {
        out.writeLine("użycie: i2c scan [magistrala]");
        return fail(Err::BadArgument);
    }

    if (!hal::Hal::hasI2c(bus)) {
        out.printf("magistrala %u nieskonfigurowana\r\n", static_cast<unsigned>(bus));
        return fail(Err::NotSupported);
    }

    u8   found[16];
    auto count = hal::Hal::i2c(bus).scan(found, sizeof(found));
    if (!count) return fail(count.error());

    for (u8 i = 0; i < *count; ++i) {
        out.printf("0x%02X\r\n", found[i]);
    }
    out.field("found", static_cast<u32>(*count));
    out.field("clock_hz", hal::Hal::i2c(bus).clockHz());
    return ok();
}

Status cmdGpio(int argc, char** argv, Output& out) {
    if (argc < 3) {
        out.writeLine("użycie: gpio read <pin> | gpio write <pin> <0|1>");
        return fail(Err::BadArgument);
    }

    const auto pin = static_cast<hal::PinNum>(strtol(argv[2], nullptr, 10));

    if (strcmp(argv[1], "read") == 0) {
        HYDRA_CHECK(hal::Hal::gpio().configure(pin, hal::PinMode::InputPullUp));
        HYDRA_TRY(const bool level, hal::Hal::gpio().read(pin));
        out.field("pin", static_cast<i32>(pin));
        out.field("level", static_cast<u32>(level ? 1 : 0));
        return ok();
    }

    if (strcmp(argv[1], "write") == 0) {
        if (argc < 4) return fail(Err::BadArgument);
        const bool level = strtol(argv[3], nullptr, 10) != 0;
        HYDRA_CHECK(hal::Hal::gpio().configure(pin, hal::PinMode::Output));
        HYDRA_CHECK(hal::Hal::gpio().write(pin, level));
        out.field("pin", static_cast<i32>(pin));
        out.field("level", static_cast<u32>(level ? 1 : 0));
        return ok();
    }

    return fail(Err::BadArgument);
}

Status cmdAdc(int argc, char** argv, Output& out) {
    if (argc < 2) {
        out.writeLine("użycie: adc <pin>");
        return fail(Err::BadArgument);
    }
    if (!hal::Hal::hasAdc()) return fail(Err::NotSupported);

    const auto pin = static_cast<hal::PinNum>(strtol(argv[1], nullptr, 10));
    HYDRA_CHECK(hal::Hal::adc().configure(pin, hal::AdcConfig{}));

    HYDRA_TRY(const u16 raw, hal::Hal::adc().readRaw(pin));
    HYDRA_TRY(const u32 mv, hal::Hal::adc().readMv(pin));

    out.field("pin", static_cast<i32>(pin));
    out.field("raw", static_cast<u32>(raw));
    // Wartość po kalibracji i dzielniku — czyli napięcie źródła, nie pinu.
    out.field("mv", mv);
    return ok();
}

/** Podgląd i zmiana konfiguracji trwałej. */
Status cmdCfg(int argc, char** argv, Output& out) {
    if (argc < 2) {
        out.writeLine("użycie: cfg get <przestrzeń> <klucz> | cfg set <przestrzeń> <klucz> <wartość>");
        out.writeLine("        cfg erase <przestrzeń> <klucz>");
        return fail(Err::BadArgument);
    }

    if (!hal::Hal::hasStorage()) return fail(Err::NotSupported);
    auto& storage = hal::Hal::storage();

    if (strcmp(argv[1], "get") == 0) {
        if (argc < 4) return fail(Err::BadArgument);
        HYDRA_CHECK(storage.begin(argv[2], true));

        char value[64];
        auto length = storage.getString(argv[3], value, sizeof(value));
        if (!length) return fail(length.error());

        out.field(argv[3], value);
        return ok();
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc < 5) return fail(Err::BadArgument);
        HYDRA_CHECK(storage.begin(argv[2], false));
        HYDRA_CHECK(storage.setString(argv[3], argv[4]));
        HYDRA_CHECK(storage.commit());
        out.field(argv[3], argv[4]);
        return ok();
    }

    if (strcmp(argv[1], "erase") == 0) {
        if (argc < 4) return fail(Err::BadArgument);
        HYDRA_CHECK(storage.begin(argv[2], false));
        HYDRA_CHECK(storage.erase(argv[3]));
        HYDRA_CHECK(storage.commit());
        out.field("erased", argv[3]);
        return ok();
    }

    return fail(Err::BadArgument);
}

Status cmdHal(int argc, char** argv, Output& out) {
    HYDRA_UNUSED(argc);
    HYDRA_UNUSED(argv);

    out.field("backend", hal::Hal::backendName());
    out.field("reset_reason", toString(hal::Hal::resetReason()));
    out.field("gpio", static_cast<u32>(hal::Hal::hasGpio() ? 1 : 0));
    out.field("i2c0", static_cast<u32>(hal::Hal::hasI2c(0) ? 1 : 0));
    out.field("spi0", static_cast<u32>(hal::Hal::hasSpi(0) ? 1 : 0));
    out.field("adc", static_cast<u32>(hal::Hal::hasAdc() ? 1 : 0));
    out.field("storage", static_cast<u32>(hal::Hal::hasStorage() ? 1 : 0));
    return ok();
}

}  // namespace

Status registerHalCommands(Shell& shell) {
    HYDRA_CHECK(shell.add("i2c", "skan magistrali; i2c scan [nr]", &cmdI2c));
    HYDRA_CHECK(shell.add("gpio", "gpio read|write <pin> [wartość]", &cmdGpio));
    HYDRA_CHECK(shell.add("adc", "odczyt napięcia; adc <pin>", &cmdAdc));
    HYDRA_CHECK(shell.add("cfg", "konfiguracja trwała; cfg get|set|erase", &cmdCfg));
    HYDRA_CHECK(shell.add("hal", "stan warstwy sprzętowej", &cmdHal));
    return ok();
}

}  // namespace shell
}  // namespace hydra
