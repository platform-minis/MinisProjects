/**
 * Hydra — implementacja taska (rozdz. 4.2, 9).
 *
 * Okres jest egzekwowany przez rtos::delayUntil, więc nie dryfuje wraz z czasem
 * wykonania ciała pętli. Gdy iteracja nie zmieści się w okresie, delayUntil
 * zwraca false — wtedy rośnie licznik naruszeń, a po missThreshold spóźnieniach
 * z rzędu magistrala dostaje TaskDeadlineMissed. Zgłoszenia są ograniczone do
 * jednego na sekundę, żeby przeciążony task nie zalał EventBusa.
 */

#include "hydra/core/Task.hpp"

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("core.task")

namespace hydra {
namespace {
/** Minimalny odstęp między zgłoszeniami naruszenia deadline'u. */
constexpr u32 kReportPeriodMs = 1000;

/** Rejestr żywych tasków (rozdz. 13, komenda `ps`). */
constexpr u8 kMaxRegistered = 16;
Task*        gRegistry[kMaxRegistered] = {};
u8           gRegistered               = 0;
}  // namespace

void Task::enroll() {
    rtos::CriticalSection cs;
    for (u8 i = 0; i < kMaxRegistered; ++i) {
        if (gRegistry[i] != nullptr) continue;
        gRegistry[i] = this;
        if (i >= gRegistered) gRegistered = static_cast<u8>(i + 1);
        return;
    }
}

void Task::withdraw() {
    rtos::CriticalSection cs;
    for (u8 i = 0; i < gRegistered; ++i) {
        if (gRegistry[i] == this) gRegistry[i] = nullptr;
    }
    while (gRegistered > 0 && gRegistry[gRegistered - 1] == nullptr) --gRegistered;
}

u8 Task::registered() {
    rtos::CriticalSection cs;
    u8 n = 0;
    for (u8 i = 0; i < gRegistered; ++i) {
        if (gRegistry[i]) ++n;
    }
    return n;
}

Task* Task::at(u8 index) {
    rtos::CriticalSection cs;
    u8 n = 0;
    for (u8 i = 0; i < gRegistered; ++i) {
        if (!gRegistry[i]) continue;
        if (n == index) return gRegistry[i];
        ++n;
    }
    return nullptr;
}

Task::~Task() {
    if (running_) stopAndWait(500);
}

Status Task::startPeriodic(const Cfg& cfg, u32 periodMs, Body body) {
    if (running_) return fail(Err::AlreadyExists);
    if (periodMs == 0 || !body) return fail(Err::BadArgument);

    cfg_       = cfg;
    id_        = nameId(cfg.name ? cfg.name : "task");
    body_      = body;
    loop_.reset();
    stats_     = Stats{};
    consecutiveMisses_ = 0;
    lastReportMs_      = 0;
    everReported_      = false;
    stopRequested_ = false;
    finished_      = false;
    // Okres musi być ustawiony, zanim task ruszy — inaczej pierwsza iteracja
    // mogłaby zobaczyć zero i zapętlić się bez uśpienia.
    periodMs_  = periodMs;
    running_   = true;

    rtos::TaskCfg rc;
    rc.name       = cfg.name;
    rc.stackWords = cfg.stackWords;
    rc.prio       = cfg.prio;
    rc.core       = cfg.core;

    enroll();
    auto h = rtos::spawn(rc, &Task::trampoline, this);
    if (!h) {
        withdraw();
        running_  = false;
        periodMs_ = 0;
        return fail(h.error());
    }
    handle_ = *h;
    return ok();
}

Status Task::startEventLoop(const Cfg& cfg, Loop loop) {
    if (running_) return fail(Err::AlreadyExists);
    if (!loop) return fail(Err::BadArgument);

    cfg_       = cfg;
    id_        = nameId(cfg.name ? cfg.name : "task");
    loop_      = loop;
    body_.reset();
    stats_     = Stats{};
    periodMs_  = 0;
    stopRequested_ = false;
    finished_      = false;
    running_       = true;

    rtos::TaskCfg rc;
    rc.name       = cfg.name;
    rc.stackWords = cfg.stackWords;
    rc.prio       = cfg.prio;
    rc.core       = cfg.core;

    enroll();
    auto h = rtos::spawn(rc, &Task::trampoline, this);
    if (!h) {
        withdraw();
        running_ = false;
        return fail(h.error());
    }
    handle_ = *h;
    return ok();
}

void Task::trampoline(void* arg) {
    auto* self = static_cast<Task*>(arg);
    if (self->periodMs_ > 0) {
        self->runPeriodic();
    } else {
        self->runEventLoop();
    }
    self->withdraw();
    self->running_  = false;
    self->finished_ = true;
    rtos::kill(nullptr);  // task kończy się sam — nigdy nie zabijamy go z zewnątrz
}

void Task::runPeriodic() {
    u32 lastWake = rtos::tickCount();

    while (!stopRequested_) {
        const Micros t0 = rtos::nowUs();
        body_();
        const u32 durUs = static_cast<u32>(rtos::nowUs() - t0);

        {
            rtos::CriticalSection cs;
            ++stats_.iterations;
            stats_.lastDurationUs = durUs;
            if (durUs > stats_.maxDurationUs) stats_.maxDurationUs = durUs;
        }
        // nullptr = task bieżący. Czytanie handle_ byłoby wyścigiem: pole
        // zapisuje właściciel dopiero po powrocie ze spawn(), a pierwsza
        // iteracja może wykonać się wcześniej.
        {
            rtos::CriticalSection cs;
            stats_.stackFreeBytes = rtos::stackHighWaterMark(nullptr);
        }

        const u32  target = lastWake + periodMs_;
        const bool onTime = rtos::delayUntil(lastWake, periodMs_);

        if (onTime) {
            consecutiveMisses_ = 0;
            continue;
        }

        // Po spóźnieniu delayUntil resynchronizuje lastWake do bieżącej chwili,
        // więc różnica względem planowanego momentu to wielkość przekroczenia
        // (w tyknięciach — na FreeRTOS tyknięcie nie musi być milisekundą).
        const i32 late    = static_cast<i32>(lastWake - target);
        const u32 overrun = late > 0 ? rtos::ticksToMs(static_cast<u32>(late)) : 0;
        // Blokada obejmuje wyłącznie zapis statystyk. Rozciągnięcie jej na
        // publikację zdarzenia zakleszczyłoby task: magistrala sięga po tę
        // samą sekcję krytyczną, a ta nie jest wznawialna.
        u32 misses = 0;
        {
            rtos::CriticalSection cs;
            ++stats_.deadlineMisses;
            if (overrun > stats_.maxOverrunMs) stats_.maxOverrunMs = overrun;
            misses = stats_.deadlineMisses;
        }
        ++consecutiveMisses_;

        if (consecutiveMisses_ >= cfg_.missThreshold) {
            const u32 now = rtos::nowMs();
            // Pierwsze naruszenie zgłaszamy od razu — najczęściej zdarzają się
            // tuż po starcie, więc czekanie na upływ okna wyciszenia gubiłoby
            // dokładnie te przypadki, dla których ten mechanizm powstał.
            if (!everReported_ || now - lastReportMs_ >= kReportPeriodMs) {
                everReported_ = true;
                lastReportMs_ = now;
                EventBus::publish(TaskDeadlineMissed{
                    id_, static_cast<u16>(overrun > 0xFFFF ? 0xFFFF : overrun),
                    misses});
                HYDRA_LOGW("%s: przekroczony deadline o %lums (razem %lu)", cfg_.name,
                           static_cast<unsigned long>(overrun),
                           static_cast<unsigned long>(misses));
            }
        }
    }
}

void Task::runEventLoop() {
    loop_(*this);
    {
        rtos::CriticalSection cs;
        stats_.stackFreeBytes = rtos::stackHighWaterMark(nullptr);
    }
}

bool Task::stopAndWait(u32 timeoutMs) {
    if (!running_ && !handle_) return true;
    requestStop();

    const u32 deadline = rtos::nowMs() + timeoutMs;
    while (!finished_) {
        if (static_cast<i32>(rtos::nowMs() - deadline) >= 0) {
            HYDRA_LOGE("%s: nie zakończył się w %lums", cfg_.name,
                       static_cast<unsigned long>(timeoutMs));
            return false;
        }
        rtos::delayMs(1);
    }
    handle_ = nullptr;  // deskryptor zwalnia się sam w rtos::kill(nullptr)
    return true;
}

/**
 * Odczyt statystyk. Wołany z innego wątku niż ten, który je zapisuje —
 * komenda `ps` w shellu czyta je z konsoli, a task właśnie kończy iterację.
 * Bez sekcji krytycznej struktura kopiuje się w trakcie zapisu i `ps` pokazuje
 * mieszankę dwóch chwil: liczbę iteracji z jednej, czas trwania z drugiej.
 *
 * Wykryte przez wykrywanie wyścigów na Linuksie; wersja na macOS tego nie
 * zgłaszała, bo tam sekcja krytyczna hosta wygląda inaczej.
 */
Task::Stats Task::stats() const {
    rtos::CriticalSection cs;
    return stats_;
}

}  // namespace hydra
