/**
 * Silnik WebAssembly oparty na WAMR.
 *
 * Jedyny plik w bibliotece widzący nagłówki WAMR — ta sama zasada, co przy
 * `Wasm3Engine.cpp` i `LuaInternal.hpp`.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT && HYDRA_SCRIPT_HAS_WAMR

#include "hydra/script/WamrEngine.hpp"

#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/script/Script.hpp"   // HYDRA_SCRIPT_HEAP_BYTES

#include "wasm_export.h"

HYDRA_LOG_MODULE("script.wamr")

namespace hydra {
namespace script {

namespace {

/** Statyczna pula domyślna — ta sama zasada co przy Lua i wasm3. */
alignas(8) u8 gDefaultPool[HYDRA_SCRIPT_HEAP_BYTES];
bool gDefaultPoolTaken = false;

/**
 * Czy środowisko WAMR jest podniesione.
 *
 * `wasm_runtime_full_init()` ustawia stan globalny biblioteki, więc drugie
 * wywołanie bez `destroy` cicho nadpisałoby pulę pierwszego. Stąd znacznik
 * i `Err::Busy` zamiast pozwolenia na dwie instancje.
 */
bool gRuntimeUp = false;

}  // namespace

WamrEngine::~WamrEngine() {
    close();
}

EngineInfo WamrEngine::info() const {
    EngineInfo out;
    out.name     = "wamr";
    out.language = ScriptLanguage::Wasm;
    // Strażnik z innego wątku istnieje w API WAMR, ale nikt go tu nie
    // uruchamia — dopóki tak jest, deklarowanie `Watchdog` byłoby obietnicą
    // bez pokrycia.
    out.preemption    = Preemption::RunToCompletion;
    out.acceptsSource = false;
    out.acceptsBinary = true;
    return out;
}

void WamrEngine::setError(const char* text) {
    if (text == nullptr || text[0] == '\0') text = "nieznany blad";
    strncpy(error_, text, sizeof(error_) - 1);
    error_[sizeof(error_) - 1] = '\0';
}

void WamrEngine::captureException() {
    if (instance_ == nullptr) return;

    const char* text = wasm_runtime_get_exception(static_cast<wasm_module_inst_t>(instance_));
    if (text != nullptr) {
        setError(text);
        // Wyjątek zostaje na instancji do czasu wyczyszczenia i **blokuje
        // kolejne wywołania**. Bez tego jedna pułapka unieruchamiałaby moduł
        // do końca pracy urządzenia.
        wasm_runtime_clear_exception(static_cast<wasm_module_inst_t>(instance_));
    }
}

Status WamrEngine::open() {
    if (runtimeUp_) return fail(Err::AlreadyExists);
    if (gRuntimeUp) {
        setError("inny silnik WAMR jest juz otwarty");
        return fail(Err::Busy);
    }

    void*  pool  = cfg_.pool;
    size_t bytes = cfg_.poolBytes;

    if (pool == nullptr) {
        if (gDefaultPoolTaken) {
            setError("pula domyslna jest juz zajeta");
            return fail(Err::Busy);
        }
        pool  = gDefaultPool;
        bytes = sizeof(gDefaultPool);
        gDefaultPoolTaken = true;
        ownsDefaultPool_  = true;
    }

    RuntimeInitArgs args;
    memset(&args, 0, sizeof(args));

    // Tu jest cała różnica wobec wasm3: pula wchodzi konfiguracją, a nie łatką
    // alokatora. WAMR trzyma własny menedżer i od tej chwili nie tyka malloc().
    args.mem_alloc_type                 = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = pool;
    args.mem_alloc_option.pool.heap_size = static_cast<uint32_t>(bytes);

    if (!wasm_runtime_full_init(&args)) {
        setError("nie udalo sie podniesc srodowiska WAMR");
        if (ownsDefaultPool_) { gDefaultPoolTaken = false; ownsDefaultPool_ = false; }
        return fail(Err::OutOfMemory);
    }

    runtimeUp_ = true;
    gRuntimeUp = true;
    clearError();
    return ok();
}

void WamrEngine::close() {
    pendingFn_ = nullptr;
    jobState_  = RunState::Idle;
    steps_     = 0;

    // Odwrotnie do tworzenia: środowisko wykonania trzyma instancję,
    // instancja trzyma moduł.
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
    if (runtimeUp_) {
        wasm_runtime_destroy();
        runtimeUp_ = false;
        gRuntimeUp = false;
    }
    if (ownsDefaultPool_) {
        gDefaultPoolTaken = false;
        ownsDefaultPool_  = false;
    }
}

Status WamrEngine::loadBinary(CByteSpan image, const char* chunkName) {
    if (!runtimeUp_) return fail(Err::NotInitialized);
    if (image.empty()) return fail(Err::BadArgument);

    // Ten sam wczesny test, co w wasm3, i z tego samego powodu: odsiewa
    // wgrany plik `.lua`, obraz OTA albo ucięty transfer, zanim parser
    // zgłosi „unknown opcode" w miejscu bez związku z przyczyną.
    static const u8 kMagic[4] = {0x00, 0x61, 0x73, 0x6D};
    if (image.size() < 8 || memcmp(image.data(), kMagic, sizeof(kMagic)) != 0) {
        setError("to nie jest modul .wasm (bledny naglowek)");
        return fail(Err::Protocol);
    }

    char message[HYDRA_SCRIPT_ERROR_MAX];
    message[0] = '\0';

    // WAMR **nie kopiuje** bajtów — trzyma wskaźnik. Wymóg „obraz musi przeżyć
    // moduł" jest więc taki sam jak w wasm3 i tak samo opisany w loadModule().
    wasm_module_t loaded = wasm_runtime_load(
        const_cast<uint8_t*>(image.data()), static_cast<uint32_t>(image.size()),
        message, sizeof(message));
    if (loaded == nullptr) {
        setError(message);
        return fail(Err::Protocol);
    }

    wasm_module_inst_t inst = wasm_runtime_instantiate(
        loaded, cfg_.stackBytes, cfg_.heapBytes, message, sizeof(message));
    if (inst == nullptr) {
        wasm_runtime_unload(loaded);
        setError(message);
        return fail(Err::OutOfMemory);
    }

    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, cfg_.stackBytes);
    if (env == nullptr) {
        wasm_runtime_deinstantiate(inst);
        wasm_runtime_unload(loaded);
        setError("brak pamieci na srodowisko wykonania");
        return fail(Err::OutOfMemory);
    }

    module_   = loaded;
    instance_ = inst;
    execEnv_  = env;

    if (chunkName != nullptr) HYDRA_LOGI("wczytano modul %s", chunkName);
    clearError();
    return ok();
}

bool WamrEngine::hasFunction(const char* name) const {
    if (instance_ == nullptr || name == nullptr) return false;
    return wasm_runtime_lookup_function(static_cast<wasm_module_inst_t>(instance_), name)
           != nullptr;
}

Status WamrEngine::callFunction(const char* name) {
    if (instance_ == nullptr) return fail(Err::NotInitialized);

    wasm_function_inst_t fn =
        wasm_runtime_lookup_function(static_cast<wasm_module_inst_t>(instance_), name);
    if (fn == nullptr) {
        setError("brak funkcji w module");
        return fail(Err::NotFound);
    }

    // Bufor argumentów, choć funkcja nie bierze żadnych.
    //
    // `wasm_runtime_call_wasm` używa tej samej tablicy na wejście i na wynik,
    // więc `nullptr` przy zerowej liczbie argumentów nie jest bezpieczny —
    // WAMR i tak w nią zagląda. Objawem był SIGSEGV przy pierwszym wywołaniu.
    uint32_t argv[4] = {0, 0, 0, 0};

    if (!wasm_runtime_call_wasm(static_cast<wasm_exec_env_t>(execEnv_), fn, 0, argv)) {
        captureException();
        return fail(Err::Internal);
    }

    clearError();
    return ok();
}

Status WamrEngine::startJob(const char* name) {
    if (instance_ == nullptr) return fail(Err::NotInitialized);

    wasm_function_inst_t fn =
        wasm_runtime_lookup_function(static_cast<wasm_module_inst_t>(instance_), name);
    if (fn == nullptr) {
        setError("brak funkcji w module");
        return fail(Err::NotFound);
    }

    pendingFn_ = fn;
    jobState_  = RunState::Running;
    return ok();
}

RunState WamrEngine::resumeJob(u32 budget) {
    (void)budget;   // brak wywłaszczania — patrz nagłówek klasy

    if (jobState_ != RunState::Running || pendingFn_ == nullptr) return jobState_;

    wasm_function_inst_t fn = static_cast<wasm_function_inst_t>(pendingFn_);
    pendingFn_ = nullptr;
    ++steps_;

    // Bufor argumentów, choć funkcja nie bierze żadnych.
    //
    // `wasm_runtime_call_wasm` używa tej samej tablicy na wejście i na wynik,
    // więc `nullptr` przy zerowej liczbie argumentów nie jest bezpieczny —
    // WAMR i tak w nią zagląda. Objawem był SIGSEGV przy pierwszym wywołaniu.
    uint32_t argv[4] = {0, 0, 0, 0};

    if (!wasm_runtime_call_wasm(static_cast<wasm_exec_env_t>(execEnv_), fn, 0, argv)) {
        captureException();
        jobState_ = RunState::Failed;
        return jobState_;
    }

    jobState_ = RunState::Done;
    return jobState_;
}

void WamrEngine::cancelJob() {
    pendingFn_ = nullptr;
    jobState_  = RunState::Idle;
}

ScriptMemory WamrEngine::memory() const {
    ScriptMemory out;
    if (instance_ == nullptr) return out;

    // WAMR liczy zużycie sterty **modułu**, nie całej puli. To jest ta liczba,
    // która interesuje autora programu: ile z przydzielonego mu miejsca zajął.
    // `mem_alloc_info_t` opisuje pulę **całego środowiska**, bo taką WAMR
    // dostał w `full_init`. Nie ma tu odpowiednika statystyk sterty modułu,
    // więc `peak` zostaje zerem zamiast zmyślonej wartości.
    mem_alloc_info_t info;
    memset(&info, 0, sizeof(info));
    if (wasm_runtime_get_mem_alloc_info(&info)) {
        out.capacityBytes = static_cast<u32>(info.total_size);
        out.usedBytes     = static_cast<u32>(info.total_size - info.total_free_size);
        out.peakBytes     = static_cast<u32>(info.highmark_size);
    }
    return out;
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT && HYDRA_SCRIPT_HAS_WAMR
