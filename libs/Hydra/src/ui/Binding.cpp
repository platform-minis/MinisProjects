/** Hydra — implementacja wiązania danych (rozdz. 6). */

#include "hydra/ui/Binding.hpp"

#if HYDRA_ENABLE_UI

namespace hydra {
namespace ui {

BindingHub::~BindingHub() {
    // Subskrypcje przeżyłyby obiekt i wołały metodę na zwolnionej pamięci.
    for (u8 i = 0; i < count_; ++i) {
        if (entries_[i].sub != kInvalidSub) EventBus::unsubscribe(entries_[i].sub);
    }
}

void BindingHub::stage(u8 index, const void* event, u8 size) {
    if (index >= HYDRA_UI_MAX_BINDINGS || !event) return;
    Entry& entry = entries_[index];

    bool needsCommand = false;
    {
        // Odkładamy wartość w sekcji krytycznej: zdarzenie przychodzi
        // z dowolnego taska, a odczytywać ją będzie ui.render.
        rtos::CriticalSection cs;
        ++stats_.received;
        memcpy(entry.staging, event, size < entry.size ? size : entry.size);

        if (entry.pending) {
            // Poprzednia wartość nie zdążyła trafić na ekran — nadpisujemy ją
            // i nie kolejkujemy drugiego polecenia. Interfejs ma pokazać stan
            // najnowszy, a nie odtwarzać każdy krok po drodze.
            ++stats_.coalesced;
        } else {
            entry.pending = true;
            needsCommand  = true;
        }
    }

    if (needsCommand) {
        queue_.post([this, index] { applyStaged(index); });
    }
}

void BindingHub::applyStaged(u8 index) {
    if (index >= count_) return;
    Entry& entry = entries_[index];
    if (!entry.shim || !entry.widget || !entry.fn) return;

    u8 snapshot[HYDRA_EVENT_MAX_SIZE];
    {
        rtos::CriticalSection cs;
        if (!entry.pending) return;
        memcpy(snapshot, entry.staging, entry.size);
        entry.pending = false;
    }

    // Funkcja użytkownika wykonuje się poza sekcją krytyczną: wolno jej
    // rysować, unieważniać obszary i robić cokolwiek innego.
    entry.shim(entry.widget, entry.fn, snapshot);
    ++stats_.applied;
}

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
