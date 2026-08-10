#pragma once
/**
 * Hydra — silnik wykonujący skrypt, widziany przez moduł skryptowy.
 *
 * `ScriptModule` obiecuje trzy rzeczy i żadna z nich nie jest własnością Lua:
 * skrypt dostaje ograniczoną pulę pamięci, wykonuje się w porcjach o zadanej
 * liczbie instrukcji i nie może zawiesić urządzenia. Ten interfejs jest
 * spisaniem tej obietnicy tak, żeby dało się jej dotrzymać także maszyną
 * WebAssembly — a `ScriptModule` nie musiał wiedzieć, którą.
 *
 * Podział obowiązków między moduł a silnik:
 *
 *   - moduł zna **cykl życia i politykę** — kiedy wołać `setup()`, ile budżetu
 *     dać na przebieg, po ilu błędach przestać próbować,
 *   - silnik zna **wykonanie** — jak wczytać jednostkę wykonywalną, jak ją
 *     wywłaszczyć i wznowić, gdzie kończy się jego pula.
 *
 * Dlatego bindingi instaluje silnik, a nie moduł: `hydra.gpio.write` jest
 * w Lua wpisem w tabeli globalnej, a w WebAssembly funkcją importowaną
 * o ustalonej sygnaturze. Wspólny jest wybór (`BindingSet`), nie mechanizm.
 *
 * **Sterta jest wspólna.** `Heap` nie zna ani Lua, ani Hydry — trzyma jedną
 * statyczną pulę i mierzy jej zużycie. Każdy silnik ma z niej korzystać, bo to
 * ona realizuje regułę „po `App::begin()` nie sięgamy po stertę systemową",
 * i to jej `Stats` odpowiada na pytanie „ile RAM-u zjada ten skrypt".
 *
 * Wątkowość jak w `Interp`: jeden silnik obsługuje jeden task. Dzielenie
 * między taski wymaga synchronizacji po stronie aplikacji.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/script/Bindings.hpp"
#include "hydra/script/Heap.hpp"

namespace hydra {
namespace script {

class IScriptEngine {
public:
    /**
     * Stan wywłaszczalnego wywołania.
     *
     * Powtarza `Job::State`, ale świadomie osobnym typem: `Job` jest mechaniką
     * Lua (wątek `lua_State` zakotwiczony w rejestrze), a to jest pojęcie
     * z umowy między modułem a silnikiem.
     */
    enum class JobState : u8 {
        Idle = 0,   ///< nie wystartowano albo wynik odebrano
        Running,    ///< wywłaszczony po wyczerpaniu budżetu, czeka na wznowienie
        Done,       ///< funkcja wróciła normalnie
        Failed,     ///< błąd wykonania — treść w `error()`
    };

    virtual ~IScriptEngine() = default;

    /** Nazwa do logów i diagnostyki: "lua", "wasm3", "wamr". */
    virtual const char* name() const = 0;

    // --- cykl życia --------------------------------------------------------

    /**
     * Otwiera silnik na podanej puli. Pusta pula oznacza pulę domyślną
     * (HYDRA_SCRIPT_HEAP_BYTES) — ta może należeć tylko do jednego silnika naraz.
     */
    virtual Status open(void* pool, size_t poolBytes) = 0;
    virtual void   close()                            = 0;
    virtual bool   ready() const                      = 0;

    // --- powierzchnia widoczna dla skryptu ---------------------------------

    /** Udostępnia wybrane grupy funkcji. Wołać po `open()`, przed `load()`. */
    virtual Status installBindings(const BindingSet& set) = 0;
    /** Zwalnia subskrypcję magistrali. Wołane przy zamykaniu. */
    virtual void   removeBindings()                       = 0;
    /**
     * Wykonuje handlery zdarzeń zarejestrowane przez skrypt, najwyżej
     * `maxSignals` w jednym wywołaniu. Zwraca liczbę obsłużonych.
     */
    virtual u32    dispatchSignals(u32 maxSignals)        = 0;

    // --- jednostka wykonywalna ---------------------------------------------

    /**
     * Wczytuje to, co silnik wykonuje: źródło Lua albo moduł WebAssembly.
     *
     * `bytes` zerowe oznacza obraz zakończony zerem — tak przychodzi tekst
     * skryptu z pamięci programu. Silniki binarne wymagają jawnej długości.
     * `name` trafia do komunikatów błędów.
     */
    virtual Status load(const void* image, size_t bytes, const char* name) = 0;

    virtual bool   hasFunction(const char* fn) const = 0;

    /**
     * Wywołanie bez budżetu, do `setup()`. Nie da się go wywłaszczyć, więc
     * odpowiedzialność za czas wykonania spada na autora skryptu — tak samo,
     * jak przy budżecie zerowym.
     */
    virtual Status call(const char* fn) = 0;

    // --- wywłaszczalne wywołanie -------------------------------------------

    /** Przygotowuje wywołanie. NotFound, gdy takiej funkcji nie ma. */
    virtual Status   jobBegin(const char* fn) = 0;
    /** Wykonuje do `budget` instrukcji. Budżet zerowy znosi ograniczenie. */
    virtual JobState jobStep(u32 budget)      = 0;
    virtual JobState jobState() const         = 0;
    /** Porzuca wykonanie i zwalnia zasoby wywołania. */
    virtual void     jobCancel()              = 0;
    /** Ile instrukcji wykonano od `jobBegin()`. */
    virtual u32      jobSteps() const         = 0;

    // --- diagnostyka -------------------------------------------------------

    virtual Heap::Stats memory() const  = 0;
    /** Pełny cykl odśmiecania. Zwraca liczbę bajtów w użyciu po zakończeniu. */
    virtual u32         collect()       = 0;
    /** Treść ostatniego błędu. Nigdy nullptr. */
    virtual const char* error() const   = 0;

    /**
     * Wykonuje fragment kodu źródłowego w bieżącym stanie — to, co robi
     * `lua print(x)` w shellu.
     *
     * Nie jest czysto wirtualna, bo dla silnika binarnego nie istnieje: moduł
     * WebAssembly przychodzi skompilowany i nie ma czego wpisać w wiersz
     * polecenia. Domyślna odpowiedź brzmi „nie ta liga", a nie „błąd".
     */
    virtual Status eval(const char* source, const char* name) {
        (void)source;
        (void)name;
        return fail(Err::NotSupported);
    }
};

}  // namespace script
}  // namespace hydra
