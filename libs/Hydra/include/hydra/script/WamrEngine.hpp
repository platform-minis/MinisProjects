#pragma once
/**
 * @file WamrEngine.hpp
 * @brief WebAssembly przez WAMR — trzeci silnik skryptowy Hydry.
 *
 * Cięższy brat `Wasm3Engine`: około 220 KB kodu wobec ~100 KB, w zamian
 * szybszy interpreter i droga do AOT. Który wybrać, rozstrzyga
 * `EngineSelector` na podstawie dostępnej pamięci — patrz `EngineSelector.hpp`.
 *
 * ## Pamięć: inaczej niż w wasm3
 *
 * wasm3 alokował przez trzy funkcje, które podmieniliśmy łatką. WAMR ma
 * **własny menedżer puli** (`mem-alloc/ems`), więc nie podmienia się niczego:
 * podaje mu się bufor przy `wasm_runtime_full_init()` i od tej chwili wszystko
 * — struktury interpretera, kod modułu, jego pamięć liniowa — idzie stamtąd.
 *
 * Konsekwencja jest ta sama co w wasm3 i wynika z tego samego: WAMR trzyma
 * stan globalnie, więc **naraz może istnieć jeden otwarty `WamrEngine`**.
 *
 * ## Trzy byty zamiast jednego
 *
 * wasm3 miał środowisko i maszynę. WAMR rozdziela to na:
 *
 *   moduł          wynik wczytania bajtów, jeszcze bez pamięci
 *   instancja      moduł z przydzieloną pamięcią liniową i globalnymi
 *   exec_env       stos wywołań; to jego dostaje `call_wasm`
 *
 * Rozdział jest po to, żeby jeden moduł dało się zinstancjonować wielokrotnie.
 * Nie korzystamy z tego — urządzenie uruchamia jeden program — ale kolejność
 * zwalniania musi być odwrotna do tworzenia, inaczej zostają wiszące wskaźniki.
 *
 * ## Wywłaszczanie
 *
 * Tak samo jak w wasm3: **nie ma go**. WAMR pozwala przerwać wykonanie
 * z innego wątku (`wasm_runtime_terminate`), co jest strażnikiem, a nie
 * wywłaszczeniem — stan przepada zamiast być wznowionym. Dlatego silnik
 * zgłasza `Preemption::RunToCompletion`, a nie `Watchdog`: strażnika nikt
 * jeszcze nie uruchamia.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT && HYDRA_SCRIPT_HAS_WAMR

#include "hydra/script/IScriptEngine.hpp"

#ifndef HYDRA_SCRIPT_ERROR_MAX
#  define HYDRA_SCRIPT_ERROR_MAX 128
#endif

namespace hydra {
namespace script {

class WamrEngine : public IScriptEngine {
public:
    struct Config {
        /** Pula na wszystko. Pusta oznacza pulę domyślną. */
        void*  pool      = nullptr;
        size_t poolBytes = 0;

        /** Stos wywołań maszyny wirtualnej. */
        u32 stackBytes = 8192;
        /**
         * Sterta modułu — obsługuje `malloc()` **wewnątrz** programu.
         *
         * Zero znaczy „program nie alokuje". Moduły z Rusta i AssemblyScriptu
         * zwykle alokują, więc zero daje im pułapkę przy pierwszym przydziale.
         */
        u32 heapBytes = 4096;
    };

    WamrEngine() = default;
    ~WamrEngine() override;

    void configure(const Config& cfg) { cfg_ = cfg; }

    EngineInfo info() const override;

    Status open() override;
    void   close() override;
    bool   ready() const override { return instance_ != nullptr || runtimeUp_; }

    Status loadBinary(CByteSpan image, const char* chunkName) override;

    bool   hasFunction(const char* name) const override;
    Status callFunction(const char* name) override;

    Status   startJob(const char* name) override;
    RunState resumeJob(u32 budget) override;
    RunState jobState() const override { return jobState_; }
    u32      jobSteps() const override { return steps_; }
    void     cancelJob() override;

    const char*  error() const override { return error_; }
    void         clearError() override { error_[0] = '\0'; }
    ScriptMemory memory() const override;

private:
    void setError(const char* text);
    /** Przepisuje wyjątek instancji do bufora błędu i czyści go po stronie WAMR. */
    void captureException();

    Config cfg_{};
    bool   runtimeUp_       = false;
    bool   ownsDefaultPool_ = false;

    void* module_   = nullptr;   ///< wasm_module_t
    void* instance_ = nullptr;   ///< wasm_module_inst_t
    void* execEnv_  = nullptr;   ///< wasm_exec_env_t
    void* pendingFn_ = nullptr;  ///< wasm_function_inst_t

    RunState jobState_ = RunState::Idle;
    u32      steps_    = 0;
    char     error_[HYDRA_SCRIPT_ERROR_MAX] = {};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT && HYDRA_SCRIPT_HAS_WAMR
