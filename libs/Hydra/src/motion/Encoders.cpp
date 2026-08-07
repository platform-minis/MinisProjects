/** Hydra — implementacja enkoderów (rozdz. 9). */

#include "hydra/motion/IEncoder.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/RealMath.hpp"
#include "hydra/hal/Hal.hpp"

namespace hydra {
namespace motion {
namespace {

/**
 * Tablica przejść dekodera kwadraturowego, indeksowana jako
 * (poprzedni stan << 2) | stan bieżący.
 *
 * Wartość 0 oznacza brak zmiany, ±1 kierunek obrotu, a 2 przejście
 * nieprawidłowe — takie, w którym oba sygnały zmieniły się jednocześnie.
 * Przy poprawnym sygnale kwadraturowym jest to niemożliwe, więc jego
 * wystąpienie oznacza drgania styków albo zgubione przerwanie.
 */
constexpr i8 kTransitions[16] = {
    0,  +1, -1, 2,
    -1, 0,  2,  +1,
    +1, 2,  0,  -1,
    2,  -1, +1, 0,
};

}  // namespace

// ---------------------------------------------------------------------------
// QuadratureEncoder
// ---------------------------------------------------------------------------

Status QuadratureEncoder::configure(const Config& cfg) {
    if (cfg.a == hal::kNoPin || cfg.b == hal::kNoPin) return fail(Err::BadArgument);
    if (cfg.a == cfg.b) return fail(Err::BadArgument);
    cfg_ = cfg;
    return ok();
}

Status QuadratureEncoder::begin() {
    const hal::PinMode mode = cfg_.pullUp ? hal::PinMode::InputPullUp : hal::PinMode::Input;
    HYDRA_CHECK(hal::Hal::gpio().configure(cfg_.a, mode));
    HYDRA_CHECK(hal::Hal::gpio().configure(cfg_.b, mode));

    ready_ = true;
    return reset();
}

Status QuadratureEncoder::reset() {
    rtos::CriticalSection cs;
    counts_   = 0;
    glitches_ = 0;
    primed_   = false;
    return ok();
}

Result<i32> QuadratureEncoder::counts() {
    if (!ready_) return unexpected(Err::NotInitialized);
    // Odczyt pod sekcją krytyczną: licznik aktualizuje przerwanie, a na
    // rdzeniach 32-bitowych i tak byłby atomowy — ale nie na wszystkich
    // platformach docelowych i nie przy dostępie z drugiego rdzenia.
    rtos::CriticalSection cs;
    return static_cast<i32>(counts_);
}

void QuadratureEncoder::update(bool a, bool b) {
    const u8 state = static_cast<u8>((a ? 0x2 : 0) | (b ? 0x1 : 0));

    if (!primed_) {
        // Pierwszy odczyt tylko ustala punkt odniesienia. Bez tego początkowy
        // stan wejść zostałby zinterpretowany jako ruch.
        lastState_ = state;
        primed_    = true;
        return;
    }

    const i8 step = kTransitions[(lastState_ << 2) | state];
    lastState_    = state;

    if (step == 2) {
        ++glitches_;
        return;
    }
    if (step == 0) return;

    counts_ += cfg_.invert ? -step : step;
}

HYDRA_ISR_ATTR void QuadratureEncoder::onEdge(void* arg) {
    auto* self = static_cast<QuadratureEncoder*>(arg);
    // W przerwaniu wolno odczytać stan pinu i policzyć krok — bez alokacji,
    // bez logowania, bez magistral (rozdz. 10).
    const auto a = hal::Hal::gpio().read(self->cfg_.a);
    const auto b = hal::Hal::gpio().read(self->cfg_.b);
    if (a && b) self->update(*a, *b);
}

Status QuadratureEncoder::attachInterrupts() {
    if (!ready_) return fail(Err::NotInitialized);

    HYDRA_CHECK(hal::Hal::gpio().attachInterrupt(cfg_.a, hal::Edge::Both,
                                                 &QuadratureEncoder::onEdge, this));
    HYDRA_CHECK(hal::Hal::gpio().attachInterrupt(cfg_.b, hal::Edge::Both,
                                                 &QuadratureEncoder::onEdge, this));
    irqActive_ = true;
    return ok();
}

Status QuadratureEncoder::detachInterrupts() {
    if (!irqActive_) return ok();
    HYDRA_CHECK(hal::Hal::gpio().detachInterrupt(cfg_.a));
    HYDRA_CHECK(hal::Hal::gpio().detachInterrupt(cfg_.b));
    irqActive_ = false;
    return ok();
}

// ---------------------------------------------------------------------------
// WheelOdometer
// ---------------------------------------------------------------------------

Status WheelOdometer::configure(const Config& cfg) {
    if (cfg.countsPerRevolution <= 0) return fail(Err::BadArgument);
    if (!(cfg.wheelRadius > real(0.0f))) return fail(Err::BadArgument);

    cfg_ = cfg;
    // Droga na jedno zliczenie: obwód koła podzielony przez rozdzielczość.
    // Liczona raz, bo w pętli 1 ms każde dzielenie jest kosztem.
    const real_t circumference = real(kTwoPi) * cfg.wheelRadius;
    perCount_ = circumference / real(static_cast<float>(cfg.countsPerRevolution));
    reset();
    return ok();
}

void WheelOdometer::reset() {
    last_   = 0;
    primed_ = false;
    total_  = real(0.0f);
}

real_t WheelOdometer::advance(i32 counts) {
    if (!primed_) {
        last_   = counts;
        primed_ = true;
        return real(0.0f);
    }

    // Odejmowanie w arytmetyce uzupełnieniowej daje poprawną różnicę także
    // wtedy, gdy licznik przekręcił się między odczytami.
    const i32 delta = counts - last_;
    last_ = counts;

    const real_t distance = real(static_cast<float>(delta)) * perCount_;
    total_ += distance;
    return distance;
}

real_t WheelOdometer::velocity(i32 counts, real_t dtSeconds) {
    const real_t distance = advance(counts);
    if (!(dtSeconds > real(0.0f))) return real(0.0f);
    return distance / dtSeconds;
}

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
