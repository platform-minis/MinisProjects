#pragma once
/**
 * Hydra — task z egzekwowanym okresem i diagnostyką (rozdz. 4.2, 9, 10).
 *
 * Pętle okresowe (motion.control, sense.poll, ui.render, core.house) używają
 * delayUntil, więc okres nie dryfuje wraz z czasem wykonania ciała pętli.
 * Spóźniona iteracja inkrementuje licznik diagnostyczny, a po przekroczeniu
 * progu wyzwala zdarzenie TaskDeadlineMissed (rozdz. 9).
 *
 * Zatrzymanie jest kooperatywne: requestStop() ustawia flagę, task kończy
 * bieżącą iterację i wychodzi sam. Nigdy nie zabijamy taska z zewnątrz —
 * to jedyny sposób, by zwolnił swoje zasoby.
 */

#include <atomic>

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {

class Task : NonCopyable {
public:
    /** Ciało pętli okresowej — jedna iteracja. */
    using Body = Delegate<void()>;
    /** Ciało taska zdarzeniowego — własna pętla, kończy się gdy stopRequested(). */
    using Loop = Delegate<void(Task&)>;

    struct Cfg {
        const char* name       = "task";
        Prio        prio       = Prio::Normal;
        Core        core       = Core::Any;
        u32         stackWords = HYDRA_DEFAULT_STACK;
        /** Ile spóźnionych iteracji z rzędu przed zgłoszeniem zdarzenia. */
        u32         missThreshold = 3;
    };

    /**
     * Migawka diagnostyczna. Pola zapisuje wyłącznie sam task, a czyta je
     * diagnostyka — pojedyncze pole jest zawsze spójne, ale cała struktura
     * może pochodzić z dwóch sąsiednich iteracji. Synchronizacja kosztowałaby
     * sekcję krytyczną w każdej iteracji pętli czasu rzeczywistego.
     */
    struct Stats {
        u32 iterations      = 0;
        u32 deadlineMisses  = 0;
        u32 maxOverrunMs    = 0;
        u32 lastDurationUs  = 0;
        u32 maxDurationUs   = 0;
        u32 stackFreeBytes  = 0;
    };

    Task() = default;
    ~Task();

    /**
     * Uruchamia pętlę okresową o zadanym okresie.
     * periodMs musi być > 0; dla motion.control typowo 1–5 ms.
     */
    Status startPeriodic(const Cfg& cfg, u32 periodMs, Body body);

    /** Uruchamia task zdarzeniowy — pętla jest po stronie ciała (np. pump Inboxa). */
    Status startEventLoop(const Cfg& cfg, Loop loop);

    /** Prosi task o zakończenie. Nie blokuje. */
    void requestStop() { stopRequested_ = true; }
    bool stopRequested() const { return stopRequested_; }

    /** Prosi o zakończenie i czeka. false = task nie wyszedł w zadanym czasie. */
    bool stopAndWait(u32 timeoutMs = 1000);

    bool  running() const { return running_; }
    Stats stats() const;
    u16   id() const { return id_; }
    const char* name() const { return cfg_.name; }
    u32   periodMs() const { return periodMs_; }

    /**
     * Rejestr żywych tasków — podstawa komendy `ps` shella diagnostycznego
     * i raportowania high-water-marków stosów w telemetrii (rozdz. 10, 13).
     */
    static u8    registered();
    static Task* at(u8 index);

private:
    void enroll();
    void withdraw();

    static void trampoline(void* arg);
    void runPeriodic();
    void runEventLoop();

    Cfg              cfg_{};
    u32              periodMs_ = 0;
    Body             body_{};
    Loop             loop_{};
    rtos::TaskHandle handle_ = nullptr;
    u16              id_     = 0;

    /**
     * Flagi wymieniane między taskiem a jego właścicielem. std::atomic, nie
     * volatile: na wszystkich platformach docelowych zapis i odczyt bool-a to
     * pojedyncza instrukcja (bez wywołań libatomic, bo nie ma tu operacji
     * odczyt-modyfikacja-zapis), więc poprawność dostajemy za darmo.
     */
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};

    u32  consecutiveMisses_ = 0;
    u32  lastReportMs_      = 0;
    bool everReported_      = false;  ///< pierwsze naruszenie zgłaszamy natychmiast
    Stats stats_{};
};

}  // namespace hydra
