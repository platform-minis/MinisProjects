#pragma once
/**
 * Hydra — wiązanie danych z magistralą zdarzeń (rozdz. 6).
 *
 * Deklaruje się raz, co ma się dziać, i nie pisze więcej nic:
 *
 *     bindings.bind<Label, sense::Sample>(tempLabel,
 *         [](Label& l, const sense::Sample& s) { l.setValue(s.value[0], 1, "degC"); });
 *
 * Od tej chwili każda próbka sama trafia na ekran. W kodzie ekranu nie ma
 * pętli, porównywania starych wartości z nowymi ani wywołań „przerysuj".
 *
 * Dwie rzeczy dzieją się tu po cichu, a są niezbędne:
 *
 * 1. **Przejście do taska interfejsu.** Zdarzenie przychodzi w kontekście
 *    nadawcy — taska czujników albo sieci. Widżetu nie wolno tam dotknąć,
 *    więc wiązanie odkłada wartość i zleca jej nałożenie przez kolejkę poleceń.
 *
 * 2. **Scalanie nadmiarowych aktualizacji.** Czujnik nadający 100 razy na
 *    sekundę przy 30 klatkach zalałby kolejkę. Jeśli poprzednia wartość nie
 *    zdążyła trafić na ekran, jest nadpisywana — interfejs pokazuje najnowszą,
 *    a nie zaległą.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include <string.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/ui/RenderQueue.hpp"
#include "hydra/ui/Widget.hpp"

namespace hydra {
namespace ui {

/** Maksymalna liczba wiązań. */
#ifndef HYDRA_UI_MAX_BINDINGS
#  define HYDRA_UI_MAX_BINDINGS 12
#endif

class BindingHub {
public:
    explicit BindingHub(RenderQueue& queue) : queue_(queue) {}
    ~BindingHub();

    /**
     * Wiąże zdarzenie typu E z widżetem typu W.
     * apply jest wskaźnikiem na funkcję, nie domknięciem — wiązania mieszkają
     * w tablicy o stałym rozmiarze i nie alokują.
     */
    template <typename W, typename E>
    Status bind(W& widget, void (*apply)(W&, const E&)) {
        static_assert(EventContract<E>::value, "");
        if (!apply) return fail(Err::BadArgument);
        if (count_ >= HYDRA_UI_MAX_BINDINGS) return fail(Err::OutOfMemory);

        const u8 index = count_;
        Entry&   entry = entries_[index];
        entry.widget   = &widget;
        entry.fn       = reinterpret_cast<void*>(apply);
        entry.shim     = &applyShim<W, E>;
        entry.size     = sizeof(E);

        auto sub = EventBus::subscribe<E>([this, index](const E& event) {
            stage(index, &event, sizeof(E));
        });
        if (!sub) return fail(sub.error());

        entry.sub = *sub;
        ++count_;
        return ok();
    }

    struct Stats {
        u32 received  = 0;  ///< zdarzenia przyjęte z magistrali
        u32 applied   = 0;  ///< nałożone na widżety
        u32 coalesced = 0;  ///< nadpisane, zanim trafiły na ekran
    };

    Stats stats() const { return stats_; }
    u8    count() const { return count_; }

private:
    using ApplyShim = void (*)(void* widget, void* fn, const void* event);

    /** Przywraca typy widżetu i zdarzenia, po czym woła funkcję użytkownika. */
    template <typename W, typename E>
    static void applyShim(void* widget, void* fn, const void* event) {
        auto f = reinterpret_cast<void (*)(W&, const E&)>(fn);
        f(*static_cast<W*>(widget), *static_cast<const E*>(event));
    }

    struct Entry {
        void*     widget = nullptr;
        void*     fn     = nullptr;
        ApplyShim shim   = nullptr;
        SubId     sub    = kInvalidSub;
        u8        size   = 0;
        bool      pending = false;
        u8        staging[HYDRA_EVENT_MAX_SIZE] = {};
    };

    void stage(u8 index, const void* event, u8 size);
    void applyStaged(u8 index);

    RenderQueue& queue_;
    Entry        entries_[HYDRA_UI_MAX_BINDINGS];
    u8           count_ = 0;
    Stats        stats_{};
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
