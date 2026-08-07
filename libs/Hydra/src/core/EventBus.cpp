/**
 * Hydra — implementacja magistrali zdarzeń (rozdz. 4.3).
 *
 * Tablica subskrypcji ma stały rozmiar (HYDRA_EVENT_MAX_SUBS) — po
 * App::begin() magistrala nie alokuje ani bajta (rozdz. 11).
 *
 * Callbacki nigdy nie są wołane pod blokadą tablicy subskrypcji. To celowe:
 * kanoniczny przykład z dokumentacji — subskrybent BatteryEvent publikujący
 * LowPowerEvent — inaczej zakleszczyłby magistralę na własnym mutexie.
 */

#include "hydra/core/EventBus.hpp"

#include <string.h>

namespace hydra {
namespace {

struct Sub {
    EventBus::RawHandler handler{};
    Inbox*  inbox = nullptr;
    TopicId topic = kInvalidTopic;
    u8      size  = 0;
    bool    used  = false;
};

/** Wiadomość w skrzynce subskrybenta — adresowana do konkretnej subskrypcji. */
struct QueuedMsg {
    SubId   sub;
    TopicId topic;
    u8      size;
    u8      data[HYDRA_EVENT_MAX_SIZE];
};

/** Wiadomość odłożona z ISR — rozgłaszana później do wszystkich subskrybentów. */
struct IsrMsg {
    TopicId topic;
    u8      size;
    u8      data[HYDRA_EVENT_MAX_SIZE];
};

struct State {
    rtos::Mutex     mtx;
    rtos::Queue     isrQueue;
    Sub             subs[HYDRA_EVENT_MAX_SUBS];
    EventBus::Stats stats;
    bool            initialized = false;
};

/**
 * Stan magistrali w funkcji-akcesorze, nie w zmiennej globalnej: mutex powstaje
 * przy pierwszym użyciu, a nie w nieokreślonej kolejności inicjalizacji
 * statycznej — na FreeRTOS oznaczałoby to tworzenie obiektów jądra przed main().
 */
State& st() {
    static State s;
    return s;
}

/** Licznik identyfikatorów tematów. 0 jest zarezerwowane jako kInvalidTopic. */
TopicId gNextTopic = 1;

}  // namespace

TopicId nextTopicId() {
    rtos::CriticalSection cs;
    return gNextTopic++;
}

// ---------------------------------------------------------------------------
// Inbox
// ---------------------------------------------------------------------------

Inbox::~Inbox() = default;

Status Inbox::create(u32 depth) {
    if (q_.valid()) return fail(Err::AlreadyExists);
    return q_.create(depth, sizeof(QueuedMsg));
}

u32 Inbox::pump(u32 timeoutMs) {
    if (!q_.valid()) return 0;

    u32       handled = 0;
    QueuedMsg msg;
    u32       wait = timeoutMs;

    while (q_.receive(&msg, wait)) {
        wait = 0;  // pierwsze zdarzenie może poczekać, kolejne bierzemy bez blokowania

        EventBus::RawHandler h;
        bool                 hit = false;
        {
            rtos::LockGuard g(st().mtx);
            if (msg.sub < HYDRA_EVENT_MAX_SUBS) {
                const Sub& s = st().subs[msg.sub];
                // Subskrypcja mogła zostać zdjęta, gdy zdarzenie czekało w kolejce.
                if (s.used && s.topic == msg.topic) {
                    h   = s.handler;
                    hit = true;
                }
            }
        }
        if (hit && h) {
            h(msg.data);
            ++handled;
        }
    }
    return handled;
}

// ---------------------------------------------------------------------------
// EventBus
// ---------------------------------------------------------------------------

Status EventBus::init() {
    State& s = st();
    if (s.initialized) return ok();
    if (!s.mtx.valid()) return fail(Err::OutOfMemory);
    HYDRA_CHECK(s.isrQueue.create(HYDRA_EVENT_ISR_QUEUE_LEN, sizeof(IsrMsg)));
    s.initialized = true;
    return ok();
}

void EventBus::shutdown() {
    State& s = st();
    s.isrQueue.destroy();
    s.initialized = false;
}

Result<SubId> EventBus::addSub(TopicId topic, u8 size, const RawHandler& h, Inbox* inbox) {
    State& s = st();
    if (!h) return unexpected(Err::BadArgument);
    if (inbox && !inbox->valid()) return unexpected(Err::NotInitialized);

    rtos::LockGuard g(s.mtx);
    if (!g.held()) return unexpected(Err::Timeout);

    for (u16 i = 0; i < HYDRA_EVENT_MAX_SUBS; ++i) {
        if (s.subs[i].used) continue;
        s.subs[i].handler = h;
        s.subs[i].inbox   = inbox;
        s.subs[i].topic   = topic;
        s.subs[i].size    = size;
        s.subs[i].used    = true;
        {
            rtos::CriticalSection cs;
            ++s.stats.subs;
        }
        return static_cast<SubId>(i);
    }
    return unexpected(Err::OutOfMemory);
}

void EventBus::unsubscribe(SubId id) {
    if (id >= HYDRA_EVENT_MAX_SUBS) return;
    State& s = st();
    rtos::LockGuard g(s.mtx);
    if (!s.subs[id].used) return;
    s.subs[id].handler.reset();
    s.subs[id].inbox = nullptr;
    s.subs[id].topic = kInvalidTopic;
    s.subs[id].used  = false;
    rtos::CriticalSection cs;
    if (s.stats.subs) --s.stats.subs;
}

void EventBus::publishRaw(TopicId topic, const void* data, u8 size) {
    State& s = st();
    {
        // Liczniki chroni sekcja krytyczna, a nie mutex tablicy subskrypcji:
        // te same pola aktualizuje ścieżka ISR, która mutexa wziąć nie może.
        rtos::CriticalSection cs;
        ++s.stats.published;
    }

    for (u16 i = 0; i < HYDRA_EVENT_MAX_SUBS; ++i) {
        RawHandler h;
        Inbox*     inbox = nullptr;
        bool       hit   = false;
        {
            rtos::LockGuard g(s.mtx);
            const Sub& sub = s.subs[i];
            if (sub.used && sub.topic == topic) {
                h     = sub.handler;
                inbox = sub.inbox;
                hit   = true;
            }
        }
        if (!hit) continue;

        if (inbox) {
            // Tryb Queued: callback wykona się w kontekście taska subskrybenta.
            QueuedMsg msg;
            msg.sub   = i;
            msg.topic = topic;
            msg.size  = size;
            memcpy(msg.data, data, size);
            if (!inbox->q_.send(&msg, 0)) {
                rtos::CriticalSection cs;
                ++inbox->dropped_;
                ++s.stats.queueDropped;
            }
        } else if (h) {
            // Tryb Direct: callback w kontekście nadawcy, poza blokadą tablicy.
            h(data);
        }
    }
}

bool EventBus::publishRawFromIsr(TopicId topic, const void* data, u8 size) {
    State& s = st();
    if (!s.isrQueue.valid()) {
        rtos::CriticalSection cs;
        ++s.stats.isrDropped;
        return false;
    }

    IsrMsg msg;
    msg.topic = topic;
    msg.size  = size;
    memcpy(msg.data, data, size);

    bool woken = false;
    const bool sent = s.isrQueue.sendFromIsr(&msg, &woken);

    rtos::CriticalSection cs;
    if (!sent) {
        ++s.stats.isrDropped;
        return false;
    }
    ++s.stats.isrPublished;
    return true;
}

u32 EventBus::drainIsr(u32 maxEvents) {
    State& s = st();
    if (!s.isrQueue.valid()) return 0;

    u32    n = 0;
    IsrMsg msg;
    while (n < maxEvents && s.isrQueue.receive(&msg, 0)) {
        publishRaw(msg.topic, msg.data, msg.size);
        ++n;
    }
    return n;
}

EventBus::Stats EventBus::stats() {
    State& s = st();
    rtos::CriticalSection cs;
    return s.stats;
}

void EventBus::reset() {
    State& s = st();
    {
        rtos::LockGuard g(s.mtx);
        for (auto& sub : s.subs) {
            sub.handler.reset();
            sub.inbox = nullptr;
            sub.topic = kInvalidTopic;
            sub.used  = false;
        }
        rtos::CriticalSection cs;
        s.stats = Stats{};
    }
    // Identyfikatory tematów celowo nie są resetowane — są przypisane typom
    // na stałe (statyczne zmienne lokalne w EventTraits).
}

}  // namespace hydra
