/**
 * Hydra — kolejka sygnałów między magistralą a skryptem.
 *
 * Pierścień pod blokadą i jedna subskrypcja magistrali. Nic więcej się tu nie
 * dzieje i o to chodzi: to jest granica między kontekstem nadawcy a kontekstem
 * taska skryptu, a granica ma być cienka.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "SignalQueue.hpp"

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace script {
namespace detail {

namespace {

struct SignalQueue {
    ScriptSignal items[HYDRA_SCRIPT_SIGNAL_QUEUE];
    u8           head    = 0;
    u8           tail    = 0;
    u32          dropped = 0;
    rtos::Mutex  mtx;
};

SignalQueue gQueue;
SubId       gSubscription = kInvalidSub;

void pushSignal(const ScriptSignal& s) {
    rtos::LockGuard guard(gQueue.mtx);
    const u8        next = static_cast<u8>((gQueue.head + 1) % HYDRA_SCRIPT_SIGNAL_QUEUE);
    if (next == gQueue.tail) {
        // Pełny pierścień: gubimy najnowszy, nie najstarszy. Sygnał zgubiony
        // po cichu byłby gorszy od widocznego w liczniku.
        ++gQueue.dropped;
        return;
    }
    gQueue.items[gQueue.head] = s;
    gQueue.head               = next;
}

}  // namespace

Status signalQueueSubscribe() {
    if (gSubscription != kInvalidSub) return ok();

    auto sub = EventBus::subscribe<ScriptSignal>([](const ScriptSignal& s) { pushSignal(s); });
    if (!sub) return fail(sub.error());
    gSubscription = *sub;
    return ok();
}

void signalQueueRelease() {
    if (gSubscription != kInvalidSub) {
        EventBus::unsubscribe(gSubscription);
        gSubscription = kInvalidSub;
    }
    rtos::LockGuard guard(gQueue.mtx);
    gQueue.head = gQueue.tail = 0;
}

bool signalQueuePop(ScriptSignal& out) {
    rtos::LockGuard guard(gQueue.mtx);
    if (gQueue.tail == gQueue.head) return false;
    out         = gQueue.items[gQueue.tail];
    gQueue.tail = static_cast<u8>((gQueue.tail + 1) % HYDRA_SCRIPT_SIGNAL_QUEUE);
    return true;
}

u32 signalQueueDropped() { return gQueue.dropped; }

}  // namespace detail
}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
