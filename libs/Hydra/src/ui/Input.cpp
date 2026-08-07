/** Hydra — implementacja routera wejścia (rozdz. 6). */

#include "hydra/ui/IInput.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/EventBus.hpp"

namespace hydra {
namespace ui {

Status InputRouter::begin() {
    if (pointer_) HYDRA_CHECK(pointer_->begin());
    if (encoder_) HYDRA_CHECK(encoder_->begin());
    if (buttons_) HYDRA_CHECK(buttons_->begin());
    return ok();
}

u32 InputRouter::pollPointer() {
    if (!pointer_) return 0;

    auto state = pointer_->read();
    // Brak nowego odczytu nie jest awarią — panel dotykowy odpowiada wolniej,
    // niż leci pętla renderowania.
    if (!state) return 0;

    u32 events = 0;

    if (!pointerKnown_) {
        // Pierwszy odczyt wyznacza punkt odniesienia. Gdyby ekran był w tej
        // chwili dotknięty, potraktowanie tego jako Down byłoby fałszywym
        // dotknięciem tuż po starcie.
        lastPointer_  = *state;
        pointerKnown_ = true;
        return 0;
    }

    const i16 dx = static_cast<i16>(state->x - lastPointer_.x);
    const i16 dy = static_cast<i16>(state->y - lastPointer_.y);

    if (state->pressed && !lastPointer_.pressed) {
        EventBus::publish(PointerEvent{state->x, state->y, PointerAction::Down, 0, 0});
        ++events;
    } else if (!state->pressed && lastPointer_.pressed) {
        // Oderwanie zgłaszamy w ostatnim znanym punkcie kontaktu: współrzędne
        // po oderwaniu bywają śmieciowe, a to na nich opiera się trafienie
        // w przycisk.
        EventBus::publish(
            PointerEvent{lastPointer_.x, lastPointer_.y, PointerAction::Up, 0, 0});
        ++events;
    } else if (state->pressed && (dx != 0 || dy != 0)) {
        EventBus::publish(PointerEvent{state->x, state->y, PointerAction::Move, dx, dy});
        ++events;
    }

    lastPointer_ = *state;
    return events;
}

u32 InputRouter::pollEncoder() {
    if (!encoder_) return 0;

    auto state = encoder_->read();
    if (!state) return 0;

    if (!encoderKnown_) {
        lastEncoderPos_     = state->position;
        lastEncoderPressed_ = state->pressed;
        encoderKnown_       = true;
        return 0;
    }

    u32       events = 0;
    const i32 delta  = state->position - lastEncoderPos_;

    if (delta != 0 || state->pressed != lastEncoderPressed_) {
        // Zdarzenie niesie różnicę, nie pozycję: subskrybent nie musi pamiętać
        // poprzedniej wartości ani radzić sobie z przepełnieniem licznika.
        const i32 clamped = delta > 32767 ? 32767 : (delta < -32768 ? -32768 : delta);
        EventBus::publish(EncoderEvent{static_cast<i16>(clamped), state->pressed});
        ++events;
    }

    lastEncoderPos_     = state->position;
    lastEncoderPressed_ = state->pressed;
    return events;
}

u32 InputRouter::pollButtons(Millis now) {
    if (!buttons_) return 0;

    const u8 count = buttons_->count() < kMaxButtons ? buttons_->count() : kMaxButtons;
    u32      events = 0;

    for (u8 i = 0; i < count; ++i) {
        auto pressed = buttons_->pressed(i);
        if (!pressed) continue;

        if (!buttonsKnown_) {
            buttonState_[i] = *pressed;
            buttonSince_[i] = now;
            continue;
        }
        if (*pressed == buttonState_[i]) continue;

        if (*pressed) {
            buttonSince_[i] = now;
            EventBus::publish(ButtonEvent{i, true, 0});
        } else {
            // Czas przytrzymania liczymy przy zwolnieniu — subskrybent dostaje
            // go razem ze zdarzeniem i nie musi sam mierzyć.
            const u32 held = now - buttonSince_[i];
            EventBus::publish(
                ButtonEvent{i, false, static_cast<u16>(held > 0xFFFF ? 0xFFFF : held)});
        }
        buttonState_[i] = *pressed;
        ++events;
    }

    buttonsKnown_ = true;
    return events;
}

u32 InputRouter::poll(Millis now) {
    return pollPointer() + pollEncoder() + pollButtons(now);
}

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
