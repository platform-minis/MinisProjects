#pragma once
/**
 * @file Wasm3Engine.hpp
 * @brief WebAssembly przez wasm3 — drugi silnik skryptowy Hydry.
 *
 * Moduł `.wasm` zamiast tekstu Lua. Program powstaje w Rust, C, C++, TinyGo
 * albo AssemblyScript, a na urządzenie trafia jako kilka kilobajtów bajtów,
 * których nikt nie kompiluje na miejscu.
 *
 * ## Co daje, czego Lua nie dawała
 *
 *  - **Izolację pamięci wymuszoną przez maszynę.** Moduł widzi wyłącznie
 *    swoją pamięć liniową; każdy dostęp jest sprawdzany przez interpreter.
 *    W Lua izolacja opierała się na tym, że skrypt nie dostał niebezpiecznych
 *    funkcji — tu jest własnością formatu, nie doboru bibliotek.
 *  - **Wybór języka.** Ten sam moduł działa niezależnie od tego, w czym go
 *    napisano.
 *
 * ## Czego nie daje i trzeba o tym wiedzieć
 *
 * **Wywłaszczania w punkcie.** wasm3 nie ma pułapki licznikowej — wywołanie
 * `loop()` idzie do końca i nikt go nie przerwie. `while(1)` w module WASM
 * zawiesza task skryptu, czego `while true do end` w Lua nie robiło. Silnik
 * zgłasza to jako `Preemption::RunToCompletion`, a `ScriptModule` ostrzega
 * w logu, gdy ustawiono budżet, którego nie da się dotrzymać.
 *
 * Nie jest to przeoczenie wasm3, tylko cena za jego rozmiar: licznik
 * instrukcji kosztowałby sprawdzenie przy każdej z nich.
 *
 * ## Pamięć
 *
 * Wszystko idzie z puli podanej w `configure()` — kod modułu, jego pamięć
 * liniowa i struktury interpretera. Poza tę pulę silnik nie sięga, więc
 * „ile zajmuje skrypt" ma jedną odpowiedź, a nie trzy.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/Heap.hpp"
#include "hydra/script/IScriptEngine.hpp"

/**
 * Rozmiar bufora na treść błędu.
 *
 * Ta sama stała, z której korzysta interpreter Lua — komunikat z wasm3 ma
 * przejść przez shell i log tą samą drogą, więc obcinamy go tak samo.
 */
#ifndef HYDRA_SCRIPT_ERROR_MAX
#  define HYDRA_SCRIPT_ERROR_MAX 128
#endif

namespace hydra {
namespace script {

class Wasm3Engine : public IScriptEngine {
public:
    struct Config {
        /**
         * Pula na wszystko: interpreter, kod modułu, pamięć liniowa.
         * Pusta oznacza pulę domyślną o rozmiarze HYDRA_SCRIPT_HEAP_BYTES.
         */
        void*  pool      = nullptr;
        size_t poolBytes = 0;

        /**
         * Stos maszyny wirtualnej w bajtach.
         *
         * Nie mylić ze stosem taska. To jest miejsce na wartości i ramki
         * wywołań wewnątrz modułu; rekurencja w module zjada właśnie je.
         * Przekroczenie kończy się błędem wykonania, nie uszkodzeniem pamięci.
         */
        u32 stackBytes = 4096;
    };

    Wasm3Engine() = default;
    ~Wasm3Engine() override;

    void configure(const Config& cfg) { cfg_ = cfg; }

    EngineInfo info() const override;

    Status open() override;
    void   close() override;
    bool   ready() const override { return runtime_ != nullptr; }

    /** Wczytuje moduł `.wasm`. Bajty muszą przeżyć silnik — wasm3 ich nie kopiuje. */
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

    /**
     * Woła funkcję bez argumentów i oddaje jej wynik całkowity.
     *
     * `callFunction()` wynik pomija, bo `IScriptEngine` go nie przewiduje —
     * a moduły wystawiają gettery stanu i bez tego nie dałoby się ich odczytać
     * inaczej niż przez sięgnięcie do pamięci liniowej po adresie zgadniętym
     * z układu programu.
     */
    Result<i32> callInt(const char* name);

    /** Rozmiar pamięci liniowej modułu w bajtach; 0, gdy moduł jej nie ma. */
    u32 linearMemoryBytes() const;

    /**
     * Uchwyty wasm3 dla warstwy bindingów.
     *
     * `void*`, żeby typy wasm3 nie wyciekły do nagłówka publicznego — ta sama
     * zasada, co przy `Interp::rawState()`. Poza `WasmBindings.cpp` nie ma
     * z nich żadnego pożytku.
     */
    void* rawRuntime() const { return runtime_; }
    void* rawModule() const  { return module_; }

private:
    void setError(const char* text);

    Config cfg_{};
    Heap   heap_{};
    bool   ownsDefaultPool_ = false;

    void* environment_ = nullptr;   ///< IM3Environment
    void* runtime_     = nullptr;   ///< IM3Runtime
    void* module_      = nullptr;   ///< IM3Module — należy do runtime po załadowaniu
    void* pendingFn_   = nullptr;   ///< IM3Function czekająca na resumeJob()

    RunState jobState_ = RunState::Idle;
    u32      steps_    = 0;
    char     error_[HYDRA_SCRIPT_ERROR_MAX] = {};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
