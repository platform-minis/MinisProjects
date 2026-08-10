/**
 * Silnik WebAssembly oparty na wasm3.
 *
 * Jedyny plik w bibliotece, który widzi `wasm3.h` — tak samo jak
 * `LuaInternal.hpp` jest jedynym, który widzi `lua.h`. Nagłówek publiczny
 * operuje na `void*`, więc typy wasm3 nie wyciekają do API i podmiana
 * interpretera na WAMR w etapie piątym nie przechodzi przez nagłówki.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/Wasm3Engine.hpp"

#include <string.h>

#include "hydra/core/Log.hpp"

// Alokator kierowany na pulę Hydry włącza hydra_m3_conf.h, wciągany przez
// m3_config.h — patrz łatki w tools/vendor_wasm3.sh.
#include "wasm3.h"

HYDRA_LOG_MODULE("script.wasm")

namespace hydra {
namespace script {

namespace {

/**
 * Pula obsługująca alokacje wasm3.
 *
 * Wskaźnik globalny, a nie pole silnika, bo wasm3 jest biblioteką w C i woła
 * `m3_Malloc_Impl` bez żadnego kontekstu. Konsekwencja jest jedna i trzeba ją
 * znać: **naraz może istnieć jeden otwarty `Wasm3Engine`**. Drugi zgłasza
 * `Err::Busy` przy `open()`, zamiast po cichu alokować z cudzej puli.
 *
 * Ograniczenie nie jest dotkliwe — urządzenie uruchamia jeden program — a jego
 * zniesienie wymagałoby przebudowy alokatora wasm3 na wariant z kontekstem,
 * czyli łatki znacznie większej niż trzy funkcje.
 */
Heap* gActiveHeap = nullptr;

/**
 * Nagłówki bloków.
 *
 * `Heap::reallocate()` chce znać stary rozmiar, a wasm3 przy `m3_Free_Impl`
 * go nie podaje. Trzymamy więc rozmiar tuż przed ładunkiem. Ośmiobajtowy
 * narzut zachowuje wyrównanie, którego oczekują struktury interpretera.
 */
constexpr size_t kBlockHeader = 8;

}  // namespace

}  // namespace script
}  // namespace hydra

// ---------------------------------------------------------------------------
// Alokator dla wasm3 — wiązanie w C, bo woła go kod w C
// ---------------------------------------------------------------------------

extern "C" {

void* hydra_wasm3_malloc(size_t size) {
    using namespace hydra;
    using namespace hydra::script;

    if (gActiveHeap == nullptr) return nullptr;

    void* raw = gActiveHeap->allocate(size + kBlockHeader);
    if (raw == nullptr) return nullptr;

    *static_cast<size_t*>(raw) = size;
    u8* payload = static_cast<u8*>(raw) + kBlockHeader;

    // wasm3 zakłada pamięć wyzerowaną — jego `m3_Malloc` w oryginale woła
    // `calloc`. Bez tego pola struktur zaczynają od śmieci i interpreter
    // wywraca się w miejscu niezwiązanym z przyczyną.
    memset(payload, 0, size);
    return payload;
}

void hydra_wasm3_free(void* ptr) {
    using namespace hydra::script;

    if (ptr == nullptr || gActiveHeap == nullptr) return;
    gActiveHeap->release(static_cast<hydra::u8*>(ptr) - kBlockHeader);
}

void* hydra_wasm3_realloc(void* ptr, size_t newSize, size_t oldSize) {
    using namespace hydra;
    using namespace hydra::script;

    if (gActiveHeap == nullptr) return nullptr;
    if (ptr == nullptr) return hydra_wasm3_malloc(newSize);

    u8* raw = static_cast<u8*>(ptr) - kBlockHeader;
    // Rozmiar z nagłówka, nie z argumentu: wasm3 podaje `oldSize` rzetelnie,
    // ale własny zapis jest jedynym, któremu można ufać przy zwalnianiu.
    const size_t stored = *reinterpret_cast<size_t*>(raw);
    (void)oldSize;

    void* moved = gActiveHeap->reallocate(raw, stored + kBlockHeader, newSize + kBlockHeader);
    if (moved == nullptr) return nullptr;

    *static_cast<size_t*>(moved) = newSize;
    u8* payload = static_cast<u8*>(moved) + kBlockHeader;

    if (newSize > stored) memset(payload + stored, 0, newSize - stored);
    return payload;
}

}  // extern "C"

namespace hydra {
namespace script {

namespace {

/** Statyczna pula domyślna — ta sama zasada co przy interpreterze Lua. */
alignas(8) u8 gDefaultPool[HYDRA_SCRIPT_HEAP_BYTES];
bool gDefaultPoolTaken = false;

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  Cykl życia
// ═══════════════════════════════════════════════════════════════════════════

Wasm3Engine::~Wasm3Engine() {
    close();
}

EngineInfo Wasm3Engine::info() const {
    EngineInfo out;
    out.name     = "wasm3";
    out.language = ScriptLanguage::Wasm;
    // Bez pułapki licznikowej: wywołanie idzie do końca. Powód w nagłówku klasy.
    out.preemption    = Preemption::RunToCompletion;
    out.acceptsSource = false;
    out.acceptsBinary = true;
    return out;
}

void Wasm3Engine::setError(const char* text) {
    if (text == nullptr) text = "nieznany blad";
    strncpy(error_, text, sizeof(error_) - 1);
    error_[sizeof(error_) - 1] = '\0';
}

Status Wasm3Engine::open() {
    if (runtime_ != nullptr) return fail(Err::AlreadyExists);

    // Jeden silnik naraz — powód przy `gActiveHeap`.
    if (gActiveHeap != nullptr) {
        setError("inny silnik wasm3 jest juz otwarty");
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

    HYDRA_CHECK(heap_.init(pool, bytes));
    gActiveHeap = &heap_;

    environment_ = m3_NewEnvironment();
    if (environment_ == nullptr) {
        setError("brak pamieci na srodowisko wasm3");
        close();
        return fail(Err::OutOfMemory);
    }

    runtime_ = m3_NewRuntime(static_cast<IM3Environment>(environment_), cfg_.stackBytes, nullptr);
    if (runtime_ == nullptr) {
        setError("brak pamieci na maszyne wirtualna");
        close();
        return fail(Err::OutOfMemory);
    }

    clearError();
    return ok();
}

void Wasm3Engine::close() {
    pendingFn_ = nullptr;
    jobState_  = RunState::Idle;
    steps_     = 0;

    // Kolejność ma znaczenie: runtime trzyma moduł, środowisko trzyma runtime.
    // Zwolnienie środowiska jako pierwszego zostawiłoby wiszące wskaźniki
    // w strukturach, które wasm3 przejdzie przy sprzątaniu.
    if (runtime_ != nullptr) {
        m3_FreeRuntime(static_cast<IM3Runtime>(runtime_));
        runtime_ = nullptr;
    }
    // Moduł należy do runtime od chwili `m3_LoadModule` — zwalnia go wraz
    // z sobą. Sami zwalniamy go tylko wtedy, gdy ładowanie się nie udało.
    module_ = nullptr;

    if (environment_ != nullptr) {
        m3_FreeEnvironment(static_cast<IM3Environment>(environment_));
        environment_ = nullptr;
    }

    if (gActiveHeap == &heap_) gActiveHeap = nullptr;
    if (ownsDefaultPool_) {
        gDefaultPoolTaken = false;
        ownsDefaultPool_  = false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Ładowanie
// ═══════════════════════════════════════════════════════════════════════════

Status Wasm3Engine::loadBinary(CByteSpan image, const char* chunkName) {
    if (runtime_ == nullptr) return fail(Err::NotInitialized);
    if (image.empty()) return fail(Err::BadArgument);

    // Najtańsze sprawdzenie, jakie istnieje, a odsiewa najczęstszą pomyłkę:
    // wgrany plik `.lua`, obraz OTA albo ucięty transfer. Bez tego wasm3
    // zgłasza „unknown opcode" i szuka się błędu w module, a nie w tym,
    // co go dowiozło.
    static const u8 kMagic[4] = {0x00, 0x61, 0x73, 0x6D};   // "\0asm"
    if (image.size() < 8 || memcmp(image.data(), kMagic, sizeof(kMagic)) != 0) {
        setError("to nie jest modul .wasm (bledny naglowek)");
        return fail(Err::Protocol);
    }

    IM3Module parsed = nullptr;
    M3Result result = m3_ParseModule(static_cast<IM3Environment>(environment_), &parsed,
                                     image.data(), static_cast<u32>(image.size()));
    if (result != m3Err_none) {
        setError(result);
        return fail(Err::Protocol);
    }

    result = m3_LoadModule(static_cast<IM3Runtime>(runtime_), parsed);
    if (result != m3Err_none) {
        // Moduł nie przeszedł do runtime, więc jego zwolnienie należy do nas.
        m3_FreeModule(parsed);
        setError(result);
        return fail(Err::Protocol);
    }

    if (chunkName != nullptr) m3_SetModuleName(parsed, chunkName);
    module_ = parsed;

    clearError();
    return ok();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Wywołania
// ═══════════════════════════════════════════════════════════════════════════

bool Wasm3Engine::hasFunction(const char* name) const {
    if (runtime_ == nullptr || name == nullptr) return false;

    IM3Function fn = nullptr;
    return m3_FindFunction(&fn, static_cast<IM3Runtime>(runtime_), name) == m3Err_none &&
           fn != nullptr;
}

Status Wasm3Engine::callFunction(const char* name) {
    if (runtime_ == nullptr) return fail(Err::NotInitialized);

    IM3Function fn = nullptr;
    M3Result result = m3_FindFunction(&fn, static_cast<IM3Runtime>(runtime_), name);
    if (result != m3Err_none || fn == nullptr) {
        setError("brak funkcji w module");
        return fail(Err::NotFound);
    }

    result = m3_CallV(fn);
    if (result != m3Err_none) {
        setError(result);
        return fail(Err::Internal);
    }

    clearError();
    return ok();
}

Result<i32> Wasm3Engine::callInt(const char* name) {
    if (runtime_ == nullptr) return unexpected(Err::NotInitialized);

    IM3Function fn = nullptr;
    if (m3_FindFunction(&fn, static_cast<IM3Runtime>(runtime_), name) != m3Err_none ||
        fn == nullptr) {
        setError("brak funkcji w module");
        return unexpected(Err::NotFound);
    }

    const M3Result called = m3_CallV(fn);
    if (called != m3Err_none) {
        setError(called);
        return unexpected(Err::Internal);
    }

    int32_t value = 0;
    const void* results[] = {&value};
    if (m3_GetResults(fn, 1, results) != m3Err_none) {
        setError("funkcja nie zwraca wartosci calkowitej");
        return unexpected(Err::Protocol);
    }

    clearError();
    return static_cast<i32>(value);
}

Status Wasm3Engine::startJob(const char* name) {
    if (runtime_ == nullptr) return fail(Err::NotInitialized);

    IM3Function fn = nullptr;
    const M3Result result = m3_FindFunction(&fn, static_cast<IM3Runtime>(runtime_), name);
    if (result != m3Err_none || fn == nullptr) {
        setError("brak funkcji w module");
        return fail(Err::NotFound);
    }

    pendingFn_ = fn;
    jobState_  = RunState::Running;
    return ok();
}

/**
 * Wykonuje przygotowane wywołanie.
 *
 * Budżet jest **ignorowany** i to nie jest przeoczenie — wasm3 nie ma czym
 * przerwać wykonania w środku. Stan `Running` pojawia się tylko między
 * `startJob()` a tym wywołaniem, więc pętla wołającego „dopóki Running"
 * wykonuje dokładnie jeden obieg i kończy się tak samo jak przy Lua.
 */
RunState Wasm3Engine::resumeJob(u32 budget) {
    (void)budget;

    if (jobState_ != RunState::Running || pendingFn_ == nullptr) return jobState_;

    IM3Function fn = static_cast<IM3Function>(pendingFn_);
    pendingFn_ = nullptr;

    const M3Result result = m3_CallV(fn);
    ++steps_;

    if (result != m3Err_none) {
        setError(result);
        jobState_ = RunState::Failed;
        return jobState_;
    }

    jobState_ = RunState::Done;
    return jobState_;
}

void Wasm3Engine::cancelJob() {
    pendingFn_ = nullptr;
    jobState_  = RunState::Idle;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Diagnostyka
// ═══════════════════════════════════════════════════════════════════════════

u32 Wasm3Engine::linearMemoryBytes() const {
    if (runtime_ == nullptr) return 0;

    uint32_t size = 0;
    (void)m3_GetMemory(static_cast<IM3Runtime>(runtime_), &size, 0);
    return size;
}

ScriptMemory Wasm3Engine::memory() const {
    const Heap::Stats stats = heap_.stats();

    ScriptMemory out;
    out.capacityBytes = stats.capacity;
    out.usedBytes     = stats.used;
    out.peakBytes     = stats.peak;
    return out;
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
