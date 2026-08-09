/**
 * Hydra — backend RTOS dla hosta (POSIX/pthread).
 *
 * Pozwala kompilować i uruchamiać rdzeń oraz logikę modułów natywnie na PC.
 * To główny poligon testów jednostkowych (rozdz. 12) — kod rdzenia nigdy nie
 * widzi FreeRTOS-a wprost, więc ten sam plik obsługuje macOS i Linuksa w CI.
 *
 * Semantyka celowo odwzorowuje FreeRTOS:
 *   - priorytety są mapowane na polityki schedulera tylko jeśli proces ma do
 *     tego uprawnienia; w przeciwnym razie są ignorowane (testy nie zależą od
 *     priorytetów, a wymaganie roota psułoby CI),
 *   - delayUntil raportuje przekroczenie okresu tak samo jak vTaskDelayUntil,
 *   - inIsr() zawsze zwraca false — na hoście nie ma przerwań.
 */

#include <new>
#include "hydra/core/Config.hpp"

#if HYDRA_PLAT_HOST

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace rtos {
namespace {

constexpr u32 kForever = 0xFFFFFFFFu;

/*
 * Dolna granica stosu wątku.
 *
 * `PTHREAD_STACK_MIN` to rozszerzenie POSIX — winpthreads (mingw) go nie
 * definiuje, a na Linuksie bywa makrem wołającym `sysconf`, więc nie nadaje
 * się na wyrażenie stałe. Bierzemy wartość z widełek, w których i tak mieści
 * się każda z platform (glibc: 16 kB na aarch64), bo chodzi wyłącznie o to,
 * żeby zbyt mała wartość z konfiguracji taska nie przeszła do systemu.
 */
constexpr size_t kMinStackBytes = 16u * 1024u;

struct HostTask {
    pthread_t   thread;
    TaskEntry   entry;
    void*       arg;
    char        name[24];
    bool        alive;
};

/** Licznik żywych tasków — dla taskCount(). Chroniony przez gCountMutex. */
pthread_mutex_t gCountMutex = PTHREAD_MUTEX_INITIALIZER;
u32             gTaskCount  = 0;

/** Sekcja krytyczna: na hoście jeden globalny mutex rekurencyjny. */
pthread_mutex_t gCritical;
pthread_once_t  gCriticalOnce = PTHREAD_ONCE_INIT;

void initCritical() {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&gCritical, &a);
    pthread_mutexattr_destroy(&a);
}

Micros monotonicUs() {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    ts.tv_sec  = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000;
#endif
    return static_cast<Micros>(ts.tv_sec) * 1000000ull + static_cast<Micros>(ts.tv_nsec) / 1000ull;
}

/** Chwila startu procesu — czas Hydry liczy się od zera, jak od resetu MCU. */
const Micros gBootUs = monotonicUs();

void sleepUs(u64 us) {
    struct timespec ts;
    ts.tv_sec  = static_cast<time_t>(us / 1000000ull);
    ts.tv_nsec = static_cast<long>((us % 1000000ull) * 1000ull);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

/** Czas bezwzględny dla pthread_cond_timedwait (zegar realtime). */
void absTime(u32 ms, struct timespec* out) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    out->tv_sec  = tv.tv_sec + static_cast<time_t>(ms / 1000);
    out->tv_nsec = static_cast<long>(tv.tv_usec) * 1000L + static_cast<long>(ms % 1000) * 1000000L;
    if (out->tv_nsec >= 1000000000L) {
        out->tv_sec += 1;
        out->tv_nsec -= 1000000000L;
    }
}

/** Deskryptor bieżącego wątku — pozwala zwolnić go przy wyjściu przez kill(nullptr). */
thread_local HostTask* gSelf = nullptr;

/** Wyrejestrowanie i zwolnienie deskryptora własnego wątku. */
void finishSelf() {
    HostTask* t = gSelf;
    if (!t) return;
    gSelf = nullptr;
    pthread_mutex_lock(&gCountMutex);
    if (t->alive) {
        t->alive = false;
        if (gTaskCount) --gTaskCount;
    }
    pthread_mutex_unlock(&gCountMutex);
    free(t);
}

void* trampoline(void* p) {
    auto* t = static_cast<HostTask*>(p);
    gSelf   = t;
    t->entry(t->arg);
    finishSelf();  // ścieżka dla ciała, które po prostu wróciło z entry
    return nullptr;
}

/** Wewnętrzna kolejka: bufor pierścieniowy + dwie zmienne warunkowe. */
struct HostQueue {
    pthread_mutex_t m;
    pthread_cond_t  notEmpty;
    pthread_cond_t  notFull;
    u8*             buf;
    u32             itemSize;
    u32             capacity;
    u32             head;   ///< indeks do odczytu
    u32             tail;   ///< indeks do zapisu
    u32             count;
};

}  // namespace

// ---------------------------------------------------------------------------
// Taski
// ---------------------------------------------------------------------------

Result<TaskHandle> spawn(const TaskCfg& cfg, TaskEntry entry, void* arg) {
    if (!entry) return unexpected(Err::BadArgument);

    auto* t = static_cast<HostTask*>(calloc(1, sizeof(HostTask)));
    if (!t) return unexpected(Err::OutOfMemory);

    t->entry = entry;
    t->arg   = arg;
    t->alive = true;
    strncpy(t->name, cfg.name ? cfg.name : "task", sizeof(t->name) - 1);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Wątki tworzymy odłączone: odpowiednikiem join na FreeRTOS jest
    // kooperatywne zakończenie taska, a nikt tu nie odbiera kodu powrotu.
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    // Stos podajemy w bajtach; na FreeRTOS stackWords to słowa maszynowe.
    const size_t bytes = static_cast<size_t>(cfg.stackWords) * sizeof(void*);
    const size_t floor = kMinStackBytes * 2;
    pthread_attr_setstacksize(&attr, bytes < floor ? floor : bytes);

    pthread_mutex_lock(&gCountMutex);
    ++gTaskCount;
    pthread_mutex_unlock(&gCountMutex);

    const int rc = pthread_create(&t->thread, &attr, trampoline, t);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        pthread_mutex_lock(&gCountMutex);
        if (gTaskCount) --gTaskCount;
        pthread_mutex_unlock(&gCountMutex);
        free(t);
        return unexpected(Err::OutOfMemory);
    }
    return static_cast<TaskHandle>(t);
}

void kill(TaskHandle h) {
    if (!h || h == static_cast<TaskHandle>(gSelf)) {
        // Zakończenie własnego taska — odpowiednik vTaskDelete(NULL).
        finishSelf();
        pthread_exit(nullptr);
    }
    // Zabicie cudzego wątku jest na hoście niewykonalne bezpiecznie: pthread
    // nie ma odpowiednika vTaskDelete(h), a pthread_cancel zostawiłby
    // zablokowane mutexy. Wątek i tak kończy się kooperatywnie
    // (Task::requestStop) i wtedy sam zwalnia swój deskryptor — tutaj
    // wypisujemy go tylko z rejestru.
    auto* t = static_cast<HostTask*>(h);
    pthread_mutex_lock(&gCountMutex);
    if (t->alive) {
        t->alive = false;
        if (gTaskCount) --gTaskCount;
    }
    pthread_mutex_unlock(&gCountMutex);
}

void delayMs(u32 ms) { sleepUs(static_cast<u64>(ms) * 1000ull); }

bool delayUntil(u32& lastWakeTicks, u32 periodMs) {
    const u32 target = lastWakeTicks + periodMs;
    const u32 now    = tickCount();
    // Porównanie odporne na przepełnienie licznika (jak w FreeRTOS).
    if (static_cast<i32>(now - target) >= 0) {
        lastWakeTicks = now;  // resynchronizacja — okres został przekroczony
        return false;
    }
    sleepUs(static_cast<u64>(target - now) * 1000ull);
    lastWakeTicks = target;
    return true;
}

u32    tickCount() { return static_cast<u32>((monotonicUs() - gBootUs) / 1000ull); }
Millis nowMs()     { return tickCount(); }
Micros nowUs()     { return monotonicUs() - gBootUs; }

// Na hoście tyknięcie to milisekunda — konwersje są tożsamościowe.
u32 ticksToMs(u32 ticks) { return ticks; }
u32 msToTicks(u32 ms)    { return ms; }

void yield() { sched_yield(); }
bool inIsr() { return false; }

void startScheduler() {
    // Na hoście wątki działają od momentu utworzenia — nic do zrobienia.
}

u32 stackHighWaterMark(TaskHandle) {
    return 0;  // pthread nie udostępnia tej informacji przenośnie
}

u32 taskCount() {
    pthread_mutex_lock(&gCountMutex);
    const u32 n = gTaskCount;
    pthread_mutex_unlock(&gCountMutex);
    return n;
}

u32 freeHeapBytes() {
    return 0;  // host ma pamięć wirtualną — liczba nie miałaby sensu
}

// ---------------------------------------------------------------------------
// Mutex
// ---------------------------------------------------------------------------

Mutex::Mutex() {
    auto* m = static_cast<pthread_mutex_t*>(malloc(sizeof(pthread_mutex_t)));
    if (!m) return;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
#if defined(PTHREAD_PRIO_INHERIT)
    // Odpowiednik dziedziczenia priorytetów z FreeRTOS (rozdz. 10).
    // Na części systemów niedostępne — wtedy zostaje mutex zwykły.
    pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_INHERIT);
#endif
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    h_ = m;
}

Mutex::~Mutex() {
    if (!h_) return;
    auto* m = static_cast<pthread_mutex_t*>(h_);
    pthread_mutex_destroy(m);
    free(m);
    h_ = nullptr;
}

bool Mutex::lock(u32 timeoutMs) {
    if (!h_) return false;
    auto* m = static_cast<pthread_mutex_t*>(h_);
    if (timeoutMs == kForever) return pthread_mutex_lock(m) == 0;
    // macOS nie ma pthread_mutex_timedlock — odpytujemy z krótkim odstępem.
    const u32 deadline = tickCount() + timeoutMs;
    for (;;) {
        if (pthread_mutex_trylock(m) == 0) return true;
        if (static_cast<i32>(tickCount() - deadline) >= 0) return false;
        sleepUs(200);
    }
}

bool Mutex::tryLock() {
    return h_ && pthread_mutex_trylock(static_cast<pthread_mutex_t*>(h_)) == 0;
}

void Mutex::unlock() {
    if (h_) pthread_mutex_unlock(static_cast<pthread_mutex_t*>(h_));
}

// ---------------------------------------------------------------------------
// Kolejka
// ---------------------------------------------------------------------------

Queue::~Queue() { destroy(); }

Status Queue::create(u32 length, u32 itemSize) {
    if (h_) return fail(Err::AlreadyExists);
    if (!length || !itemSize) return fail(Err::BadArgument);

    auto* q = static_cast<HostQueue*>(calloc(1, sizeof(HostQueue)));
    if (!q) return fail(Err::OutOfMemory);
    q->buf = static_cast<u8*>(calloc(length, itemSize));
    if (!q->buf) {
        free(q);
        return fail(Err::OutOfMemory);
    }
    pthread_mutex_init(&q->m, nullptr);
    pthread_cond_init(&q->notEmpty, nullptr);
    pthread_cond_init(&q->notFull, nullptr);
    q->itemSize = itemSize;
    q->capacity = length;

    h_        = q;
    itemSize_ = itemSize;
    return ok();
}

void Queue::destroy() {
    if (!h_) return;
    auto* q = static_cast<HostQueue*>(h_);
    pthread_mutex_lock(&q->m);
    pthread_cond_broadcast(&q->notEmpty);
    pthread_cond_broadcast(&q->notFull);
    pthread_mutex_unlock(&q->m);
    pthread_cond_destroy(&q->notEmpty);
    pthread_cond_destroy(&q->notFull);
    pthread_mutex_destroy(&q->m);
    free(q->buf);
    free(q);
    h_        = nullptr;
    itemSize_ = 0;
}

bool Queue::send(const void* item, u32 timeoutMs) {
    if (!h_ || !item) return false;
    auto* q = static_cast<HostQueue*>(h_);
    pthread_mutex_lock(&q->m);

    if (q->count == q->capacity) {
        if (timeoutMs == 0) {
            pthread_mutex_unlock(&q->m);
            return false;
        }
        struct timespec abs;
        absTime(timeoutMs, &abs);
        while (q->count == q->capacity) {
            const int rc = (timeoutMs == kForever)
                               ? pthread_cond_wait(&q->notFull, &q->m)
                               : pthread_cond_timedwait(&q->notFull, &q->m, &abs);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->m);
                return false;
            }
        }
    }

    memcpy(q->buf + static_cast<size_t>(q->tail) * q->itemSize, item, q->itemSize);
    q->tail = (q->tail + 1) % q->capacity;
    ++q->count;
    pthread_cond_signal(&q->notEmpty);
    pthread_mutex_unlock(&q->m);
    return true;
}

bool Queue::receive(void* out, u32 timeoutMs) {
    if (!h_ || !out) return false;
    auto* q = static_cast<HostQueue*>(h_);
    pthread_mutex_lock(&q->m);

    if (q->count == 0) {
        if (timeoutMs == 0) {
            pthread_mutex_unlock(&q->m);
            return false;
        }
        struct timespec abs;
        absTime(timeoutMs, &abs);
        while (q->count == 0) {
            const int rc = (timeoutMs == kForever)
                               ? pthread_cond_wait(&q->notEmpty, &q->m)
                               : pthread_cond_timedwait(&q->notEmpty, &q->m, &abs);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->m);
                return false;
            }
        }
    }

    memcpy(out, q->buf + static_cast<size_t>(q->head) * q->itemSize, q->itemSize);
    q->head = (q->head + 1) % q->capacity;
    --q->count;
    pthread_cond_signal(&q->notFull);
    pthread_mutex_unlock(&q->m);
    return true;
}

bool Queue::sendFromIsr(const void* item, bool* woken) {
    if (woken) *woken = false;
    return send(item, 0);  // na hoście nie ma kontekstu przerwania
}

u32 Queue::waiting() const {
    if (!h_) return 0;
    auto* q = static_cast<HostQueue*>(h_);
    pthread_mutex_lock(&q->m);
    const u32 n = q->count;
    pthread_mutex_unlock(&q->m);
    return n;
}

// ---------------------------------------------------------------------------
// Sekcja krytyczna
// ---------------------------------------------------------------------------
// Timer
//
// Host nie ma jądra, które obsłużyłoby timery, więc każdy dostaje własny
// wątek śpiący do następnego wystrzelenia. Na mikrokontrolerze byłoby to
// marnotrawstwo, przed którym Timer ma chronić — tutaj pamięci jest pod
// dostatkiem, a chodzi o zgodność zachowania, nie o oszczędność.

namespace {

struct HostTimer {
    Timer::Callback callback = nullptr;
    void*           arg      = nullptr;
    u32             periodMs = 0;
    bool            periodic = false;
    bool            running  = false;
    bool            stopping = false;
    /** Rośnie przy każdym uzbrojeniu — po tym wątek poznaje przezbrojenie
     *  timera, którego stan `running` się nie zmienił. */
    u32             generation = 0;
    pthread_t       thread{};
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
};

/** Czeka `ms` albo do zmiany stanu — czekanie na zmiennej warunkowej,
 *  żeby `stop()` i `destroy()` nie musiały czekać na koniec okresu. */
bool waitOrWake(HostTimer& t, u32 ms) {
    struct timespec deadline{};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec  += static_cast<time_t>(ms / 1000u);
    deadline.tv_nsec += static_cast<long>((ms % 1000u) * 1000000L);
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec += 1; deadline.tv_nsec -= 1000000000L; }

    const bool wasRunning  = t.running;
    const u32  atGeneration = t.generation;
    while (t.running == wasRunning && t.generation == atGeneration && !t.stopping) {
        if (pthread_cond_timedwait(&t.cond, &t.mutex, &deadline) == ETIMEDOUT) return true;
    }
    return false;   // zatrzymany albo przezbrojony — okres nie dobiegł końca
}

void* timerThread(void* arg) {
    auto* t = static_cast<HostTimer*>(arg);
    pthread_mutex_lock(&t->mutex);
    for (;;) {
        while (!t->running && !t->stopping) pthread_cond_wait(&t->cond, &t->mutex);
        if (t->stopping) break;

        const u32 period = t->periodMs;
        if (!waitOrWake(*t, period)) continue;   // zatrzymany albo przezbrojony
        if (t->stopping) break;

        const Timer::Callback callback = t->callback;
        void* const           userArg  = t->arg;
        if (!t->periodic) t->running = false;

        // Wywołanie poza blokadą: gdyby callback sięgnął po start()/stop(),
        // trzymanie mutexu byłoby zakleszczeniem.
        pthread_mutex_unlock(&t->mutex);
        if (callback) callback(userArg);
        pthread_mutex_lock(&t->mutex);
    }
    pthread_mutex_unlock(&t->mutex);
    return nullptr;
}

}  // namespace

Timer::~Timer() { destroy(); }

Status Timer::create(const char* name, u32 periodMs, bool periodic,
                     Callback callback, void* arg) {
    (void) name;   // host nie nazywa wątków timerów — nazwa służy diagnostyce na MCU
    if (h_) return fail(Err::AlreadyExists);
    if (!callback || periodMs == 0) return fail(Err::BadArgument);

    auto* t = new (std::nothrow) HostTimer{};
    if (!t) return fail(Err::OutOfMemory);
    t->callback = callback;
    t->arg      = arg;
    t->periodMs = periodMs;
    t->periodic = periodic;

    if (pthread_create(&t->thread, nullptr, timerThread, t) != 0) {
        delete t;
        return fail(Err::OutOfMemory);
    }
    h_ = t;
    return ok();
}

void Timer::destroy() {
    if (!h_) return;
    auto* t = static_cast<HostTimer*>(h_);

    pthread_mutex_lock(&t->mutex);
    t->stopping = true;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->mutex);

    pthread_join(t->thread, nullptr);
    delete t;
    h_ = nullptr;
}

bool Timer::start(u32 timeoutMs) {
    (void) timeoutMs;   // host nie kolejkuje poleceń — nie ma na co czekać
    if (!h_) return false;
    auto* t = static_cast<HostTimer*>(h_);
    pthread_mutex_lock(&t->mutex);
    t->running = true;
    t->generation += 1;          // także dla już biegnącego: liczy od nowa
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->mutex);
    return true;
}

bool Timer::stop(u32 timeoutMs) {
    (void) timeoutMs;
    if (!h_) return false;
    auto* t = static_cast<HostTimer*>(h_);
    pthread_mutex_lock(&t->mutex);
    t->running = false;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->mutex);
    return true;
}

bool Timer::setPeriod(u32 periodMs, u32 timeoutMs) {
    (void) timeoutMs;
    if (!h_ || periodMs == 0) return false;
    auto* t = static_cast<HostTimer*>(h_);
    pthread_mutex_lock(&t->mutex);
    t->periodMs = periodMs;
    t->running  = true;
    t->generation += 1;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->mutex);
    return true;
}

bool Timer::running() const {
    if (!h_) return false;
    auto* t = static_cast<HostTimer*>(h_);
    pthread_mutex_lock(&t->mutex);
    const bool value = t->running;
    pthread_mutex_unlock(&t->mutex);
    return value;
}

// ---------------------------------------------------------------------------

CriticalSection::CriticalSection() {
    pthread_once(&gCriticalOnce, initCritical);
    pthread_mutex_lock(&gCritical);
}

CriticalSection::~CriticalSection() { pthread_mutex_unlock(&gCritical); }

}  // namespace rtos
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST
