/**
 * Hydra — silnik skryptowy WebAssembly na WAMR.
 *
 * Trzy rzeczy dzieją się tu naprawdę: przekierowanie alokacji WAMR-a na pulę
 * Hydry, rejestracja importów z `BindingSet` i przekład wyniku wykonania na
 * `JobState`. Treść samych importów siedzi w `WasmApi.hpp` — wspólna z wasm3,
 * żeby ten sam bajtkod nie robił na dwóch płytkach dwóch różnych rzeczy.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_WAMR

#include "hydra/script/WasmEngineWamr.hpp"

#include <stdio.h>
#include <string.h>

#include "hydra/core/Log.hpp"

#include "SignalQueue.hpp"
#include "WasmApi.hpp"

extern "C" {
#include "wasm_export.h"
}

HYDRA_LOG_MODULE("wamr")

namespace hydra {
namespace script {

namespace {

/** Pula domyślna silnika WAMR. Rozmiar ustala `Profile.hpp`. */
alignas(8) u8 gDefaultWamrPool[HYDRA_WASM_HEAP_BYTES];
bool gDefaultWamrPoolTaken = false;

/**
 * Pula, z której WAMR bierze pamięć.
 *
 * Wskaźnik statyczny, bo `RuntimeInitArgs` przyjmuje trójkę funkcji bez
 * kontekstu — nie ma gdzie przekazać sterty. To samo ograniczenie, co przy
 * wasm3: **jedna pula naraz**, czyli jeden otwarty silnik WAMR.
 */
Heap* gWamrHeap = nullptr;

/**
 * Nagłówek przydziału.
 *
 * WAMR-owe `realloc_func` dostaje wyłącznie nowy rozmiar, a `Heap::reallocate`
 * potrzebuje starego, żeby wiedzieć, ile bajtów przenieść. Zamiast zgadywać —
 * co przy powiększaniu znaczyłoby czytanie poza starym blokiem — trzymamy
 * rozmiar przed ładunkiem.
 */
constexpr size_t kAllocHeader = 8;

void* wamrMalloc(unsigned int size) {
    if (gWamrHeap == nullptr || size == 0) return nullptr;

    auto* raw = static_cast<u8*>(gWamrHeap->allocate(size + kAllocHeader));
    if (raw == nullptr) return nullptr;

    *reinterpret_cast<u64*>(raw) = size;
    return raw + kAllocHeader;
}

void wamrFree(void* ptr) {
    if (gWamrHeap == nullptr || ptr == nullptr) return;
    gWamrHeap->release(static_cast<u8*>(ptr) - kAllocHeader);
}

void* wamrRealloc(void* ptr, unsigned int size) {
    if (gWamrHeap == nullptr) return nullptr;
    if (ptr == nullptr) return wamrMalloc(size);
    if (size == 0) { wamrFree(ptr); return nullptr; }

    auto*        raw     = static_cast<u8*>(ptr) - kAllocHeader;
    const size_t oldSize = static_cast<size_t>(*reinterpret_cast<u64*>(raw));

    auto* fresh = static_cast<u8*>(
        gWamrHeap->reallocate(raw, oldSize + kAllocHeader, size + kAllocHeader));
    if (fresh == nullptr) return nullptr;

    *reinterpret_cast<u64*>(fresh) = size;
    return fresh + kAllocHeader;
}

// ---------------------------------------------------------------------------
// Importy — przejściówki WAMR
// ---------------------------------------------------------------------------
//
// WAMR podaje argumenty jako zwykłe argumenty funkcji C, więc przejściówki są
// krótsze niż w wasm3. Wskaźniki przychodzą jako offsety w pamięci liniowej
// modułu i trzeba je sprawdzić — moduł mógł je policzyć źle albo złośliwie.

/** Sprawdza offset i zamienia go na wskaźnik hosta. Null, gdy poza zakresem. */
void* appPtr(wasm_exec_env_t env, u32 offset, u32 length) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
    if (!wasm_runtime_validate_app_addr(inst, offset, length)) return nullptr;
    return wasm_runtime_addr_app_to_native(inst, offset);
}

u32 wamr_wasmMillis(wasm_exec_env_t) { return wasmapi::millis(); }
u32 wamr_wasmMicros(wasm_exec_env_t) { return wasmapi::micros(); }
void wamr_wasmDelay(wasm_exec_env_t, u32 ms) { wasmapi::delay(ms); }

void wamr_wasmLog(wasm_exec_env_t env, u32 level, u32 ptr, u32 len) {
    auto* text = static_cast<const char*>(appPtr(env, ptr, len));
    if (text == nullptr) return;
    wasmapi::logText(level, text, len);
}

u32 wamr_wasmGpioMode(wasm_exec_env_t, u32 pin, u32 mode) { return wasmapi::gpioMode(pin, mode); }
u32 wamr_wasmGpioWrite(wasm_exec_env_t, u32 pin, u32 value) { return wasmapi::gpioWrite(pin, value); }
i32 wamr_wasmGpioRead(wasm_exec_env_t, u32 pin) { return wasmapi::gpioRead(pin); }
u32 wamr_wasmGpioToggle(wasm_exec_env_t, u32 pin) { return wasmapi::gpioToggle(pin); }

i32 wamr_wasmAdcRaw(wasm_exec_env_t, u32 pin) { return wasmapi::adcRaw(pin); }
i32 wamr_wasmAdcMv(wasm_exec_env_t, u32 pin) { return wasmapi::adcMv(pin); }

u32 wamr_wasmPwmSetup(wasm_exec_env_t, u32 pin, u32 hz) { return wasmapi::pwmSetup(pin, hz); }
u32 wamr_wasmPwmDuty(wasm_exec_env_t, u32 pin, u32 pm) { return wasmapi::pwmDuty(pin, pm); }
u32 wamr_wasmPwmUs(wasm_exec_env_t, u32 pin, u32 us) { return wasmapi::pwmUs(pin, us); }
u32 wamr_wasmPwmRelease(wasm_exec_env_t, u32 pin) { return wasmapi::pwmRelease(pin); }

void wamr_wasmEventEmit(wasm_exec_env_t env, u32 ptr, u32 len, float value, i32 data) {
    auto* name = static_cast<const char*>(appPtr(env, ptr, len));
    if (name == nullptr) return;
    wasmapi::eventEmit(name, len, value, data);
}

u32 wamr_wasmEventNameId(wasm_exec_env_t env, u32 ptr, u32 len) {
    auto* name = static_cast<const char*>(appPtr(env, ptr, len));
    if (name == nullptr) return 0;
    return wasmapi::nameIdOfSpan(name, len);
}

u32 wamr_wasmI2cPing(wasm_exec_env_t, u32 bus, u32 addr) { return wasmapi::i2cPing(bus, addr); }

i32 wamr_wasmI2cScan(wasm_exec_env_t env, u32 bus, u32 outPtr, u32 capacity) {
    auto* out = static_cast<u8*>(appPtr(env, outPtr, capacity));
    if (out == nullptr) return -1;
    return wasmapi::i2cScan(bus, out, capacity);
}

i32 wamr_wasmI2cRead(wasm_exec_env_t env, u32 bus, u32 addr, u32 reg, u32 outPtr, u32 len) {
    auto* out = static_cast<u8*>(appPtr(env, outPtr, len));
    if (out == nullptr) return -1;
    return wasmapi::i2cRead(bus, addr, reg, out, len);
}

i32 wamr_wasmI2cWrite(wasm_exec_env_t env, u32 bus, u32 addr, u32 reg, u32 dataPtr, u32 len) {
    auto* data = static_cast<const u8*>(appPtr(env, dataPtr, len));
    if (data == nullptr) return -1;
    return wasmapi::i2cWrite(bus, addr, reg, data, len);
}

#include "wamr_imports.inc"

/** Nazwa modułu, w którym leżą wszystkie importy Hydry. */
constexpr const char* kImportModule = "hydra";

/** Budżet jednego wywołania `on_event`. Jak przy wasm3 i z tego samego powodu. */
constexpr u32 kEventHandlerFuel = 10000;

}  // namespace

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status WasmEngineWamr::open(void* pool, size_t poolBytes) {
    if (runtimeUp_) return fail(Err::Busy);

    if (pool == nullptr || poolBytes == 0) {
        if (gDefaultWamrPoolTaken) return fail(Err::Busy);
        pool             = gDefaultWamrPool;
        poolBytes        = sizeof(gDefaultWamrPool);
        ownsDefaultPool_ = true;
    }

    HYDRA_CHECK(heap_.init(pool, poolBytes));
    if (ownsDefaultPool_) gDefaultWamrPoolTaken = true;
    gWamrHeap = &heap_;

    RuntimeInitArgs args;
    memset(&args, 0, sizeof(args));
    // Alokator własny, a nie pula WAMR-a: chcemy tę samą `Heap`, co reszta
    // warstwy skryptowej, razem z jej licznikami zużycia.
    args.mem_alloc_type                          = Alloc_With_Allocator;
    args.mem_alloc_option.allocator.malloc_func  = reinterpret_cast<void*>(wamrMalloc);
    args.mem_alloc_option.allocator.realloc_func = reinterpret_cast<void*>(wamrRealloc);
    args.mem_alloc_option.allocator.free_func    = reinterpret_cast<void*>(wamrFree);

    if (!wasm_runtime_full_init(&args)) {
        setError("nie udalo sie zainicjowac runtime WAMR");
        gWamrHeap = nullptr;
        heap_.reset();
        if (ownsDefaultPool_) { gDefaultWamrPoolTaken = false; ownsDefaultPool_ = false; }
        return fail(Err::OutOfMemory);
    }

    runtimeUp_          = true;
    bindingsRegistered_ = false;
    error_[0]           = '\0';
    return ok();
}

void WasmEngineWamr::releaseModule() {
    jobCancel();

    if (execEnv_ != nullptr) {
        wasm_runtime_destroy_exec_env(static_cast<wasm_exec_env_t>(execEnv_));
        execEnv_ = nullptr;
    }
    if (instance_ != nullptr) {
        wasm_runtime_deinstantiate(static_cast<wasm_module_inst_t>(instance_));
        instance_ = nullptr;
    }
    if (module_ != nullptr) {
        wasm_runtime_unload(static_cast<wasm_module_t>(module_));
        module_ = nullptr;
    }
    if (imageCopy_ != nullptr) {
        heap_.release(imageCopy_);
        imageCopy_  = nullptr;
        imageBytes_ = 0;
    }
    moduleReady_ = false;
}

void WasmEngineWamr::close() {
    releaseModule();

    if (runtimeUp_) {
        wasm_runtime_destroy();
        runtimeUp_ = false;
    }

    gWamrHeap = nullptr;
    heap_.reset();

    if (ownsDefaultPool_) {
        gDefaultWamrPoolTaken = false;
        ownsDefaultPool_      = false;
    }
}

// ---------------------------------------------------------------------------
// Bindingi
// ---------------------------------------------------------------------------

Status WasmEngineWamr::installBindings(const BindingSet& set) {
    bindings_ = set;
    // Sama rejestracja idzie przy `load()` — tak samo jak linkowanie w wasm3.
    // Tutaj zapamiętujemy wybór i podpinamy magistralę, żeby sygnał opublikowany
    // przed wczytaniem modułu trafił do pierścienia, a nie przepadł.
    if (set.event) HYDRA_CHECK(detail::signalQueueSubscribe());
    return ok();
}

void WasmEngineWamr::removeBindings() { detail::signalQueueRelease(); }

Status WasmEngineWamr::registerBindings() {
    if (!runtimeUp_) return fail(Err::NotInitialized);
    if (bindingsRegistered_) return ok();
    bindingsRegistered_ = true;

    // Rejestracja jest globalna dla runtime'u, a nie związana z modułem —
    // inaczej niż w wasm3, gdzie importy wiąże się z konkretnym modułem.
    for (const WamrImportGroup& g : kWamrImportGroups) {
        if (!(bindings_.*(g.flag))) continue;
        if (!wasm_runtime_register_natives(kImportModule, g.imports, g.count)) {
            setError("nie udalo sie zarejestrowac importow");
            return fail(Err::Internal);
        }
    }
    return ok();
}

u32 WasmEngineWamr::dispatchSignals(u32 maxSignals) {
    if (instance_ == nullptr || !bindings_.event) return 0;

    auto* inst    = static_cast<wasm_module_inst_t>(instance_);
    auto* env     = static_cast<wasm_exec_env_t>(execEnv_);
    auto  handler = wasm_runtime_lookup_function(inst, "on_event");

    if (handler == nullptr || env == nullptr) {
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
        wasm_runtime_set_instruction_count_limit(env, static_cast<int>(kEventHandlerFuel));

        // Argumenty WAMR-a idą przez tablicę słów 32-bitowych; f32 wchodzi
        // do niej swoją reprezentacją bitową, a nie po konwersji.
        u32 argv[3] = {};
        argv[0] = signal.nameId;
        memcpy(&argv[1], &signal.value, sizeof(float));
        argv[2] = static_cast<u32>(signal.data);

        if (!wasm_runtime_call_wasm(env, handler, 3, argv)) {
            // Błąd handlera nie może zatrzymać pozostałych — tak samo, jak
            // `lua_pcall` w wariancie dla Lua.
            captureException();
            HYDRA_LOGW("on_event(%u): %s", static_cast<u32>(signal.nameId), error_);
            wasm_runtime_clear_exception(inst);
        }
        ++handled;
    }
    return handled;
}

// ---------------------------------------------------------------------------
// Wczytanie modułu
// ---------------------------------------------------------------------------

Status WasmEngineWamr::load(const void* image, size_t bytes, const char* name) {
    if (!runtimeUp_) return fail(Err::NotInitialized);
    if (image == nullptr || bytes == 0) {
        setError("pusty obraz modulu");
        return fail(Err::BadArgument);
    }

    releaseModule();
    HYDRA_CHECK(registerBindings());

    // WAMR nie kopiuje bajtów — trzyma wskaźnik i pisze po buforze w trakcie
    // ładowania. Obraz z magazynu musi zostać nietknięty, bo to z niego robi
    // się wycofanie.
    imageCopy_ = static_cast<u8*>(heap_.allocate(bytes));
    if (imageCopy_ == nullptr) {
        setError("brak miejsca w puli na kopie obrazu");
        return fail(Err::OutOfMemory);
    }
    memcpy(imageCopy_, image, bytes);
    imageBytes_ = bytes;

    char err[128] = {};
    module_ = wasm_runtime_load(imageCopy_, static_cast<u32>(bytes), err, sizeof(err));
    if (module_ == nullptr) {
        setError(err[0] != '\0' ? err : "obraz nie jest modulem WebAssembly");
        releaseModule();
        return fail(Err::BadArgument);
    }

    instance_ = wasm_runtime_instantiate(static_cast<wasm_module_t>(module_),
                                         cfg_.stackBytes, cfg_.moduleHeapBytes,
                                         err, sizeof(err));
    if (instance_ == nullptr) {
        // Tu wychodzi między innymi brakujący import: WAMR rozwiązuje je przy
        // tworzeniu instancji, czyli w tym samym miejscu, w którym wasm3 robi
        // to przez `m3_CompileModule`. Moduł żądający niewystawionej grupy nie
        // wczyta się z jasnym błędem, zamiast działać połowicznie.
        setError(err[0] != '\0' ? err : "nie udalo sie utworzyc instancji modulu");
        releaseModule();
        return fail(Err::BadArgument);
    }

    // WAMR toleruje nierozwiązany import: ostrzega przy tworzeniu instancji
    // i wywraca się dopiero przy pierwszym wywołaniu. wasm3 odmawia od razu.
    // Domykamy tę różnicę, zamiast ją dokumentować — moduł, który nie ma
    // kompletu importów, ma się nie wczytać na obu silnikach tak samo,
    // a nie zawieść w rzadkiej gałęzi po godzinie pracy.
    const i32 imports = wasm_runtime_get_import_count(static_cast<wasm_module_t>(module_));
    for (i32 i = 0; i < imports; ++i) {
        wasm_import_t info{};
        wasm_runtime_get_import_type(static_cast<wasm_module_t>(module_), i, &info);
        if (info.linked) continue;

        snprintf(error_, sizeof(error_), "brak importu: %s.%s",
                 info.module_name != nullptr ? info.module_name : "?",
                 info.name != nullptr ? info.name : "?");
        releaseModule();
        return fail(Err::BadArgument);
    }

    execEnv_ = wasm_runtime_create_exec_env(static_cast<wasm_module_inst_t>(instance_),
                                            cfg_.stackBytes);
    if (execEnv_ == nullptr) {
        setError("brak pamieci na srodowisko wykonania");
        releaseModule();
        return fail(Err::OutOfMemory);
    }

    (void)name;
    moduleReady_ = true;
    error_[0]    = '\0';
    return ok();
}

bool WasmEngineWamr::hasFunction(const char* fn) const {
    if (instance_ == nullptr || fn == nullptr) return false;
    return wasm_runtime_lookup_function(static_cast<wasm_module_inst_t>(instance_), fn) != nullptr;
}

Status WasmEngineWamr::call(const char* fn) {
    if (instance_ == nullptr || execEnv_ == nullptr) return fail(Err::NotInitialized);

    auto found = wasm_runtime_lookup_function(static_cast<wasm_module_inst_t>(instance_), fn);
    if (found == nullptr) return fail(Err::NotFound);

    // Bez budżetu — tak samo, jak `Interp::callGlobal()`. Odpowiedzialność za
    // czas wykonania `setup()` spada na autora modułu.
    wasm_runtime_set_instruction_count_limit(static_cast<wasm_exec_env_t>(execEnv_), -1);

    if (!wasm_runtime_call_wasm(static_cast<wasm_exec_env_t>(execEnv_), found, 0, nullptr)) {
        captureException();
        wasm_runtime_clear_exception(static_cast<wasm_module_inst_t>(instance_));
        return fail(Err::Internal);
    }
    return ok();
}

// ---------------------------------------------------------------------------
// Wykonanie z budżetem
// ---------------------------------------------------------------------------

Status WasmEngineWamr::jobBegin(const char* fn) {
    if (instance_ == nullptr || execEnv_ == nullptr) return fail(Err::NotInitialized);

    auto found = wasm_runtime_lookup_function(static_cast<wasm_module_inst_t>(instance_), fn);
    if (found == nullptr) {
        state_ = JobState::Idle;
        return fail(Err::NotFound);
    }

    jobFn_ = found;
    steps_ = 0;
    state_ = JobState::Idle;
    return ok();
}

IScriptEngine::JobState WasmEngineWamr::jobStep(u32 budget) {
    if (jobFn_ == nullptr) return JobState::Idle;

    auto* env  = static_cast<wasm_exec_env_t>(execEnv_);
    auto* inst = static_cast<wasm_module_inst_t>(instance_);

    // -1 znosi ograniczenie; zero znaczyłoby „nie wykonuj ani jednej".
    wasm_runtime_set_instruction_count_limit(env, budget == 0 ? -1 : static_cast<int>(budget));

    const bool okCall = wasm_runtime_call_wasm(
        env, static_cast<wasm_function_inst_t>(jobFn_), 0, nullptr);

    steps_ = budget;   // WAMR nie oddaje reszty licznika — zużycie znamy z góry

    if (okCall) {
        state_ = JobState::Done;
    } else {
        const char* ex = wasm_runtime_get_exception(inst);
        // Wyczerpanie budżetu nie jest błędem, tylko końcem przydzielonego
        // czasu. WAMR zgłasza je zwykłym wyjątkiem, więc rozpoznajemy je po
        // treści — innego kanału nie ma.
        if (ex != nullptr && strstr(ex, "instruction limit exceeded") != nullptr) {
            state_ = JobState::Exhausted;
        } else {
            captureException();
            state_ = JobState::Failed;
        }
        wasm_runtime_clear_exception(inst);
    }

    jobFn_ = nullptr;
    return state_;
}

void WasmEngineWamr::jobCancel() {
    jobFn_ = nullptr;
    state_ = JobState::Idle;
}

// ---------------------------------------------------------------------------
// Błędy
// ---------------------------------------------------------------------------

void WasmEngineWamr::setError(const char* text) {
    if (text == nullptr) { error_[0] = '\0'; return; }
    const size_t n = strlen(text) < sizeof(error_) - 1 ? strlen(text) : sizeof(error_) - 1;
    memcpy(error_, text, n);
    error_[n] = '\0';
}

void WasmEngineWamr::captureException() {
    if (instance_ == nullptr) return;
    setError(wasm_runtime_get_exception(static_cast<wasm_module_inst_t>(instance_)));
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_SCRIPT_ENGINE_WAMR
