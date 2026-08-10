#pragma once
/**
 * Hydra — silnik skryptowy WebAssembly na osadzonym wasm3.
 *
 * Drugi backend obok Lua. Wartość jest w tym, czego Lua nie daje: moduł
 * powstaje z Rusta, C++, TinyGo albo AssemblyScript, jest **piaskownicą**
 * (dostęp do świata wyłącznie przez zadeklarowane importy) i przychodzi jako
 * skompilowany bajtkod, więc urządzenie nie płaci za parsowanie źródła.
 *
 * ## Trzy różnice względem silnika Lua, o których trzeba wiedzieć
 *
 * **1. Budżet liczy pętle i wywołania, nie instrukcje.** wasm3 nie ma licznika
 * instrukcji; łatka Hydry pobiera budżet na krawędzi wstecznej pętli oraz przy
 * `call`/`call_indirect` — czyli w każdym miejscu, gdzie może powstać wykonanie
 * bez końca. Kod prostoliniowy jest skończony z definicji, bo ogranicza go
 * rozmiar modułu. Szczegóły: `src/wasm3/VENDOR.md`.
 *
 * **2. Wykonania nie da się wznowić.** Lua wywłaszcza przez korutynę i wraca
 * dokładnie tam, gdzie stanęła. wasm3 nie ma czego zapisać — stos C jest
 * spleciony z wywołaniami ogonowymi interpretera. Wyczerpanie budżetu przerywa
 * `loop()` i zwraca `JobState::Exhausted`; kolejny przebieg zaczyna funkcję od
 * początku. Urządzenie żyje, ciągłość przebiegu przepada.
 *
 * **3. Dane wracają przez bufor, nie przez tabelę.** WebAssembly zna cztery
 * typy liczbowe i pamięć liniową — tabel nie ma. Tam, gdzie Lua oddaje tabelę
 * (`i2c.scan`, `i2c.read`), moduł podaje offset w swojej pamięci, a wynik
 * funkcji mówi, ile bajtów tam trafiło albo że był błąd. Każdy taki bufor jest
 * sprawdzany wobec granic pamięci modułu.
 *
 * Zdarzenia idą podobnie w drugą stronę: moduł publikuje przez `event_emit`,
 * a odbiera przez **eksportowaną** funkcję `on_event(nameId, value, data)` —
 * bo nie ma domknięć, które dałoby się zarejestrować tak, jak `hydra.event.on`
 * w Lua. Grupa niewłączona w `BindingSet` nie zostaje zlinkowana, więc moduł
 * jej żądający nie wczyta się z jasnym błędem, zamiast działać połowicznie.
 *
 * ## Co widzi moduł
 *
 * Wszystkie importy leżą w module `"hydra"`. Napisy przekazuje się jako parę
 * (offset w pamięci liniowej, długość) — nie ma innego sposobu, bo pamięć
 * modułu jest jego własną przestrzenią adresową.
 *
 *     (import "hydra" "millis"     (func $millis (result i32)))
 *     (import "hydra" "log"        (func $log (param i32 i32 i32)))
 *     (import "hydra" "gpio_mode"  (func $gpio_mode (param i32 i32) (result i32)))
 *     (import "hydra" "gpio_write" (func $gpio_write (param i32 i32) (result i32)))
 *     (import "hydra" "gpio_read"  (func $gpio_read (param i32) (result i32)))
 *
 * Pełna lista: `docs/wasm-imports.md`, generowana z `tools/wasm_bindings.def`
 * razem z deklaracjami dla AssemblyScript.
 *
 * Moduł eksportuje `setup` i `loop`, obie bez argumentów i bez wyniku — ta sama
 * umowa, co w Lua. Obie są opcjonalne. Trzecim, również opcjonalnym eksportem
 * jest `on_event(i32, f32, i32)`.
 *
 * ## Pamięć
 *
 * Silnik dostaje pulę tak samo jak interpreter Lua i alokuje wyłącznie z niej
 * (patrz `WasmAlloc.h`). Ograniczenie: wasm3 alokuje przez funkcje bez
 * kontekstu, więc **jedna pula naraz** — dwa otwarte silniki WASM jednocześnie
 * nie są obsługiwane. To samo ograniczenie, co przy domyślnej puli `Interp`.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_WASM

#include <string.h>

#include "hydra/script/IScriptEngine.hpp"

/**
 * Stos wykonania modułu w bajtach — przestrzeń na ramki funkcji WebAssembly,
 * osobna od stosu taska. Domyślna wartość wasm3 (64 kB) jest szczodra jak na
 * urządzenie; profil mały schodzi do rozmiaru, przy którym mieści się jeszcze
 * rekurencja o rozsądnej głębokości.
 */
#ifndef HYDRA_WASM_STACK_BYTES
#  if HYDRA_SCRIPT_LARGE_PROFILE
#    define HYDRA_WASM_STACK_BYTES (16 * 1024)
#  else
#    define HYDRA_WASM_STACK_BYTES (4 * 1024)
#  endif
#endif

namespace hydra {
namespace script {

class WasmEngine final : public IScriptEngine {
public:
    struct Config {
        /** Stos wykonania modułu w bajtach. */
        u32 stackBytes = HYDRA_WASM_STACK_BYTES;
    };

    WasmEngine() = default;
    explicit WasmEngine(const Config& cfg) : cfg_(cfg) {}

    void configure(const Config& cfg) { cfg_ = cfg; }

    // --- IScriptEngine -----------------------------------------------------

    const char* name() const override { return "wasm3"; }

    Status open(void* pool, size_t poolBytes) override;
    void   close() override;
    bool   ready() const override { return runtime_ != nullptr; }

    /**
     * Zapamiętuje wybór grup. Linkowanie następuje dopiero w `load()`, bo
     * w WebAssembly importy wiąże się z konkretnym modułem, a nie ze
     * środowiskiem — inaczej niż tabela globalna w Lua.
     */
    Status installBindings(const BindingSet& set) override;
    void   removeBindings() override;
    /**
     * Oddaje sygnały z magistrali eksportowanej funkcji `on_event`.
     *
     * Odbiór jest eksportem, a nie callbackiem: WebAssembly nie ma domknięć,
     * które dałoby się zarejestrować w tabeli tak, jak robi to `hydra.event.on`
     * w Lua. Moduł bez `on_event` też opróżnia pierścień — inaczej zapchałby
     * się i zaczął gubić zdarzenia adresowane do kogoś innego.
     */
    u32    dispatchSignals(u32 maxSignals) override;

    Status load(const void* image, size_t bytes, const char* name) override;

    /**
     * Przenośny bajtkod tak, kod AOT nie — ten wariant wymaga runtime'u
     * z kompilacją z wyprzedzeniem, a wasm3 go nie ma. Odmowa jest tu
     * właściwa: obraz AOT dla obcego celu nie jest „gorszy", tylko
     * niewykonywalny.
     */
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
    /** Wiąże wybrane grupy z importami modułu. Wołane z `load()`. */
    Status linkBindings();
    void   setError(const char* text);
    /** Zapisuje treść błędu wasm3 razem z jego informacją kontekstową. */
    void   captureError(const char* result);

    void* environment_ = nullptr;  ///< IM3Environment
    void* runtime_     = nullptr;  ///< IM3Runtime
    void* module_      = nullptr;  ///< IM3Module — należy do runtime po załadowaniu
    void* jobFn_       = nullptr;  ///< IM3Function bieżącego zadania

    Heap       heap_{};
    Config     cfg_{};
    BindingSet bindings_{};
    JobState   state_ = JobState::Idle;
    u32        steps_ = 0;
    bool       ownsDefaultPool_ = false;

    char error_[HYDRA_SCRIPT_ERROR_MAX] = {};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_SCRIPT_ENGINE_WASM
