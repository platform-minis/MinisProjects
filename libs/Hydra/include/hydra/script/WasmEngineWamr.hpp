#pragma once
/**
 * Hydra — silnik skryptowy WebAssembly na WAMR (WebAssembly Micro Runtime).
 *
 * Drugi runtime WebAssembly, obok wasm3. Ten sam bajtkod, ta sama powierzchnia
 * importów, ta sama umowa `IScriptEngine` — inna maszyna pod spodem.
 *
 * ## Kiedy WAMR, a kiedy wasm3
 *
 * | Runtime | Sam runtime | Kiedy |
 * |---|---|---|
 * | wasm3 | ~64 kB | poniżej 256 kB RAM — RP2040/RP2350, STM32 |
 * | WAMR | 200 kB+ | powyżej; szybszy interpreter, wbudowany licznik instrukcji |
 *
 * Wybór robi `HYDRA_SCRIPT_WASM_ENGINE` w `Profile.hpp`. To jest decyzja
 * o zapasie pamięci, nie o preferencji: skrypt nie ma prawa zabrać pamięci
 * pętli sterowania, więc na małej płytce dostaje mniejszy runtime.
 *
 * ## Dwie różnice względem wasm3, obie na korzyść
 *
 * **1. Budżet liczy instrukcje, nie krawędzie pętli.** WAMR ma licznik
 * wbudowany (`wasm_runtime_set_instruction_count_limit`), więc nie trzeba było
 * łatać interpretera tak, jak przy wasm3. Jednostka budżetu jest tu ta sama,
 * co w Lua — instrukcja maszyny wirtualnej — więc ta sama liczba w konfiguracji
 * znaczy co innego niż przy wasm3. Kto przestawia silnik, powinien budżet
 * przeliczyć.
 *
 * **2. Alokacja idzie przez `script::Heap`** — tak samo jak przy wasm3, ale bez
 * łatania źródeł: WAMR przyjmuje trójkę funkcji alokatora w `RuntimeInitArgs`.
 *
 * Wspólne z wasm3 zostaje to, co wynika z samego WebAssembly: wykonania nie da
 * się wznowić po wyczerpaniu budżetu (`JobState::Exhausted`), a odbiór zdarzeń
 * jest eksportowaną funkcją `on_event`, nie wywołaniem zwrotnym.
 *
 * ## Biblioteka osobno
 *
 * Źródła WAMR nie leżą w Hydrze, tylko w `libs/HydraWamr` — osobnej bibliotece
 * PlatformIO. Powód i sposób osadzenia: `libs/HydraWamr/VENDOR.md`.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_WAMR

#include <string.h>

#include "hydra/script/IScriptEngine.hpp"

/** Stos wykonania modułu w bajtach — przestrzeń na ramki funkcji WebAssembly. */
#ifndef HYDRA_WAMR_STACK_BYTES
#  if HYDRA_SCRIPT_LARGE_PROFILE
#    define HYDRA_WAMR_STACK_BYTES (16 * 1024)
#  else
#    define HYDRA_WAMR_STACK_BYTES (8 * 1024)
#  endif
#endif

namespace hydra {
namespace script {

class WasmEngineWamr final : public IScriptEngine {
public:
    struct Config {
        u32 stackBytes = HYDRA_WAMR_STACK_BYTES;
        /**
         * Sterta modułu — pamięć, z której moduł alokuje przez `malloc` swojego
         * języka. Zero wystarcza modułom, które nie alokują; AssemblyScript
         * z `runtime: "stub"` do nich należy.
         */
        u32 moduleHeapBytes = 0;
    };

    WasmEngineWamr() = default;
    explicit WasmEngineWamr(const Config& cfg) : cfg_(cfg) {}

    void configure(const Config& cfg) { cfg_ = cfg; }

    // --- IScriptEngine -----------------------------------------------------

    const char* name() const override { return "wamr"; }

    Status open(void* pool, size_t poolBytes) override;
    void   close() override;
    bool   ready() const override { return instance_ != nullptr || moduleReady_; }

    Status installBindings(const BindingSet& set) override;
    void   removeBindings() override;
    u32    dispatchSignals(u32 maxSignals) override;

    Status load(const void* image, size_t bytes, const char* name) override;

    /** Przenośny bajtkod tak; AOT wymagałby budowy WAMR-a z `WASM_ENABLE_AOT`. */
    bool acceptsVariant(const char* variant) const override {
        return IScriptEngine::acceptsVariant(variant) || strcmp(variant, "wasm") == 0;
    }

    bool   hasFunction(const char* fn) const override;
    Status call(const char* fn) override;

    Status   jobBegin(const char* fn) override;
    JobState jobStep(u32 budget) override;
    JobState jobState() const override { return state_; }
    void     jobCancel() override;
    u32      jobSteps() const override { return steps_; }

    Heap::Stats memory() const override { return heap_.stats(); }
    /** WebAssembly nie ma odśmiecania — pamięć modułu jest jego pamięcią liniową. */
    u32         collect() override { return heap_.stats().used; }
    const char* error() const override { return error_; }

private:
    Status registerBindings();
    void   setError(const char* text);
    /** Zdejmuje wyjątek z instancji modułu i zapisuje go jako treść błędu. */
    void   captureException();
    void   releaseModule();

    void* module_   = nullptr;  ///< wasm_module_t
    void* instance_ = nullptr;  ///< wasm_module_inst_t
    void* execEnv_  = nullptr;  ///< wasm_exec_env_t
    void* jobFn_    = nullptr;  ///< wasm_function_inst_t

    /**
     * Kopia obrazu.
     *
     * WAMR nie kopiuje bajtów przy `wasm_runtime_load` — trzyma wskaźnik i pisze
     * po tym buforze w trakcie ładowania. Obraz z `ImageStore` musi zostać
     * nietknięty, bo to z niego robi się wycofanie, więc pracujemy na kopii
     * z naszej puli.
     */
    u8*    imageCopy_ = nullptr;
    size_t imageBytes_ = 0;

    Heap       heap_{};
    Config     cfg_{};
    BindingSet bindings_{};
    JobState   state_ = JobState::Idle;
    u32        steps_ = 0;
    bool       moduleReady_     = false;
    bool       ownsDefaultPool_ = false;
    bool       runtimeUp_       = false;
    /** Importy rejestruje się raz na runtime, przy pierwszym `load()`. */
    bool       bindingsRegistered_ = false;

    char error_[HYDRA_SCRIPT_ERROR_MAX] = {};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_SCRIPT_ENGINE_WAMR
