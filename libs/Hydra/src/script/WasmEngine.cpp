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

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/script/WasmAlloc.h"

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

/** Jeden import: nazwa, sygnatura wasm3 i funkcja. */
struct Import {
    const char*      name;
    const char*      signature;
    M3RawCall        fn;
};

const Import kCoreImports[] = {
    {"millis", "i()",  wasmMillis},
    {"micros", "i()",  wasmMicros},
    {"delay",  "v(i)", wasmDelay},
};

const Import kLogImports[] = {
    {"log", "v(iii)", wasmLog},
};

const Import kGpioImports[] = {
    {"gpio_mode",   "i(ii)", wasmGpioMode},
    {"gpio_write",  "i(ii)", wasmGpioWrite},
    {"gpio_read",   "i(i)",  wasmGpioRead},
    {"gpio_toggle", "i(i)",  wasmGpioToggle},
};

const Import kAdcImports[] = {
    {"adc_raw", "i(i)", wasmAdcRaw},
    {"adc_mv",  "i(i)", wasmAdcMv},
};

const Import kPwmImports[] = {
    {"pwm_setup",   "i(ii)", wasmPwmSetup},
    {"pwm_duty",    "i(ii)", wasmPwmDuty},
    {"pwm_us",      "i(ii)", wasmPwmUs},
    {"pwm_release", "i(i)",  wasmPwmRelease},
};

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
    return ok();
}

u32 WasmEngine::dispatchSignals(u32 maxSignals) {
    (void)maxSignals;
    return 0;
}

Status WasmEngine::linkBindings() {
    auto* module = static_cast<IM3Module>(module_);
    if (module == nullptr) return fail(Err::NotInitialized);

    if (bindings_.core) HYDRA_CHECK(linkGroup(module, kCoreImports, 3));
    if (bindings_.log)  HYDRA_CHECK(linkGroup(module, kLogImports, 1));
    if (bindings_.gpio) HYDRA_CHECK(linkGroup(module, kGpioImports, 4));
    if (bindings_.adc)  HYDRA_CHECK(linkGroup(module, kAdcImports, 2));
    if (bindings_.pwm)  HYDRA_CHECK(linkGroup(module, kPwmImports, 4));

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
