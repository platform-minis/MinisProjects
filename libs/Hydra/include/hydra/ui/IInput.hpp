#pragma once
/**
 * Hydra — urządzenia wejściowe interfejsu (rozdz. 6).
 *
 * Sterownik zgłasza wyłącznie **stan bieżący** — gdzie jest palec, jaka jest
 * pozycja enkodera, czy przycisk jest wciśnięty. Wykrywanie zboczy, liczenie
 * różnic, mierzenie czasu przytrzymania i publikacja zdarzeń należą do
 * frameworka.
 *
 * To ten sam podział, co w module czujników i z tego samego powodu: logika
 * napisana raz zachowuje się identycznie na panelu pojemnościowym, rezystancyjnym
 * i na enkoderze, a adapter konkretnego układu ma kilkanaście linii i nie ma
 * gdzie popełnić błędu w obsłudze stanu.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Expected.hpp"
#include "hydra/ui/UiTypes.hpp"

namespace hydra {
namespace ui {

/** Panel dotykowy albo inne urządzenie wskazujące. */
class IPointerDevice {
public:
    virtual ~IPointerDevice() = default;
    virtual Status begin() { return ok(); }
    /**
     * Stan bieżący. Err::WouldBlock oznacza brak nowego odczytu i nie jest
     * awarią — dokładnie jak w module czujników.
     */
    virtual Result<PointerState> read() = 0;
};

/** Enkoder obrotowy, zwykle z przyciskiem w osi. */
class IEncoderDevice {
public:
    virtual ~IEncoderDevice() = default;
    virtual Status begin() { return ok(); }
    virtual Result<EncoderState> read() = 0;
};

/** Zestaw przycisków sprzętowych. */
class IButtonDevice {
public:
    virtual ~IButtonDevice() = default;
    virtual Status begin() { return ok(); }
    /** Liczba obsługiwanych przycisków. */
    virtual u8 count() const = 0;
    /** Stan przycisku o podanym numerze. */
    virtual Result<bool> pressed(u8 index) = 0;
};

/**
 * Zamienia surowe stany urządzeń na zdarzenia magistrali.
 *
 * Trzyma poprzedni stan każdego urządzenia i publikuje wyłącznie zmiany —
 * dzięki temu ekran nie jest budzony przy każdym odpytaniu, a subskrybenci
 * dostają zdarzenia, nie strumień odczytów.
 */
class InputRouter {
public:
    /** Maksymalna liczba przycisków ze śledzonym czasem przytrzymania. */
    static constexpr u8 kMaxButtons = 8;

    void attachPointer(IPointerDevice& device) { pointer_ = &device; }
    void attachEncoder(IEncoderDevice& device) { encoder_ = &device; }
    void attachButtons(IButtonDevice& device) { buttons_ = &device; }

    Status begin();

    /**
     * Odpytuje podłączone urządzenia i publikuje wykryte zmiany.
     * Zwraca liczbę opublikowanych zdarzeń. Wołane raz na klatkę z ui.render.
     */
    u32 poll(Millis now);

    /** Ostatnia znana pozycja wskaźnika — przydatna przy trafianiu w widżety. */
    PointerState lastPointer() const { return lastPointer_; }

private:
    u32 pollPointer();
    u32 pollEncoder();
    u32 pollButtons(Millis now);

    IPointerDevice* pointer_ = nullptr;
    IEncoderDevice* encoder_ = nullptr;
    IButtonDevice*  buttons_ = nullptr;

    PointerState lastPointer_{};
    bool         pointerKnown_ = false;

    i32  lastEncoderPos_     = 0;
    bool encoderKnown_       = false;
    bool lastEncoderPressed_ = false;

    bool   buttonState_[kMaxButtons]   = {};
    Millis buttonSince_[kMaxButtons]   = {};
    bool   buttonsKnown_               = false;
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
