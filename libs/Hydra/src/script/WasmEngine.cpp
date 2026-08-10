/**
 * Hydra — silnik skryptowy WebAssembly na wasm3.
 *
 * Trzy rzeczy dzieją się tu naprawdę: linkowanie importów z `BindingSet`,
 * przekład wyniku wykonania na `JobState` i pilnowanie, żeby wasm3 alokował
 * wyłącznie z puli Hydry. Reszta to przekład API.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_WASM

#include "hydra/script/WasmEngine.hpp"

#if HYDRA_SCRIPT_WASM_ENGINE != HYDRA_WASM_ENGINE_WASM3
#  error "HYDRA_SCRIPT_WASM_ENGINE: zaimplementowany jest wylacznie wasm3. \
WAMR wymaga innej drogi osadzenia niz vendor_wasm3.sh — patrz docs/plan-wasm-runtime.md, faza 3."
#endif

#include <stdio.h>
#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/script/WasmAlloc.h"

#include "SignalQueue.hpp"

extern "C" {
#include "wasm3.h"
}

HYDRA_LOG_MODULE("wasm")

namespace hydra {
namespace script {

namespace detail {
/** Zdefiniowana w WasmAlloc.cpp — pula, z której wasm3 bierze pamięć. */
extern Heap* gWasmHeap;
}  // namespace detail

namespace {

/** Pula domyślna silnika WASM. Rozmiar ustala `Profile.hpp`. */
alignas(8) u8 gDefaultWasmPool[HYDRA_WASM_HEAP_BYTES];
bool gDefaultWasmPoolTaken = false;

/**
 * Budżet jednego wywołania `on_event`. Handler zdarzenia jest kodem użytkownika
 * i nie ma prawa zatrzymać obsługi pozostałych sygnałów.
 */
constexpr u32 kEventHandlerFuel = 10000;

/** Nazwa modułu, w którym leżą wszystkie importy Hydry. */
constexpr const char* kImportModule = "hydra";

// ---------------------------------------------------------------------------
// Importy — to, co moduł widzi z urządzenia
// ---------------------------------------------------------------------------
//
// Napisy przekazuje się parą (offset, długość): pamięć modułu jest jego własną
// przestrzenią adresową i wskaźnik hosta nic by tam nie znaczył.

m3ApiRawFunction(wasmMillis) {
    m3ApiReturnType(uint32_t);
    m3ApiReturn(static_cast<uint32_t>(rtos::nowMs()));
}

m3ApiRawFunction(wasmMicros) {
    m3ApiReturnType(uint32_t);
    m3ApiReturn(static_cast<uint32_t>(rtos::nowUs()));
}

m3ApiRawFunction(wasmDelay) {
    m3ApiGetArg(uint32_t, ms);
    rtos::delayMs(ms);
    m3ApiSuccess();
}

m3ApiRawFunction(wasmLog) {
    m3ApiGetArg(uint32_t, level);
    m3ApiGetArgMem(const char*, text);
    m3ApiGetArg(uint32_t, length);

    m3ApiCheckMem(text, length);

    // Kopiujemy, bo tekst w pamięci liniowej nie jest zakończony zerem,
    // a `HYDRA_LOG_AT` oczekuje napisu C.
    char line[HYDRA_LOG_LINE_MAX];
    const size_t n = length < sizeof(line) - 1 ? length : sizeof(line) - 1;
    memcpy(line, text, n);
    line[n] = '\0';

    const LogLevel lv = (level <= static_cast<uint32_t>(LogLevel::Error))
                            ? static_cast<LogLevel>(level)
                            : LogLevel::Info;
    HYDRA_LOG_AT(lv, "wasm", "%s", line);
    m3ApiSuccess();
}

// --- GPIO ------------------------------------------------------------------
//
// Tryb pinu jest liczbą, a nie napisem jak w Lua: przekazanie napisu kosztowałoby
// parę (offset, długość) i porównanie tekstu przy każdym wywołaniu, a wartości
// `hal::PinMode` są i tak częścią API frameworka.

m3ApiRawFunction(wasmGpioMode) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);
    m3ApiGetArg(uint32_t, mode);

    if (mode > static_cast<uint32_t>(hal::PinMode::Analog)) m3ApiReturn(0);

    auto r = hal::Hal::gpio().configure(static_cast<hal::PinNum>(pin),
                                        static_cast<hal::PinMode>(mode));
    m3ApiReturn(r ? 1u : 0u);
}

m3ApiRawFunction(wasmGpioWrite) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);
    m3ApiGetArg(uint32_t, value);

    auto r = hal::Hal::gpio().write(static_cast<hal::PinNum>(pin), value != 0);
    m3ApiReturn(r ? 1u : 0u);
}

m3ApiRawFunction(wasmGpioRead) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, pin);

    auto v = hal::Hal::gpio().read(static_cast<hal::PinNum>(pin));
    // -1 zamiast pary (wartość, błąd): WebAssembly nie zwraca dwóch wyników,
    // a stan pinu jest zawsze 0 albo 1, więc wartość spoza zakresu jest
    // jednoznaczna.
    m3ApiReturn(v ? (*v ? 1 : 0) : -1);
}

m3ApiRawFunction(wasmGpioToggle) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);

    auto r = hal::Hal::gpio().toggle(static_cast<hal::PinNum>(pin));
    m3ApiReturn(r ? 1u : 0u);
}

// --- ADC -------------------------------------------------------------------

m3ApiRawFunction(wasmAdcRaw) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, pin);

    auto v = hal::Hal::adc().readRaw(static_cast<hal::PinNum>(pin));
    m3ApiReturn(v ? static_cast<int32_t>(*v) : -1);
}

m3ApiRawFunction(wasmAdcMv) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, pin);

    auto v = hal::Hal::adc().readMv(static_cast<hal::PinNum>(pin));
    m3ApiReturn(v ? static_cast<int32_t>(*v) : -1);
}

// --- PWM -------------------------------------------------------------------

m3ApiRawFunction(wasmPwmSetup) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);
    m3ApiGetArg(uint32_t, freq);

    auto r = hal::Hal::pwm().configure(static_cast<hal::PinNum>(pin), freq, 10);
    m3ApiReturn(r ? 1u : 0u);
}

m3ApiRawFunction(wasmPwmDuty) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);
    m3ApiGetArg(uint32_t, permille);

    auto r = hal::Hal::pwm().setDutyPermille(static_cast<hal::PinNum>(pin),
                                             static_cast<u16>(permille));
    m3ApiReturn(r ? 1u : 0u);
}

m3ApiRawFunction(wasmPwmUs) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);
    m3ApiGetArg(uint32_t, us);

    auto r = hal::Hal::pwm().writeMicroseconds(static_cast<hal::PinNum>(pin),
                                               static_cast<u16>(us));
    m3ApiReturn(r ? 1u : 0u);
}

m3ApiRawFunction(wasmPwmRelease) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);

    auto r = hal::Hal::pwm().release(static_cast<hal::PinNum>(pin));
    m3ApiReturn(r ? 1u : 0u);
}


// --- Zdarzenia -------------------------------------------------------------

/**
 * Skrót nazwy liczony nad parą (offset, długość).
 *
 * Musi dawać dokładnie to samo, co `nameId()` z `Events.hpp`, bo po tej samej
 * liczbie host rozpoznaje sygnał. Osobna funkcja, bo tamta chodzi po napisie
 * zakończonym zerem, a w pamięci liniowej modułu zera nie ma.
 */
u16 nameIdOfSpan(const char* text, u32 length) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < length; ++i) {
        h ^= static_cast<u8>(text[i]);
        h *= 16777619u;
    }
    return static_cast<u16>((h >> 16) ^ (h & 0xFFFF));
}

m3ApiRawFunction(wasmEventEmit) {
    m3ApiGetArgMem(const char*, namePtr);
    m3ApiGetArg(uint32_t, nameLen);
    m3ApiGetArg(float, value);
    m3ApiGetArg(int32_t, data);

    m3ApiCheckMem(namePtr, nameLen);

    ScriptSignal signal{};
    signal.nameId = nameIdOfSpan(namePtr, nameLen);
    signal.value  = value;
    signal.data   = data;
    EventBus::publish(signal);
    m3ApiSuccess();
}

m3ApiRawFunction(wasmEventNameId) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArgMem(const char*, namePtr);
    m3ApiGetArg(uint32_t, nameLen);

    m3ApiCheckMem(namePtr, nameLen);
    m3ApiReturn(static_cast<uint32_t>(nameIdOfSpan(namePtr, nameLen)));
}

// --- I2C -------------------------------------------------------------------
//
// Lua oddaje tu tabele, których WebAssembly nie zna. Dane wracają więc do
// pamięci liniowej modułu pod wskazany offset, a wynik funkcji mówi, ile ich
// jest — albo że był błąd. Każdy bufor jest sprawdzany wobec granic pamięci
// modułu: `m3ApiCheckMem` chroni przed offsetem, który moduł policzył źle.

m3ApiRawFunction(wasmI2cPing) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, bus);
    m3ApiGetArg(uint32_t, addr);

    bool present = false;
    (void)hal::Hal::i2c(static_cast<u8>(bus)).transaction([&](hal::II2cBus::Session& s) {
        present = s.ping(static_cast<u8>(addr)).has_value();
        return ok();
    });
    m3ApiReturn(present ? 1u : 0u);
}

m3ApiRawFunction(wasmI2cScan) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, bus);
    m3ApiGetArgMem(uint8_t*, outPtr);
    m3ApiGetArg(uint32_t, capacity);

    m3ApiCheckMem(outPtr, capacity);
    if (capacity == 0 || capacity > 128) m3ApiReturn(-1);

    auto found = hal::Hal::i2c(static_cast<u8>(bus)).scan(outPtr, static_cast<u8>(capacity));
    m3ApiReturn(found ? static_cast<int32_t>(*found) : -1);
}

m3ApiRawFunction(wasmI2cRead) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, bus);
    m3ApiGetArg(uint32_t, addr);
    m3ApiGetArg(uint32_t, reg);
    m3ApiGetArgMem(uint8_t*, outPtr);
    m3ApiGetArg(uint32_t, length);

    m3ApiCheckMem(outPtr, length);
    if (length == 0) m3ApiReturn(-1);

    // Argumenty w strukturze, a wewnątrz domknięcia jeden wskaźnik: `Delegate`
    // ma stałą pojemność, a przechwycenie pięciu zmiennych ją przekracza.
    struct Op { u8 addr; u8 reg; u8* out; u32 len; bool done; } op{
        static_cast<u8>(addr), static_cast<u8>(reg), outPtr, length, false};

    (void)hal::Hal::i2c(static_cast<u8>(bus)).transaction([&op](hal::II2cBus::Session& s) {
        op.done = s.readReg(op.addr, op.reg, ByteSpan{op.out, op.len}).has_value();
        return ok();
    });
    m3ApiReturn(op.done ? static_cast<int32_t>(length) : -1);
}

m3ApiRawFunction(wasmI2cWrite) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, bus);
    m3ApiGetArg(uint32_t, addr);
    m3ApiGetArg(uint32_t, reg);
    m3ApiGetArgMem(const uint8_t*, dataPtr);
    m3ApiGetArg(uint32_t, length);

    m3ApiCheckMem(dataPtr, length);

    struct Op { u8 addr; u8 reg; const u8* data; u32 len; bool done; } op{
        static_cast<u8>(addr), static_cast<u8>(reg), dataPtr, length, false};

    (void)hal::Hal::i2c(static_cast<u8>(bus)).transaction([&op](hal::II2cBus::Session& s) {
        op.done = s.writeReg(op.addr, op.reg, CByteSpan{op.data, op.len}).has_value();
        return ok();
    });
    m3ApiReturn(op.done ? 1 : -1);
}

/** Jeden import: nazwa, sygnatura wasm3 i funkcja. */
struct Import {
    const char*      name;
    const char*      signature;
    M3RawCall        fn;
};

#include "wasm_imports.inc"

/**
 * Wiąże grupę z modułem.
 *
 * `m3Err_functionLookupFailed` nie jest błędem: znaczy tylko, że moduł tego
 * importu nie zadeklarował. Linkujemy wszystko, co mamy, a moduł bierze to,
 * czego potrzebuje — tak jak skrypt Lua nie musi wołać każdej funkcji z tabeli.
 */
Status linkGroup(IM3Module module, const Import* imports, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        M3Result r = m3_LinkRawFunction(module, kImportModule, imports[i].name,
                                        imports[i].signature, imports[i].fn);
        if (r != m3Err_none && r != m3Err_functionLookupFailed) return fail(Err::BadArgument);
    }
    return ok();
}

}  // namespace

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status WasmEngine::open(void* pool, size_t poolBytes) {
    if (runtime_ != nullptr) return fail(Err::Busy);

    if (pool == nullptr || poolBytes == 0) {
        if (gDefaultWasmPoolTaken) return fail(Err::Busy);
        pool             = gDefaultWasmPool;
        poolBytes        = sizeof(gDefaultWasmPool);
        ownsDefaultPool_ = true;
    }

    HYDRA_CHECK(heap_.init(pool, poolBytes));
    if (ownsDefaultPool_) gDefaultWasmPoolTaken = true;

    // Od tej chwili każdy przydział wasm3 idzie do naszej puli.
    detail::gWasmHeap = &heap_;

    environment_ = m3_NewEnvironment();
    if (environment_ == nullptr) {
        setError("brak pamieci na srodowisko wasm3");
        close();
        return fail(Err::OutOfMemory);
    }

    runtime_ = m3_NewRuntime(static_cast<IM3Environment>(environment_), cfg_.stackBytes, nullptr);
    if (runtime_ == nullptr) {
        setError("brak pamieci na runtime wasm3");
        close();
        return fail(Err::OutOfMemory);
    }

    error_[0] = '\0';
    return ok();
}

void WasmEngine::close() {
    jobCancel();

    if (runtime_ != nullptr) {
        m3_FreeRuntime(static_cast<IM3Runtime>(runtime_));
        runtime_ = nullptr;
    }
    if (environment_ != nullptr) {
        m3_FreeEnvironment(static_cast<IM3Environment>(environment_));
        environment_ = nullptr;
    }
    // Moduł należał do runtime'u i zniknął razem z nim.
    module_ = nullptr;

    detail::gWasmHeap = nullptr;
    heap_.reset();

    if (ownsDefaultPool_) {
        gDefaultWasmPoolTaken = false;
        ownsDefaultPool_      = false;
    }
}

// ---------------------------------------------------------------------------
// Bindingi
// ---------------------------------------------------------------------------

Status WasmEngine::installBindings(const BindingSet& set) {
    bindings_ = set;

    // Subskrypcja magistrali musi powstać już tutaj, a nie przy linkowaniu:
    // sygnał opublikowany między `installBindings()` a `load()` ma trafić do
    // pierścienia, a nie przepaść.
    if (set.event) HYDRA_CHECK(detail::signalQueueSubscribe());
    return ok();
}

void WasmEngine::removeBindings() { detail::signalQueueRelease(); }

u32 WasmEngine::dispatchSignals(u32 maxSignals) {
    if (runtime_ == nullptr || module_ == nullptr || !bindings_.event) return 0;

    // Odbiór zdarzeń jest eksportem, nie callbackiem: WebAssembly nie ma
    // domknięć, które dałoby się zarejestrować w tabeli tak, jak robi to
    // `hydra.event.on` w Lua. Moduł, który zdarzeń nie słucha, po prostu
    // tej funkcji nie eksportuje.
    IM3Function handler = nullptr;
    if (m3_FindFunction(&handler, static_cast<IM3Runtime>(runtime_), "on_event") != m3Err_none) {
        // Sygnały i tak trzeba zdjąć z pierścienia, inaczej zapchałby się
        // i zaczął gubić zdarzenia adresowane do kogoś innego.
        u32          drained = 0;
        ScriptSignal ignored{};
        while (drained < maxSignals && detail::signalQueuePop(ignored)) ++drained;
        return drained;
    }

    u32          handled = 0;
    ScriptSignal signal{};
    while (handled < maxSignals && detail::signalQueuePop(signal)) {
        // Budżet stały, nie z konfiguracji modułu: handler zdarzenia jest kodem
        // użytkownika i nie ma prawa zatrzymać obsługi pozostałych sygnałów
        // ani całego przebiegu.
        m3_SetFuel(static_cast<IM3Runtime>(runtime_), kEventHandlerFuel);

        const uint32_t nameId = signal.nameId;
        const float    value  = signal.value;
        const int32_t  data   = signal.data;
        const void*    args[] = {&nameId, &value, &data};

        M3Result r = m3_Call(handler, 3, args);
        if (r != m3Err_none) {
            // Błąd w handlerze nie może zatrzymać pozostałych — tak samo, jak
            // `lua_pcall` w wariancie dla Lua.
            captureError(r);
            HYDRA_LOGW("on_event(%u): %s", nameId, error_);
        }
        ++handled;
    }
    return handled;
}

Status WasmEngine::linkBindings() {
    auto* module = static_cast<IM3Module>(module_);
    if (module == nullptr) return fail(Err::NotInitialized);

    // Pętla po wygenerowanej tablicy zamiast wiersza na grupę: dołożenie grupy
    // do `wasm_bindings.def` nie może wymagać pamiętania o dopisaniu jej tutaj.
    for (const ImportGroup& g : kImportGroups) {
        if (bindings_.*(g.flag)) HYDRA_CHECK(linkGroup(module, g.imports, g.count));
    }

    return ok();
}

// ---------------------------------------------------------------------------
// Wczytanie modułu
// ---------------------------------------------------------------------------

Status WasmEngine::load(const void* image, size_t bytes, const char* name) {
    if (runtime_ == nullptr) return fail(Err::NotInitialized);
    if (image == nullptr || bytes == 0) {
        setError("pusty obraz modulu");
        return fail(Err::BadArgument);
    }

    IM3Module module = nullptr;
    M3Result  r = m3_ParseModule(static_cast<IM3Environment>(environment_), &module,
                                 static_cast<const u8*>(image), static_cast<uint32_t>(bytes));
    if (r != m3Err_none) {
        captureError(r);
        return fail(Err::BadArgument);
    }

    r = m3_LoadModule(static_cast<IM3Runtime>(runtime_), module);
    if (r != m3Err_none) {
        // Po nieudanym `m3_LoadModule` moduł nadal należy do nas.
        m3_FreeModule(module);
        captureError(r);
        return fail(Err::BadArgument);
    }

    module_ = module;
    m3_SetModuleName(module, name != nullptr ? name : "=wasm");

    auto linked = linkBindings();
    if (!linked) {
        setError("nie udalo sie zlinkowac importow");
        return linked;
    }

    // Kompilacja wszystkich funkcji od razu, zamiast leniwie przy pierwszym
    // wywołaniu. Tak brakujący import wychodzi przy wczytaniu — czyli tam,
    // gdzie `ScriptDelivery` jeszcze potrafi wycofać obraz — a nie po godzinie
    // pracy, gdy sterowanie pierwszy raz wejdzie w rzadką gałąź.
    r = m3_CompileModule(module);
    if (r != m3Err_none) {
        captureError(r);
        module_ = nullptr;
        return fail(Err::BadArgument);
    }

    error_[0] = '\0';
    return ok();
}

bool WasmEngine::hasFunction(const char* fn) const {
    if (runtime_ == nullptr || module_ == nullptr || fn == nullptr) return false;

    IM3Function found = nullptr;
    return m3_FindFunction(&found, static_cast<IM3Runtime>(runtime_), fn) == m3Err_none;
}

Status WasmEngine::call(const char* fn) {
    if (runtime_ == nullptr || module_ == nullptr) return fail(Err::NotInitialized);

    IM3Function found = nullptr;
    if (m3_FindFunction(&found, static_cast<IM3Runtime>(runtime_), fn) != m3Err_none) {
        return fail(Err::NotFound);
    }

    // Bez budżetu — tak samo, jak `Interp::callGlobal()`. Odpowiedzialność
    // za czas wykonania `setup()` spada na autora modułu.
    m3_SetFuel(static_cast<IM3Runtime>(runtime_), 0);

    M3Result r = m3_CallV(found);
    if (r != m3Err_none) {
        captureError(r);
        return fail(Err::Internal);
    }
    return ok();
}

// ---------------------------------------------------------------------------
// Wykonanie z budżetem
// ---------------------------------------------------------------------------

Status WasmEngine::jobBegin(const char* fn) {
    if (runtime_ == nullptr || module_ == nullptr) return fail(Err::NotInitialized);

    IM3Function found = nullptr;
    if (m3_FindFunction(&found, static_cast<IM3Runtime>(runtime_), fn) != m3Err_none) {
        state_ = JobState::Idle;
        return fail(Err::NotFound);
    }

    jobFn_ = found;
    steps_ = 0;
    state_ = JobState::Idle;
    return ok();
}

IScriptEngine::JobState WasmEngine::jobStep(u32 budget) {
    if (jobFn_ == nullptr) return JobState::Idle;

    auto* rt = static_cast<IM3Runtime>(runtime_);
    m3_SetFuel(rt, budget);

    M3Result r = m3_CallV(static_cast<IM3Function>(jobFn_));

    // Ile budżetu poszło. Przy budżecie zerowym licznik stoi, więc i tak zero.
    const u32 left = m3_GetFuel(rt);
    steps_ = (budget > left) ? (budget - left) : 0;

    if (r == m3Err_none) {
        state_ = JobState::Done;
    } else if (r == m3Err_trapOutOfFuel) {
        // Nie błąd, tylko koniec przydzielonego czasu. Wykonania nie da się
        // wznowić — patrz `JobState::Exhausted`.
        state_ = JobState::Exhausted;
    } else {
        captureError(r);
        state_ = JobState::Failed;
    }

    jobFn_ = nullptr;
    return state_;
}

void WasmEngine::jobCancel() {
    jobFn_ = nullptr;
    state_ = JobState::Idle;
}

// ---------------------------------------------------------------------------
// Błędy
// ---------------------------------------------------------------------------

void WasmEngine::setError(const char* text) {
    if (text == nullptr) { error_[0] = '\0'; return; }
    const size_t n = strlen(text) < sizeof(error_) - 1 ? strlen(text) : sizeof(error_) - 1;
    memcpy(error_, text, n);
    error_[n] = '\0';
}

void WasmEngine::captureError(const char* result) {
    // wasm3 trzyma osobno krótki kod błędu i kontekst (nazwa funkcji, plik).
    // Dla autora modułu liczy się jedno i drugie.
    M3ErrorInfo info{};
    if (runtime_ != nullptr) m3_GetErrorInfo(static_cast<IM3Runtime>(runtime_), &info);

    if (info.message != nullptr && info.message[0] != '\0' && info.message != result) {
        snprintf(error_, sizeof(error_), "%s: %s", result ? result : "blad", info.message);
    } else {
        setError(result);
    }
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_SCRIPT_ENGINE_WASM
