#pragma once
/**
 * Hydra — kolejka poleceń interfejsu (rozdz. 6, wzorzec „UI thread" z Qt).
 *
 * Cały stan interfejsu należy do jednego taska — ui.render. Każda zmiana
 * pochodząca skądinąd (odczyt czujnika, wiadomość MQTT, przerwanie przycisku)
 * musi przejść przez tę kolejkę.
 *
 * Powód jest twardy, nie estetyczny: biblioteki graficzne — LVGL, LovyanGFX,
 * TFT_eSPI — nie są thread-safe. Mutacja drzewa widżetów z drugiego taska
 * w trakcie renderowania kończy się uszkodzoną listą, a objawia dopiero
 * kilka klatek później, w zupełnie innym miejscu.
 *
 *     // dowolny task:
 *     ui.queue().post([this] { label_.setText("gotowe"); });
 *
 * Kolejka trzyma polecenia w tablicy o stałym rozmiarze i nie alokuje
 * (rozdz. 11). Przepełnienie odrzuca najstarsze polecenie — w interfejsie
 * użytkownika świeższa informacja jest zawsze cenniejsza od starszej.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace ui {

/** Liczba poleceń mieszczących się w kolejce. */
#ifndef HYDRA_UI_QUEUE_DEPTH
#  define HYDRA_UI_QUEUE_DEPTH 16
#endif

class RenderQueue {
public:
    using Command = Delegate<void()>;

    /**
     * Kolejkuje polecenie. Bezpieczne z dowolnego taska.
     * Err::WouldBlock oznacza, że kolejka była pełna i najstarsze polecenie
     * zostało porzucone — samo zakolejkowanie i tak się powiodło.
     */
    Status post(Command command);

    /**
     * Wykonuje zakolejkowane polecenia i zwraca ich liczbę.
     * Wolno wołać wyłącznie z taska ui.render.
     */
    u32 drain(u32 maxCommands = HYDRA_UI_QUEUE_DEPTH);

    u32 pending() const;
    /** Ile poleceń porzucono z powodu przepełnienia. */
    u32 dropped() const { return dropped_; }
    void reset();

private:
    mutable rtos::Mutex mtx_;
    Command             commands_[HYDRA_UI_QUEUE_DEPTH];
    u8                  head_  = 0;  ///< następne do wykonania
    u8                  count_ = 0;
    u32                 dropped_ = 0;
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
