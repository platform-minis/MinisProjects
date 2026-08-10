#pragma once
/**
 * Hydra — cienka warstwa nad RTOS-em (rozdz. 4, 10).
 *
 * Rdzeń Hydry zależy wyłącznie od tego nagłówka, nigdy od FreeRTOS-a wprost.
 * Dzięki temu ten sam kod rdzenia kompiluje się natywnie na PC (backend hostowy,
 * pthread) — to główny poligon testów jednostkowych (rozdz. 12).
 *
 * Backendy:
 *   src/core/rtos_freertos.cpp — ESP32 / RP2 / STM32
 *   src/core/rtos_host.cpp     — PC (testy jednostkowe)
 *
 * Nagłówek nie zawiera żadnych typów FreeRTOS — uchwyty są nieprzezroczyste.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace rtos {

/** Nieprzezroczysty uchwyt taska. */
using TaskHandle = void*;

/** Punkt wejścia taska. Nie wolno z niego wracać — patrz Task::run(). */
using TaskEntry = void (*)(void*);

struct TaskCfg {
    const char* name       = "task";
    u32         stackWords = HYDRA_DEFAULT_STACK;
    Prio        prio       = Prio::Normal;
    Core        core       = Core::Any;   ///< ignorowane bez SMP
};

/** Tworzy i uruchamia task. Na platformach bez SMP pole core jest ignorowane. */
Result<TaskHandle> spawn(const TaskCfg& cfg, TaskEntry entry, void* arg);

/** Kończy task. nullptr = bieżący task. */
void kill(TaskHandle h);

/** Uśpienie bieżącego taska. */
void delayMs(u32 ms);

/**
 * Uśpienie do stałego momentu — podstawa deterministycznych pętli sterowania.
 * lastWakeTicks wyrażone jest w tyknięciach systemowych (nie w milisekundach)
 * i aktualizowane w miejscu. Zwraca false, gdy deadline został przekroczony
 * (pętla nie zdążyła w swoim okresie) — rozdz. 9.
 */
bool delayUntil(u32& lastWakeTicks, u32 periodMs);

/** Bieżąca wartość licznika tyknięć — wyłącznie jako argument delayUntil. */
u32 tickCount();

/** Konwersje tyknięć na milisekundy. Częstotliwość tyknięć zależy od platformy. */
u32 ticksToMs(u32 ticks);
u32 msToTicks(u32 ms);

/** Czas monotoniczny od startu systemu. */
Millis nowMs();
Micros nowUs();

/** Oddanie procesora taskom o tym samym priorytecie. */
void yield();

/** true, gdy kod wykonuje się w kontekście przerwania. */
bool inIsr();

/**
 * Startuje scheduler. Na ESP32 i arduino-pico to no-op (scheduler już działa),
 * na stm32duino wywołuje vTaskStartScheduler() i nigdy nie wraca (rozdz. 4.1).
 */
void startScheduler();

/**
 * Minimalna wolna przestrzeń stosu taska w bajtach (0 = brak wsparcia).
 * h == nullptr oznacza task bieżący — tak samo jak w API FreeRTOS.
 */
u32 stackHighWaterMark(TaskHandle h);

/** Liczba zarejestrowanych tasków — dla komendy `ps` shella diagnostycznego. */
u32 taskCount();

/** Wolna pamięć sterty w bajtach. 0 = platforma nie udostępnia tej informacji. */
u32 freeHeapBytes();

/**
 * Mutex z dziedziczeniem priorytetów (rozdz. 10: zakaz semaforów binarnych
 * w roli mutexu). Rekurencja niedozwolona.
 */
class Mutex : NonCopyable {
public:
    Mutex();
    ~Mutex();
    /** Blokuje do skutku albo do timeoutu. timeoutMs = 0xFFFFFFFF → bez limitu. */
    bool lock(u32 timeoutMs = 0xFFFFFFFFu);
    bool tryLock();
    void unlock();
    bool valid() const { return h_ != nullptr; }

private:
    void* h_ = nullptr;
};

/** RAII dla Mutex — jedyny sposób zdejmowania blokady w kodzie Hydry. */
class LockGuard : NonCopyable {
public:
    explicit LockGuard(Mutex& m, u32 timeoutMs = 0xFFFFFFFFu)
        : m_(m), held_(m.lock(timeoutMs)) {}
    ~LockGuard() { if (held_) m_.unlock(); }
    bool held() const { return held_; }

private:
    Mutex& m_;
    bool   held_;
};

/**
 * Kolejka o stałym rozmiarze elementu — jedyny mechanizm przekazywania danych
 * między taskami poza EventBusem (rozdz. 10).
 */
class Queue : NonCopyable {
public:
    Queue() = default;
    ~Queue();

    /** Alokuje kolejkę. Wywoływane wyłącznie w fazie inicjalizacji (rozdz. 11). */
    Status create(u32 length, u32 itemSize);
    void   destroy();

    bool send(const void* item, u32 timeoutMs = 0);
    bool receive(void* out, u32 timeoutMs = 0xFFFFFFFFu);

    /** Wariant ISR-safe. woken=true → po powrocie z ISR potrzebny yield. */
    bool sendFromIsr(const void* item, bool* woken);

    u32  waiting() const;
    bool valid() const { return h_ != nullptr; }

private:
    void* h_        = nullptr;
    u32   itemSize_ = 0;
};

/**
 * Timer programowy.
 *
 * Do tej pory rzadkie zdarzenia okresowe robiło się taskiem z `delayUntil`,
 * co za każdy taki zegar płaci osobnym stosem — kilkaset bajtów RAM-u po to,
 * żeby raz na minutę coś sprawdzić. Timer wykonuje się w kontekście
 * współdzielonym, więc koszt jest jednorazowy.
 *
 * Wywołanie zwrotne biegnie w zadaniu obsługi timerów, nie w przerwaniu —
 * ale to zadanie ma wysoki priorytet i obsługuje wszystkie timery po kolei,
 * więc obowiązuje ta sama zasada co w ISR: krótko i bez blokowania. Długą
 * robotę publikuje się na EventBus (rozdz. 10).
 */
class Timer : NonCopyable {
public:
    using Callback = void (*)(void*);

    Timer() = default;
    ~Timer();

    /**
     * Tworzy timer. Wywoływane wyłącznie w fazie inicjalizacji (rozdz. 11).
     *
     * `periodic = false` daje timer jednorazowy — po wystrzeleniu trzeba go
     * uzbroić ponownie przez `start()`.
     */
    Status create(const char* name, u32 periodMs, bool periodic,
                  Callback callback, void* arg);
    void   destroy();

    /** Uzbraja albo przezbraja timer. Dla jednorazowego liczy od teraz. */
    bool start(u32 timeoutMs = 0);

    /**
     * Wyrejestrowuje timer z kolejki. Po powrocie nie padnie **nowe** wywołanie
     * zwrotne — ale to, które już się zaczęło, biegnie do końca. Tak działa
     * `xTimerStop` we FreeRTOS-ie i tak samo zachowuje się backend hostowy.
     *
     * Kto potrzebuje pewności, że callback nie dotyka już jego danych, ma
     * zsynchronizować się sam. Zatrzymanie czekające na zakończenie wywołania
     * zwrotnego zakleszczyłoby się przy `stop()` wołanym z jego wnętrza.
     */
    bool stop(u32 timeoutMs = 0);

    /** Zmienia okres i uzbraja. Okres 0 jest odrzucany — nie zatrzymuje timera. */
    bool setPeriod(u32 periodMs, u32 timeoutMs = 0);

    bool running() const;
    bool valid() const { return h_ != nullptr; }

private:
    void* h_ = nullptr;
};

/** Krytyczna sekcja ISR-safe — używana wyłącznie w rdzeniu, na krótkich odcinkach. */
class CriticalSection : NonCopyable {
public:
    CriticalSection();
    ~CriticalSection();
private:
    /** Zapamiętana maska przerwań; wykorzystywana wyłącznie przez backend FreeRTOS. */
    [[maybe_unused]] u32 state_ = 0;
};

}  // namespace rtos
}  // namespace hydra
