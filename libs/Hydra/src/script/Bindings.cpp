/**
 * Hydra — implementacja biblioteki `hydra` widocznej ze skryptu.
 *
 * Konwencja zwracania błędów jest tu inna niż w reszcie frameworka i to jest
 * zamierzone: w C++ błąd propaguje `expected<T, Err>`, ale skrypt pisze się
 * w Lua i tam idiomem jest para `wartość, komunikat`. Funkcja natywna zgłasza
 * więc twardy błąd (`Ctx::fail`) wyłącznie przy złym użyciu API — brakującym
 * albo niewłaściwym argumencie — a niepowodzenie sprzętu oddaje jako
 * `nil, "opis"`, żeby skrypt mógł je obsłużyć bez `pcall`.
 */

#include "hydra/script/Bindings.hpp"

#include "SignalQueue.hpp"

#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/core/Version.hpp"
#include "hydra/hal/Hal.hpp"

#include "LuaInternal.hpp"

HYDRA_LOG_MODULE("lua")

namespace hydra {
namespace script {

namespace {

/** Oddaje `nil, "opis"` — idiom Lua na niepowodzenie możliwe do obsłużenia. */
int softFail(Ctx& c, Err e) {
    c.pushNil();
    c.pushStr(toString(e));
    return 2;
}

/** Skleja wszystkie argumenty w jeden wiersz, tak jak robi to `print`. */
void joinArgs(Ctx& c, char* out, size_t cap) {
    size_t used = 0;
    out[0]      = '\0';
    for (int i = 1; i <= c.argCount(); ++i) {
        const char* piece = c.text(i);
        if (piece == nullptr) continue;
        if (used > 0 && used + 1 < cap) out[used++] = ' ';
        const size_t len = strlen(piece);
        const size_t fit = (used + len < cap) ? len : (cap > used + 1 ? cap - used - 1 : 0);
        memcpy(out + used, piece, fit);
        used += fit;
        out[used] = '\0';
        if (used + 1 >= cap) break;
    }
}

// ---------------------------------------------------------------------------
// hydra.* — rdzeń
// ---------------------------------------------------------------------------

int coreMillis(Ctx& c) {
    c.pushInt(static_cast<i32>(rtos::nowMs()));
    return 1;
}

int coreMicros(Ctx& c) {
    // Mikrosekundy przekraczają zakres 32-bitowej liczby całkowitej Lua po
    // ok. 36 minutach, więc oddajemy je jako liczbę zmiennoprzecinkową.
    // Do mierzenia odstępów wystarcza; do znacznika bezwzględnego nie.
    c.pushNumber(static_cast<float>(rtos::nowUs()));
    return 1;
}

/** Maksymalne uśpienie z jednego wywołania — patrz komentarz przy rejestracji. */
constexpr u32 kMaxDelayMs = 1000;

int coreDelay(Ctx& c) {
    auto ms = c.argInt(1);
    if (!ms) return c.fail("delay oczekuje liczby milisekund");
    if (*ms < 0) return c.fail("delay nie przyjmuje wartosci ujemnej");

    const u32 capped = static_cast<u32>(*ms) > kMaxDelayMs ? kMaxDelayMs : static_cast<u32>(*ms);
    rtos::delayMs(capped);
    c.pushInt(static_cast<i32>(capped));
    return 1;
}

const Reg kCoreReg[] = {
    {"millis", coreMillis},
    {"micros", coreMicros},
    {"delay",  coreDelay},
    {nullptr,  nullptr},
};

// ---------------------------------------------------------------------------
// hydra.log
// ---------------------------------------------------------------------------

int logAt(Ctx& c, LogLevel level) {
    char line[HYDRA_LOG_LINE_MAX];
    joinArgs(c, line, sizeof(line));
    // Moduł logu jest stały ("lua"), więc w logu widać, co pochodzi ze skryptu,
    // a poziom da się dla samych skryptów przyciąć przez Log::setModuleLevel.
    HYDRA_LOG_AT(level, "lua", "%s", line);
    return 0;
}

int logTrace(Ctx& c) { return logAt(c, LogLevel::Trace); }
int logDebug(Ctx& c) { return logAt(c, LogLevel::Debug); }
int logInfo(Ctx& c)  { return logAt(c, LogLevel::Info); }
int logWarn(Ctx& c)  { return logAt(c, LogLevel::Warn); }
int logError(Ctx& c) { return logAt(c, LogLevel::Error); }

const Reg kLogReg[] = {
    {"trace", logTrace}, {"debug", logDebug}, {"info", logInfo},
    {"warn",  logWarn},  {"error", logError}, {nullptr, nullptr},
};

// ---------------------------------------------------------------------------
// hydra.gpio
// ---------------------------------------------------------------------------

Result<hal::PinMode> parsePinMode(const char* name) {
    struct Entry { const char* text; hal::PinMode mode; };
    static const Entry kModes[] = {
        {"in",           hal::PinMode::Input},
        {"in_pullup",    hal::PinMode::InputPullUp},
        {"in_pulldown",  hal::PinMode::InputPullDown},
        {"out",          hal::PinMode::Output},
        {"out_od",       hal::PinMode::OutputOpenDrain},
        {"analog",       hal::PinMode::Analog},
    };
    for (const Entry& e : kModes) {
        if (strcmp(e.text, name) == 0) return e.mode;
    }
    return unexpected(Err::BadArgument);
}

int gpioMode(Ctx& c) {
    auto pin  = c.argInt(1);
    auto name = c.argStr(2);
    if (!pin || !name) return c.fail("gpio.mode(pin, tryb)");

    auto mode = parsePinMode(*name);
    if (!mode) return c.fail("nieznany tryb pinu: %s", *name);

    auto r = hal::Hal::gpio().configure(static_cast<hal::PinNum>(*pin), *mode);
    if (!r) return softFail(c, r.error());
    c.pushBool(true);
    return 1;
}

int gpioWrite(Ctx& c) {
    auto pin = c.argInt(1);
    if (!pin) return c.fail("gpio.write(pin, stan)");
    // Świadomie prawdziwość w rozumieniu Lua, a nie wymóg typu boolean:
    // `gpio.write(pin, 1)` jest tym, czego pisze skrypt sterujący.
    const bool high = c.optBool(2, false) || (c.isNumber(2) && c.optInt(2, 0) != 0);

    auto r = hal::Hal::gpio().write(static_cast<hal::PinNum>(*pin), high);
    if (!r) return softFail(c, r.error());
    c.pushBool(true);
    return 1;
}

int gpioRead(Ctx& c) {
    auto pin = c.argInt(1);
    if (!pin) return c.fail("gpio.read(pin)");
    auto v = hal::Hal::gpio().read(static_cast<hal::PinNum>(*pin));
    if (!v) return softFail(c, v.error());
    c.pushBool(*v);
    return 1;
}

int gpioToggle(Ctx& c) {
    auto pin = c.argInt(1);
    if (!pin) return c.fail("gpio.toggle(pin)");
    auto r = hal::Hal::gpio().toggle(static_cast<hal::PinNum>(*pin));
    if (!r) return softFail(c, r.error());
    c.pushBool(true);
    return 1;
}

const Reg kGpioReg[] = {
    {"mode", gpioMode}, {"write", gpioWrite}, {"read", gpioRead},
    {"toggle", gpioToggle}, {nullptr, nullptr},
};

// ---------------------------------------------------------------------------
// hydra.adc
// ---------------------------------------------------------------------------

int adcRaw(Ctx& c) {
    auto pin = c.argInt(1);
    if (!pin) return c.fail("adc.raw(pin)");
    auto v = hal::Hal::adc().readRaw(static_cast<hal::PinNum>(*pin));
    if (!v) return softFail(c, v.error());
    c.pushInt(*v);
    return 1;
}

int adcMv(Ctx& c) {
    auto pin = c.argInt(1);
    if (!pin) return c.fail("adc.mv(pin)");
    auto v = hal::Hal::adc().readMv(static_cast<hal::PinNum>(*pin));
    if (!v) return softFail(c, v.error());
    c.pushInt(static_cast<i32>(*v));
    return 1;
}

const Reg kAdcReg[] = {{"raw", adcRaw}, {"mv", adcMv}, {nullptr, nullptr}};

// ---------------------------------------------------------------------------
// hydra.pwm
// ---------------------------------------------------------------------------

int pwmSetup(Ctx& c) {
    auto pin  = c.argInt(1);
    auto freq = c.argInt(2);
    if (!pin || !freq) return c.fail("pwm.setup(pin, czestotliwoscHz [, bity])");
    const i32 bits = c.optInt(3, 10);

    auto r = hal::Hal::pwm().configure(static_cast<hal::PinNum>(*pin), static_cast<u32>(*freq),
                                       static_cast<u8>(bits));
    if (!r) return softFail(c, r.error());
    c.pushBool(true);
    return 1;
}

int pwmDuty(Ctx& c) {
    auto pin      = c.argInt(1);
    auto permille = c.argInt(2);
    if (!pin || !permille) return c.fail("pwm.duty(pin, promile)");
    if (*permille < 0 || *permille > 1000) return c.fail("wypelnienie poza zakresem 0..1000");

    auto r = hal::Hal::pwm().setDutyPermille(static_cast<hal::PinNum>(*pin),
                                             static_cast<u16>(*permille));
    if (!r) return softFail(c, r.error());
    c.pushBool(true);
    return 1;
}

int pwmMicroseconds(Ctx& c) {
    auto pin = c.argInt(1);
    auto us  = c.argInt(2);
    if (!pin || !us) return c.fail("pwm.us(pin, mikrosekundy)");
    auto r = hal::Hal::pwm().writeMicroseconds(static_cast<hal::PinNum>(*pin),
                                               static_cast<u16>(*us));
    if (!r) return softFail(c, r.error());
    c.pushBool(true);
    return 1;
}

int pwmRelease(Ctx& c) {
    auto pin = c.argInt(1);
    if (!pin) return c.fail("pwm.release(pin)");
    auto r = hal::Hal::pwm().release(static_cast<hal::PinNum>(*pin));
    if (!r) return softFail(c, r.error());
    c.pushBool(true);
    return 1;
}

const Reg kPwmReg[] = {
    {"setup", pwmSetup}, {"duty", pwmDuty}, {"us", pwmMicroseconds},
    {"release", pwmRelease}, {nullptr, nullptr},
};

// ---------------------------------------------------------------------------
// hydra.i2c
// ---------------------------------------------------------------------------

/** Najdłuższa transakcja I²C dostępna ze skryptu — bufor leży na stosie taska. */
constexpr u8 kMaxI2cBytes = 32;

int i2cScan(Ctx& c) {
    const i32 bus = c.optInt(1, 0);
    u8        found[16];
    auto      count = hal::Hal::i2c(static_cast<u8>(bus)).scan(found, sizeof(found));
    if (!count) return softFail(c, count.error());

    c.pushTable();
    for (u8 i = 0; i < *count; ++i) {
        c.pushInt(found[i]);
        c.setIndex(i + 1);
    }
    return 1;
}

int i2cPing(Ctx& c) {
    auto addr = c.argInt(1);
    if (!addr) return c.fail("i2c.ping(adres [, magistrala])");
    const i32 bus = c.optInt(2, 0);

    Status result = fail(Err::NotInitialized);
    hal::Hal::i2c(static_cast<u8>(bus))
        .transaction([&](hal::II2cBus::Session& s) {
            result = s.ping(static_cast<u8>(*addr));
            return ok();
        });

    c.pushBool(static_cast<bool>(result));
    return 1;
}

int i2cRead(Ctx& c) {
    auto addr = c.argInt(1);
    auto reg  = c.argInt(2);
    auto len  = c.argInt(3);
    if (!addr || !reg || !len) return c.fail("i2c.read(adres, rejestr, ile [, magistrala])");
    if (*len <= 0 || *len > kMaxI2cBytes) return c.fail("ile musi byc w zakresie 1..%u", kMaxI2cBytes);
    const i32 bus = c.optInt(4, 0);

    u8 buffer[kMaxI2cBytes];
    // Stan transakcji trzymany w jednej strukturze, żeby domknięcie przechwyciło
    // jeden wskaźnik zamiast pięciu zmiennych — budżet `Delegate` to cztery
    // wskaźniki i jest tak wąski celowo (rozdz. 11).
    struct ReadOp {
        u8       addr;
        u8       reg;
        ByteSpan out;
        Status   result = fail(Err::NotInitialized);
    } op{static_cast<u8>(*addr), static_cast<u8>(*reg),
         ByteSpan{buffer, static_cast<size_t>(*len)}, fail(Err::NotInitialized)};

    hal::Hal::i2c(static_cast<u8>(bus)).transaction([&op](hal::II2cBus::Session& s) {
        op.result = s.readReg(op.addr, op.reg, op.out);
        return ok();
    });
    if (!op.result) return softFail(c, op.result.error());

    c.pushTable();
    for (i32 i = 0; i < *len; ++i) {
        c.pushInt(buffer[i]);
        c.setIndex(i + 1);
    }
    return 1;
}

int i2cWrite(Ctx& c) {
    auto addr = c.argInt(1);
    auto reg  = c.argInt(2);
    if (!addr || !reg) return c.fail("i2c.write(adres, rejestr, dane [, magistrala])");

    u8     payload[kMaxI2cBytes];
    size_t count = 0;

    if (c.isTable(3)) {
        auto len = c.tableLength(3);
        if (!len) return c.fail("trzeci argument musi byc tabela bajtow albo liczba");
        if (*len > kMaxI2cBytes) return c.fail("najwyzej %u bajtow na transakcje", kMaxI2cBytes);
        for (u32 i = 0; i < *len; ++i) {
            auto byte = c.indexInt(3, static_cast<i32>(i) + 1);
            if (!byte) return c.fail("element %u tabeli nie jest liczba", i + 1);
            payload[i] = static_cast<u8>(*byte);
        }
        count = *len;
    } else {
        auto single = c.argInt(3);
        if (!single) return c.fail("trzeci argument musi byc tabela bajtow albo liczba");
        payload[0] = static_cast<u8>(*single);
        count      = 1;
    }

    const i32 bus = c.optInt(4, 0);

    struct WriteOp {
        u8        addr;
        u8        reg;
        CByteSpan data;
        Status    result;
    } op{static_cast<u8>(*addr), static_cast<u8>(*reg), CByteSpan{payload, count},
         fail(Err::NotInitialized)};

    hal::Hal::i2c(static_cast<u8>(bus)).transaction([&op](hal::II2cBus::Session& s) {
        op.result = s.writeReg(op.addr, op.reg, op.data);
        return ok();
    });
    if (!op.result) return softFail(c, op.result.error());
    c.pushBool(true);
    return 1;
}

const Reg kI2cReg[] = {
    {"scan", i2cScan}, {"ping", i2cPing}, {"read", i2cRead}, {"write", i2cWrite},
    {nullptr, nullptr},
};

// ---------------------------------------------------------------------------
// hydra.event
// ---------------------------------------------------------------------------

/**
 * Kolejka sygnałów przychodzących.
 *
 * Subskrypcja jest typu Direct, więc callback wykonuje się w kontekście
 * nadawcy — a tym bywa pętla sterowania albo task sieciowy. Wołanie stamtąd
 * kodu Lua byłoby błędem podwójnie: interpreter nie jest wielobieżny, a skrypt
 * o nieznanym czasie wykonania zatrzymałby cudzą pętlę. Dlatego callback robi
 * jedną rzecz — odkłada POD do pierścienia — a interpretację przejmuje task
 * skryptu w `dispatchSignals`.
 */
/** Klucz tabeli handlerów w rejestrze Lua. */
constexpr const char* kHandlerTable = "hydra.script.handlers";

/** Zostawia na stosie tabelę handlerów, tworząc ją przy pierwszym użyciu. */
void pushHandlerTable(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kHandlerTable);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kHandlerTable);
    }
}

int eventEmit(Ctx& c) {
    auto name = c.argStr(1);
    if (!name) return c.fail("event.emit(nazwa [, wartosc] [, dana])");

    ScriptSignal signal{};
    signal.nameId = nameId(*name);
    signal.value  = c.optNumber(2, 0.0f);
    signal.data   = c.optInt(3, 0);
    EventBus::publish(signal);

    c.pushBool(true);
    return 1;
}

int eventOn(Ctx& c) {
    auto name = c.argStr(1);
    if (!name) return c.fail("event.on(nazwa, funkcja)");
    if (!c.isFunction(2)) return c.fail("drugi argument musi byc funkcja");

    auto* L = static_cast<lua_State*>(c.interp().rawState());
    pushHandlerTable(L);
    lua_pushvalue(L, 2);  // funkcja handlera
    lua_seti(L, -2, static_cast<lua_Integer>(nameId(*name)));
    lua_pop(L, 1);

    c.pushBool(true);
    return 1;
}

const Reg kEventReg[] = {{"emit", eventEmit}, {"on", eventOn}, {nullptr, nullptr}};

}  // namespace

// ---------------------------------------------------------------------------
// Montaż
// ---------------------------------------------------------------------------

Status installBindings(Interp& interp) { return installBindings(interp, BindingSet{}); }

Status installBindings(Interp& interp, const BindingSet& set) {
    if (!interp.ready()) return fail(Err::NotInitialized);

    if (set.core) {
        HYDRA_CHECK(interp.registerLib("hydra", kCoreReg));
        HYDRA_CHECK(interp.setGlobalStr("__hydra_platform", HYDRA_PLATFORM_NAME));
        HYDRA_CHECK(interp.setGlobalStr("__hydra_version", version()));
        // Stałe wygodniej trzymać w tabeli `hydra` niż w zasięgu globalnym —
        // przenosimy je tam jednym fragmentem zamiast dokładać API do Interp.
        HYDRA_CHECK(interp.doString(
            "hydra.platform = __hydra_platform hydra.version = __hydra_version "
            "__hydra_platform = nil __hydra_version = nil",
            "=bindings"));
    }
    if (set.log)  HYDRA_CHECK(interp.registerLib("hydra.log", kLogReg));
    if (set.gpio) HYDRA_CHECK(interp.registerLib("hydra.gpio", kGpioReg));
    if (set.adc)  HYDRA_CHECK(interp.registerLib("hydra.adc", kAdcReg));
    if (set.pwm)  HYDRA_CHECK(interp.registerLib("hydra.pwm", kPwmReg));
    if (set.i2c)  HYDRA_CHECK(interp.registerLib("hydra.i2c", kI2cReg));

    if (set.event) {
        HYDRA_CHECK(interp.registerLib("hydra.event", kEventReg));
        HYDRA_CHECK(detail::signalQueueSubscribe());
    }
    return ok();
}

u32 dispatchSignals(Interp& interp, u32 maxSignals) {
    if (!interp.ready()) return 0;
    auto* L = static_cast<lua_State*>(interp.rawState());

    u32          handled = 0;
    ScriptSignal signal{};
    while (handled < maxSignals && detail::signalQueuePop(signal)) {
        pushHandlerTable(L);
        lua_geti(L, -1, static_cast<lua_Integer>(signal.nameId));
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 2);  // wartość spod klucza i tabela handlerów
            ++handled;      // sygnał bez handlera też jest obsłużony
            continue;
        }

        lua_pushnumber(L, static_cast<lua_Number>(signal.value));
        lua_pushinteger(L, static_cast<lua_Integer>(signal.data));

        // Handler skryptu jest kodem użytkownika — wołamy go pod ochroną,
        // żeby błąd w jednym nie zatrzymał obsługi pozostałych.
        const int status = lua_pcall(L, 2, 0, 0);
        if (status != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            interp.setError(msg ? msg : "blad w obsludze sygnalu");
            HYDRA_LOGW("handler sygnalu: %s", interp.error());
            lua_pop(L, 1);
        }
        lua_pop(L, 1);  // tabela handlerów
        ++handled;
    }
    flushOutput();
    return handled;
}

u32 droppedSignals() { return detail::signalQueueDropped(); }

void removeBindings(Interp& interp) {
    detail::signalQueueRelease();
    (void)interp;
}

}  // namespace script
}  // namespace hydra
