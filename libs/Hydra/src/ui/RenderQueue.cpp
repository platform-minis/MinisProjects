/** Hydra — implementacja kolejki poleceń interfejsu (rozdz. 6). */

#include "hydra/ui/RenderQueue.hpp"

#if HYDRA_ENABLE_UI

namespace hydra {
namespace ui {

Status RenderQueue::post(Command command) {
    if (!command) return fail(Err::BadArgument);

    rtos::LockGuard guard(mtx_);
    if (!guard.held()) return fail(Err::Timeout);

    bool overflowed = false;
    if (count_ == HYDRA_UI_QUEUE_DEPTH) {
        // Przepełnienie odrzuca najstarsze polecenie. W interfejsie świeższa
        // informacja jest cenniejsza od starszej — odrzucenie nowej zostawiłoby
        // na ekranie stan, którego już nie ma.
        head_ = static_cast<u8>((head_ + 1) % HYDRA_UI_QUEUE_DEPTH);
        --count_;
        ++dropped_;
        overflowed = true;
    }

    const u8 slot = static_cast<u8>((head_ + count_) % HYDRA_UI_QUEUE_DEPTH);
    commands_[slot] = command;
    ++count_;

    return overflowed ? fail(Err::WouldBlock) : ok();
}

u32 RenderQueue::drain(u32 maxCommands) {
    u32 executed = 0;

    while (executed < maxCommands) {
        Command command;
        {
            rtos::LockGuard guard(mtx_);
            if (!guard.held() || count_ == 0) break;
            command = commands_[head_];
            commands_[head_].reset();
            head_ = static_cast<u8>((head_ + 1) % HYDRA_UI_QUEUE_DEPTH);
            --count_;
        }
        // Polecenie wykonujemy poza blokadą: wolno mu zakolejkować kolejne,
        // a pod blokadą byłoby to zakleszczenie.
        if (command) {
            command();
            ++executed;
        }
    }
    return executed;
}

u32 RenderQueue::pending() const {
    rtos::LockGuard guard(mtx_);
    return guard.held() ? count_ : 0;
}

void RenderQueue::reset() {
    rtos::LockGuard guard(mtx_);
    for (auto& c : commands_) c.reset();
    head_    = 0;
    count_   = 0;
    dropped_ = 0;
}

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
