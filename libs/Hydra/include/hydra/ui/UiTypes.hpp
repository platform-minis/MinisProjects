#pragma once
/**
 * Hydra — typy i zdarzenia modułu interfejsu (rozdz. 6).
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/EventBus.hpp"
#include "hydra/gfx/Geometry.hpp"

namespace hydra {
namespace ui {

using gfx::Point;
using gfx::Rect;
using gfx::Size;

/** Faza kontaktu wskaźnika z ekranem. */
enum class PointerAction : u8 {
    Down = 0,  ///< dotknięcie
    Move,      ///< przesunięcie przy wciśniętym
    Up,        ///< oderwanie
};

constexpr const char* toString(PointerAction a) {
    switch (a) {
        case PointerAction::Down: return "down";
        case PointerAction::Move: return "move";
        case PointerAction::Up:   return "up";
    }
    return "unknown";
}

/** Surowy stan wskaźnika zgłaszany przez sterownik dotyku. */
struct PointerState {
    i16  x       = 0;
    i16  y       = 0;
    bool pressed = false;
};

/** Surowy stan enkodera obrotowego z przyciskiem. */
struct EncoderState {
    i32  position = 0;  ///< licznik narastający, nie różnica
    bool pressed  = false;
};

// ---------------------------------------------------------------------------
// Zdarzenia
// ---------------------------------------------------------------------------

/**
 * Zdarzenie dotyku. Wykrywaniem zboczy zajmuje się framework, nie sterownik:
 * sterownik zgłasza wyłącznie stan bieżący, a rozróżnienie dotknięcia,
 * przesunięcia i oderwania powstaje w jednym miejscu dla wszystkich paneli.
 */
struct PointerEvent {
    i16           x;
    i16           y;
    PointerAction action;
    /** Przesunięcie względem poprzedniego zdarzenia; zero przy Down. */
    i16           dx;
    i16           dy;
};

/** Obrót enkodera. delta jest różnicą, nie pozycją bezwzględną. */
struct EncoderEvent {
    i16  delta;
    bool pressed;
};

/** Naciśnięcie albo zwolnienie przycisku sprzętowego. */
struct ButtonEvent {
    u8   id;
    bool pressed;
    /** Czas przytrzymania w milisekundach; wypełniany przy zwolnieniu. */
    u16  heldMs;
};

/** Klatka została przerysowana i wysłana na panel. */
struct FrameRendered {
    u32 frameNumber;
    u16 dirtyWidth;
    u16 dirtyHeight;
    u16 renderUs;   ///< czas rysowania
    u16 presentUs;  ///< czas transferu na panel
};

}  // namespace ui
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::ui::PointerEvent,  "ui/pointer")
HYDRA_DECLARE_EVENT(hydra::ui::EncoderEvent,  "ui/encoder")
HYDRA_DECLARE_EVENT(hydra::ui::ButtonEvent,   "ui/button")
HYDRA_DECLARE_EVENT(hydra::ui::FrameRendered, "ui/frame")

#endif  // HYDRA_ENABLE_UI
