#pragma once
/**
 * @file IScriptEngine.hpp
 * @brief Silnik skryptowy widziany przez `ScriptModule`.
 *
 * Moduł skryptów robi rzeczy, które nie zależą od języka: prowadzi task, dzieli
 * czas między przebiegi, liczy błędy pod rząd, rozdaje sygnały z magistrali
 * i zgłasza `SysDegraded`, gdy skrypt się psuje. Który interpreter siedzi pod
 * spodem, nie ma dla tego znaczenia — i ten interfejs to rozstrzyga.
 *
 * ## Trzy modele oddawania procesora, nie jeden
 *
 * Najważniejsza rzecz w tym pliku. Lua da się **wywłaszczyć w środku
 * wywołania**: pułapka licznikowa przerywa skrypt co N instrukcji, a `resume()`
 * podejmuje go w tym samym miejscu. Interpretery WebAssembly tego nie mają —
 * wasm3 wykonuje wywołanie do końca, a WAMR pozwala je tylko zabić z innego
 * wątku.
 *
 * Gdyby interfejs udawał, że wszystkie trzy są takie same, `budget` znaczyłby
 * co innego przy każdym silniku, a `while(1)` w module WASM zawiesiłby
 * urządzenie mimo „ustawionego budżetu". Dlatego model jest **deklarowany**
 * w `EngineInfo::preemption`, a moduł skryptów pyta o niego, zanim obieca
 * cokolwiek na temat czasu wykonania.
 *
 * ```
 *   Cooperative       Lua    przerwanie w punkcie, wznowienie od niego
 *   RunToCompletion   wasm3  wywołanie idzie do końca; budżet doradczy
 *   Watchdog          WAMR   przerwanie z zewnątrz, w nieokreślonym miejscu
 * ```
 *
 * ## Konfiguracja zostaje przy silniku
 *
 * `open()` nie bierze argumentów. Biblioteki standardowe Lua i liczba stron
 * pamięci liniowej WebAssembly nie mają wspólnego mianownika, a wciśnięcie ich
 * w jedną strukturę dałoby zbiór pól, z których połowa jest zawsze pusta.
 * Ustawienia podaje się konkretnej klasie silnika przed `open()`; interfejs
 * zaczyna się tam, gdzie kończą się różnice.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace script {

/** Język, w którym napisano to, co silnik wykonuje. */
enum class ScriptLanguage : u8 {
    Lua = 0,
    Wasm,
};

/** Jak silnik oddaje procesor — patrz uwaga w nagłówku pliku. */
enum class Preemption : u8 {
    /** Przerwanie w punkcie i wznowienie od niego. Budżet jest wiążący. */
    Cooperative = 0,
    /** Wywołanie idzie do końca. Budżet jest wskazówką, nie gwarancją. */
    RunToCompletion,
    /** Przerwanie z zewnątrz, w miejscu nieokreślonym. Wynik jest porzucany. */
    Watchdog,
};

constexpr const char* toString(ScriptLanguage language) {
    switch (language) {
        case ScriptLanguage::Lua:  return "lua";
        case ScriptLanguage::Wasm: return "wasm";
    }
    return "?";
}

constexpr const char* toString(Preemption preemption) {
    switch (preemption) {
        case Preemption::Cooperative:     return "cooperative";
        case Preemption::RunToCompletion: return "run-to-completion";
        case Preemption::Watchdog:        return "watchdog";
    }
    return "?";
}

/** Czym jest silnik i czego można po nim oczekiwać. */
struct EngineInfo {
    /** Nazwa do logów i shella: "lua", "wasm3", "wamr". */
    const char*    name       = "?";
    ScriptLanguage language   = ScriptLanguage::Lua;
    Preemption     preemption = Preemption::Cooperative;
    /** Czy przyjmuje kod źródłowy jako tekst. */
    bool acceptsSource = false;
    /** Czy przyjmuje postać binarną — bytecode albo moduł `.wasm`. */
    bool acceptsBinary = false;
};

/**
 * Zużycie pamięci przez skrypt.
 *
 * Wspólny mianownik: Lua liczy stertę interpretera, WebAssembly pamięć
 * liniową modułu. Pojęcia nie są tożsame, ale odpowiadają na to samo pytanie —
 * ile z przydzielonego miejsca skrypt już zajął i jak blisko był granicy.
 */
struct ScriptMemory {
    u32 capacityBytes = 0;
    u32 usedBytes     = 0;
    u32 peakBytes     = 0;
};

/**
 * Wykonanie funkcji skryptu, ewentualnie w porcjach.
 *
 * Stany są te same dla wszystkich silników; różni je tylko to, czy `Running`
 * w ogóle występuje. Przy `RunToCompletion` `resume()` wraca od razu z `Done`
 * albo `Failed` — wołający nie musi tego rozróżniać, bo pętla „wołaj, dopóki
 * `Running`" działa w obu przypadkach.
 */
enum class RunState : u8 {
    Idle = 0,
    Running,
    Done,
    Failed,
};

class IScriptEngine {
public:
    virtual ~IScriptEngine() = default;

    virtual EngineInfo info() const = 0;

    /** Uruchamia interpreter. Ustawienia podaje się klasie silnika wcześniej. */
    virtual Status open()  = 0;
    virtual void   close() = 0;
    virtual bool   ready() const = 0;

    /**
     * Wczytuje program w postaci tekstowej.
     *
     * `Err::NotSupported`, gdy silnik przyjmuje wyłącznie postać binarną —
     * i jest to poprawna odpowiedź, nie awaria. Moduł skryptów sprawdza
     * `EngineInfo::acceptsSource` i wybiera właściwe wejście.
     */
    virtual Status loadSource(const char* source, const char* chunkName) {
        (void)source;
        (void)chunkName;
        return fail(Err::NotSupported);
    }

    /** Wczytuje program w postaci binarnej: bytecode albo moduł `.wasm`. */
    virtual Status loadBinary(CByteSpan image, const char* chunkName) {
        (void)image;
        (void)chunkName;
        return fail(Err::NotSupported);
    }

    /** Czy program udostępnia funkcję o tej nazwie. */
    virtual bool hasFunction(const char* name) const = 0;

    /** Woła funkcję do końca, bez dzielenia na porcje. */
    virtual Status callFunction(const char* name) = 0;

    // ── Wykonanie porcjami ─────────────────────────────────────────────────

    /** Przygotowuje wywołanie. `Err::NotFound`, gdy funkcji nie ma. */
    virtual Status startJob(const char* name) = 0;

    /**
     * Posuwa wykonanie o najwyżej `budget` kroków maszyny wirtualnej.
     *
     * Znaczenie `budget` zależy od `EngineInfo::preemption` i jest to
     * różnica, o której wołający musi wiedzieć — patrz nagłówek pliku.
     * Budżet zerowy zawsze znaczy „bez ograniczenia".
     */
    virtual RunState resumeJob(u32 budget) = 0;

    virtual RunState jobState() const = 0;
    /** Ile kroków wykonano od `startJob()`. */
    virtual u32      jobSteps() const = 0;
    /** Porzuca wykonanie i zwalnia jego zasoby. */
    virtual void     cancelJob() = 0;

    // ── Diagnostyka ────────────────────────────────────────────────────────

    /** Treść ostatniego błędu. Nigdy `nullptr`. */
    virtual const char*  error() const = 0;
    virtual void         clearError() = 0;
    virtual ScriptMemory memory() const = 0;

    /**
     * Odzyskiwanie pamięci, jeśli silnik je ma.
     *
     * Lua odśmieca, WebAssembly nie — pamięć liniowa modułu jest przydzielana
     * raz i nie kurczy się. Domyślna implementacja zwraca bieżące zużycie,
     * bo to jest prawdziwa odpowiedź na pytanie „ile zajmujesz po sprzątaniu".
     */
    virtual u32 collect() { return memory().usedBytes; }
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
