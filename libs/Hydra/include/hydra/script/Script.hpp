#pragma once
/**
 * Hydra — osadzony interpreter Lua.
 *
 * Pod spodem leży oficjalne Lua 5.4 z lua.org (patrz `src/lua/VENDOR.md`), ale
 * kod aplikacji go nie widzi — tak samo, jak nie widzi Arduino ani FreeRTOS-a.
 * W tym nagłówku nie ma ani jednego typu Lua: `lua_State` jest szczegółem
 * implementacji, a funkcje natywne dostają `Ctx` z akcesorami w stylu Hydry,
 * zwracającymi `Result<T>`. Dzięki temu użytkownik nie musi mieć na ścieżce
 * włączeń nagłówków Lua, a zmiana wersji interpretera nie przechodzi przez API.
 *
 * Trzy rzeczy, które odróżniają to osadzenie od podręcznikowego:
 *
 *   1. Pamięć. Interpreter dostaje statyczną pulę i nie sięga po `malloc`
 *      (rozdz. 11). Wyczerpanie puli to błąd skryptu, nie brak pamięci
 *      w systemie. Zużycie widać przez `memory()`.
 *   2. Czas. `Job` wykonuje funkcję w porcjach o zadanej liczbie instrukcji,
 *      więc skrypt z nieskończoną pętlą nie zatrzyma pętli sterowania —
 *      zostanie wywłaszczony i wznowiony w kolejnym przebiegu taska.
 *   3. Błędy. Nic nie leci wyjątkiem: kompilacja i wykonanie zwracają `Status`,
 *      a treść ostatniego błędu podaje `error()`.
 *
 * Wątkowość: jeden `Interp` obsługuje jeden task. Lua nie jest wielobieżna,
 * a Hydra nie zakłada tu blokad — dzielenie interpretera między taski wymaga
 * synchronizacji po stronie aplikacji.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/script/Heap.hpp"
#include "hydra/script/Output.hpp"

/**
 * Długość bufora na treść ostatniego błędu. Komunikaty Lua zawierają nazwę
 * fragmentu i numer wiersza, więc skracanie poniżej stu znaków obcina właśnie
 * to, po co się je czyta.
 */
#ifndef HYDRA_SCRIPT_ERROR_MAX
#  define HYDRA_SCRIPT_ERROR_MAX 128
#endif

namespace hydra {
namespace script {

class Interp;

namespace detail {
/**
 * Furtka warstwy wewnętrznej do prywatnych pól `Ctx`.
 *
 * Trampolina wołana przez Lua ma linkowanie C i przyjmuje `lua_State*`, więc
 * nie da się jej zadeklarować jako zaprzyjaźnionej w nagłówku, który o Lua nie
 * wie. Zamiast tego zaprzyjaźniamy jeden typ pomocniczy, definiowany
 * w `src/script/Interp.cpp`.
 */
struct CtxAccess;
}  // namespace detail

/**
 * Kontekst wywołania funkcji natywnej.
 *
 * Argumenty numerowane są od jedynki, tak jak w Lua. Akcesory `arg*` zwracają
 * `Result`, więc brakujący albo niewłaściwy argument nie jest cichą zerową
 * wartością; `opt*` podają wartość zastępczą tam, gdzie argument jest naprawdę
 * opcjonalny.
 *
 * Klasa jest trywialnie niszczalna i taka musi pozostać: gdy funkcja natywna
 * zgłosi błąd, Lua wraca do najbliższego `pcall` skokiem `longjmp`, który nie
 * wykonuje destruktorów C++. Z tego samego powodu w funkcji natywnej nie wolno
 * trzymać obiektów zwalniających zasoby w destruktorze.
 */
class Ctx {
public:
    /** Liczba argumentów przekazanych ze skryptu. */
    int argCount() const;

    bool isNil(int index) const;
    bool isNumber(int index) const;
    bool isString(int index) const;
    bool isBool(int index) const;
    bool isTable(int index) const;
    bool isFunction(int index) const;

    Result<i32>         argInt(int index) const;
    Result<float>       argNumber(int index) const;
    Result<const char*> argStr(int index) const;
    Result<bool>        argBool(int index) const;

    /**
     * Dowolna wartość zamieniona na tekst tak, jak zrobiłby to `tostring()` —
     * łącznie z metametodą `__tostring`. Zwrócony napis żyje do powrotu
     * z funkcji natywnej.
     */
    const char* text(int index) const;

    i32         optInt(int index, i32 fallback) const;
    float       optNumber(int index, float fallback) const;
    const char* optStr(int index, const char* fallback) const;
    bool        optBool(int index, bool fallback) const;

    // --- wyniki ------------------------------------------------------------
    void pushNil();
    void pushInt(i32 value);
    void pushNumber(float value);
    void pushBool(bool value);
    void pushStr(const char* text);
    void pushStr(const char* text, size_t len);
    /** Odkłada nową, pustą tabelę. Kolejne `setField`/`setIndex` ją wypełniają. */
    void pushTable();

    /** `t[key] = wartość ze szczytu`, gdzie `t` jest pod nią. Zdejmuje wartość. */
    void setField(const char* key);
    /** `t[index] = wartość ze szczytu` dla klucza całkowitego. Zdejmuje wartość. */
    void setIndex(i32 index);

    /** Odczyt pola z tabeli podanej jako argument. */
    Result<i32>         fieldInt(int tableIndex, const char* key) const;
    Result<float>       fieldNumber(int tableIndex, const char* key) const;
    Result<const char*> fieldStr(int tableIndex, const char* key) const;
    /** Odczyt elementu po indeksie liczbowym — `t[i]`, licząc od jedynki. */
    Result<i32>         indexInt(int tableIndex, i32 index) const;
    /** Liczba elementów części tablicowej (odpowiednik `#t`). */
    Result<u32>         tableLength(int tableIndex) const;

    /**
     * Zgłasza błąd wykonania. Zwraca wartość, którą funkcja natywna ma oddać:
     *
     *     if (!pin) return c.fail("nieznany pin %d", n);
     *
     * Błąd nie jest wyrzucany od razu — odkłada go trampolina po wyjściu
     * z ramki C++, żeby skok `longjmp` nie przeskoczył cudzych destruktorów.
     */
    int fail(const char* format, ...);

    /** Wskaźnik przekazany przy rejestracji funkcji albo biblioteki. */
    void* user() const { return user_; }

    /** Interpreter, do którego należy to wywołanie. */
    Interp& interp() const { return *interp_; }

private:
    friend class Interp;
    friend struct detail::CtxAccess;

    void*   state_  = nullptr;  ///< lua_State*
    Interp* interp_ = nullptr;
    void*   user_   = nullptr;
    bool    failed_ = false;
};

/** Funkcja natywna: zwraca liczbę wyników albo wynik `Ctx::fail`. */
using NativeFn = int (*)(Ctx&);

/** Wpis tablicy rejestracyjnej biblioteki. Ostatni wpis ma `name == nullptr`. */
struct Reg {
    const char* name;
    NativeFn    fn;
};

class Interp : NonCopyable {
public:
    /** Które biblioteki standardowe otworzyć. */
    struct Libs {
        bool base      = true;   ///< print, type, pairs, pcall, tostring, ...
        bool string    = true;   ///< także wzorce: find, match, gsub, format
        bool table     = true;
        bool math      = true;
        bool coroutine = true;
        bool utf8      = false;
        bool debug     = false;  ///< traceback — przydatny, ale daje wgląd w stan
    };

    struct Config {
        Libs libs{};
        /**
         * Własna pula pamięci. Gdy `pool` jest pusty, interpreter bierze
         * statyczną pulę o rozmiarze HYDRA_SCRIPT_HEAP_BYTES.
         */
        void*  pool      = nullptr;
        size_t poolBytes = 0;
    };

    Interp() = default;
    ~Interp();

    Status open(const Config& cfg);
    /**
     * Otwiera z konfiguracją domyślną.
     *
     * Osobne przeciążenie zamiast argumentu domyślnego `= Config{}`, bo składnia
     * z argumentem domyślnym wymagałaby kompletnej definicji `Config` przed
     * końcem klasy, w której `Config` jest zagnieżdżony.
     */
    Status open();
    void   close();
    bool   ready() const { return state_ != nullptr; }

    /** Kompiluje i wykonuje fragment. Treść błędu podaje `error()`. */
    Status doString(const char* source, const char* chunkName = "=script");

    /** Czy istnieje globalna wartość o tej nazwie. */
    bool hasGlobal(const char* name) const;
    /** Czy globalna wartość jest wywoływalna. */
    bool hasFunction(const char* name) const;

    /** Wywołuje globalną funkcję bez argumentów i bez wyników. */
    Status callGlobal(const char* name);

    Status setGlobalInt(const char* name, i32 value);
    Status setGlobalNumber(const char* name, float value);
    Status setGlobalStr(const char* name, const char* value);
    Status setGlobalBool(const char* name, bool value);

    /** Rejestruje pojedynczą funkcję w zasięgu globalnym. */
    Status registerFn(const char* name, NativeFn fn, void* user = nullptr);

    /**
     * Rejestruje bibliotekę jako tabelę globalną. `regs` kończy się wpisem
     * z pustą nazwą. Kropka w nazwie tworzy zagnieżdżenie: "hydra.gpio"
     * dołoży pole `gpio` do istniejącej tabeli `hydra`.
     */
    Status registerLib(const char* tableName, const Reg* regs, void* user = nullptr);

    /** Treść ostatniego błędu kompilacji albo wykonania. Nigdy nullptr. */
    const char* error() const { return error_; }
    void        clearError();

    Heap::Stats memory() const { return heap_.stats(); }
    /** Pełny cykl odśmiecania. Zwraca liczbę bajtów w użyciu po zakończeniu. */
    u32         collect();

    /** Wskaźnik na `lua_State`. Wyłącznie dla warstwy bindingów Hydry. */
    void* rawState() const { return state_; }

    /** Ustawia treść błędu z warstwy bindingów. */
    void setError(const char* text);

private:
    friend class Job;

    Status openLibs(const Libs& libs);
    Status pushLibTable(const char* name);

    void* state_ = nullptr;  ///< lua_State*
    Heap  heap_{};
    /** Czy ten interpreter zajął pulę domyślną — tylko wtedy ma ją zwolnić. */
    bool  ownsDefaultPool_ = false;
    char  error_[HYDRA_SCRIPT_ERROR_MAX] = {};
};

/**
 * Wykonanie funkcji skryptu w porcjach o ograniczonej liczbie instrukcji.
 *
 * To jest odpowiedź na jedyny poważny zarzut wobec skryptów w systemie czasu
 * rzeczywistego: że `while true do end` w skrypcie zawiesza urządzenie. Job
 * uruchamia funkcję we własnym wątku Lua z pułapką licznikową, która po zadanej
 * liczbie instrukcji wywłaszcza skrypt. Task woła `resume()` w każdym przebiegu,
 * a skrypt kontynuuje tam, gdzie stanął — pętla sterowania nie czeka.
 *
 * Cena jest jedna i trzeba ją znać: funkcja natywna, która sama wywołuje kod
 * Lua, tworzy barierę, przez którą nie da się wywłaszczyć. Bindingi Hydry takich
 * wywołań nie robią.
 */
class Job {
public:
    enum class State : u8 {
        Idle = 0,   ///< nie wystartowano albo zakończono i odebrano wynik
        Running,    ///< wywłaszczony po wyczerpaniu budżetu, czeka na `resume`
        Done,       ///< funkcja wróciła normalnie
        Failed,     ///< błąd wykonania — treść w `Interp::error()`
    };

    /** Przygotowuje wywołanie globalnej funkcji. NotFound, gdy takiej nie ma. */
    Status start(Interp& interp, const char* globalFn);

    /**
     * Wykonuje do `budget` instrukcji maszyny wirtualnej.
     * Budżet zerowy oznacza brak ograniczenia.
     */
    State resume(u32 budget);

    State state() const { return state_; }
    /** Ile instrukcji wykonano od `start()`. */
    u32   steps() const { return steps_; }
    /** Porzuca wykonanie i zwalnia wątek Lua. */
    void  cancel();

private:
    friend struct detail::CtxAccess;

    void release();

    Interp* interp_ = nullptr;
    void*   thread_ = nullptr;  ///< lua_State* wątku
    int     ref_    = 0;        ///< kotwica w rejestrze, żeby wątku nie zebrał GC
    State   state_  = State::Idle;
    u32     steps_  = 0;
    u32     budget_ = 0;        ///< budżet bieżącego przebiegu — dolicza go pułapka
};

}  // namespace script
}  // namespace hydra
