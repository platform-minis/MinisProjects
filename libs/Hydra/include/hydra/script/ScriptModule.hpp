#pragma once
/**
 * Hydra — moduł skryptowy w cyklu życia frameworka.
 *
 * Spina interpreter z resztą systemu tak, jak każdy inny moduł: `init()`
 * otwiera interpreter i wczytuje skrypt, `start()` uruchamia task, `stop()`
 * wszystko sprząta. Skrypt widzi dwie umowne funkcje:
 *
 *     function setup()  -- wołana raz, po wczytaniu
 *     function loop()   -- wołana w każdym przebiegu taska
 *
 * Obie są opcjonalne. Skrypt może równie dobrze zarejestrować się wyłącznie na
 * zdarzeniach przez `hydra.event.on` i nie mieć `loop()` w ogóle.
 *
 * **Skrypt nie może zawiesić urządzenia.** `loop()` wykonuje się z budżetem
 * instrukcji: po jego wyczerpaniu skrypt jest wywłaszczany i wznawiany
 * w kolejnym przebiegu. `while true do end` w skrypcie skończy się tym, że
 * `loop()` nigdy nie wróci i nie zostanie wywołany ponownie — ale task oddaje
 * procesor co przebieg, więc pętla sterowania, sieć i UI działają dalej. To jest
 * różnica między błędem w skrypcie a awarią urządzenia i cała rzecz w tym,
 * żeby te dwa pojęcia rozdzielić.
 *
 * Priorytet taska jest domyślnie niski: skrypt jest wygodą, a nie zobowiązaniem
 * czasowym. Nie wolno mu opóźnić `motion.control` ani `sense.poll`.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/IModule.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/script/Bindings.hpp"
#include "hydra/script/IScriptEngine.hpp"

#if HYDRA_ENABLE_SCRIPT

/**
 * Stos taska skryptu w słowach.
 *
 * Wyraźnie więcej niż HYDRA_DEFAULT_STACK, bo maszyna wirtualna schodzi
 * w rekurencję stosu C — Lua przy metametodach i funkcjach natywnych. Górną
 * granicę tej rekurencji ustala LUAI_MAXCCALLS w `src/lua/hydra_lua_conf.h`
 * i te dwie liczby trzeba zmieniać razem.
 */
#ifndef HYDRA_SCRIPT_STACK_WORDS
#  if HYDRA_SCRIPT_LARGE_PROFILE
#    define HYDRA_SCRIPT_STACK_WORDS 8192
#  else
#    define HYDRA_SCRIPT_STACK_WORDS 4096
#  endif
#endif

namespace hydra {
namespace script {

class ScriptModule : public ModuleBase {
public:
    struct Config {
        u32  periodMs   = 50;
        Prio priority   = Prio::Low;
        Core core       = Core::Any;
        u32  stackWords = HYDRA_SCRIPT_STACK_WORDS;

        /**
         * Ile instrukcji maszyny wirtualnej wolno wykonać w jednym przebiegu.
         * Zero znosi ograniczenie — wtedy `loop()` musi kończyć się sam,
         * a odpowiedzialność za czas wykonania spada na autora skryptu.
         */
        u32 budget = 20000;

        /**
         * Silnik wykonujący skrypt. Wymagany — moduł nie ma silnika domyślnego
         * i nie tworzy żadnego sam, bo to oznaczałoby alokację i wybór za
         * użytkownika. Musi przeżyć moduł.
         *
         *     script::LuaEngine engine;
         *     cfg.engine = &engine;
         */
        IScriptEngine* engine = nullptr;

        BindingSet bindings{};

        /**
         * Własna pula pamięci skryptu. Pusta oznacza pulę domyślną
         * (HYDRA_SCRIPT_HEAP_BYTES) — wystarczającą wszędzie tam, gdzie
         * silnik jest jeden, czyli praktycznie zawsze.
         */
        void*  pool      = nullptr;
        size_t poolBytes = 0;

        /**
         * Jednostka wykonywalna: źródło skryptu albo moduł binarny. Musi
         * przeżyć moduł — zwykle stała w pamięci programu.
         */
        const void* source = nullptr;
        /**
         * Długość obrazu. Zero oznacza tekst zakończony zerem — tak przychodzi
         * skrypt z pamięci programu i dlatego jest wartością domyślną.
         */
        size_t sourceBytes = 0;
        /** Nazwa fragmentu w komunikatach błędów. */
        const char* chunkName = "=main";

        bool callSetup = true;
        bool callLoop  = true;
        /** Ile sygnałów z magistrali obsłużyć w jednym przebiegu. */
        u8 signalsPerCycle = 8;

        /**
         * Po tylu kolejnych błędach `loop()` moduł przestaje ją wołać
         * i zgłasza SysDegraded. Skrypt psujący się co przebieg zalałby log.
         */
        u8 maxConsecutiveErrors = 5;
    };

    struct Stats {
        u32 cycles          = 0;  ///< przebiegi taska
        u32 loopRuns        = 0;  ///< zakończone wywołania loop()
        u32 loopPreemptions = 0;  ///< wywłaszczenia po wyczerpaniu budżetu
        u32 loopErrors      = 0;
        u32 signalsHandled  = 0;
        u32 instructions    = 0;  ///< instrukcje wykonane w wywłaszczonych porcjach
    };

    ScriptModule() : ModuleBase("script") {}

    Status configure(const Config& cfg);

    /**
     * Podmienia skrypt bez restartu urządzenia: zamyka silnik, otwiera nowy
     * i wczytuje podane źródło. Stan poprzedniego skryptu przepada — to jest
     * podmiana, a nie doładowanie.
     */
    Status reload(const char* source, const char* chunkName = "=main");

    /** Wariant dla obrazu binarnego, gdzie długości nie da się wyliczyć. */
    Status reload(const void* image, size_t bytes, const char* name);

    /** Wczytuje od nowa obraz podany w konfiguracji. */
    Status reload();

    /**
     * Silnik podany w konfiguracji. Ważny od `configure()`, nie dopiero od
     * `init()` — komendy shella rejestruje się zwykle wcześniej.
     * Null, dopóki `configure()` nie zostało zawołane.
     */
    IScriptEngine*       engine() { return engine_; }
    const IScriptEngine* engine() const { return engine_; }

    /**
     * Obraz obecnie wczytany — ten z konfiguracji albo ten, który podmienił
     * `reload()`. Dla tekstu długość obejmuje zero kończące, więc wynik nadaje
     * się wprost na argument `reload()`.
     *
     * Pusty span, dopóki nic nie wczytano.
     */
    CByteSpan image() const;

    /** Jeden przebieg pętli. Wystawiony publicznie do testów. */
    void step();

    Stats stats() const { return stats_; }
    /** Czy moduł przestał wołać `loop()` z powodu powtarzających się błędów. */
    bool  loopStopped() const { return loopStopped_; }

protected:
    Status onInit() override;
    Status onStart() override;
    void   onStop() override;

private:
    Status loadImage(const void* image, size_t bytes, const char* name);

    IScriptEngine* engine_ = nullptr;
    Task           task_{};
    Config         cfg_{};
    Stats          stats_{};
    u8     consecutiveErrors_ = 0;
    bool   loopStopped_       = false;
    bool   loaded_            = false;
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
