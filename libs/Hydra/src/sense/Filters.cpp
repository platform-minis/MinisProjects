/** Hydra — implementacja filtrów próbek (rozdz. 8). */

#include "hydra/sense/Filters.hpp"

#if HYDRA_ENABLE_SENSE

#include <math.h>

#include "hydra/core/Expected.hpp"

namespace hydra {
namespace sense {

Status ChannelFilter::configure(const FilterCfg& cfg) {
    cfg_ = cfg;

    if (cfg_.medianWindow > HYDRA_SENSE_MEDIAN_MAX) cfg_.medianWindow = HYDRA_SENSE_MEDIAN_MAX;
    if (cfg_.medianWindow == 0) cfg_.medianWindow = 1;

    if (cfg_.kind == FilterKind::Ema) {
        if (cfg_.emaAlpha <= 0.0f || cfg_.emaAlpha > 1.0f) return fail(Err::BadArgument);
    }

    if (cfg_.kind == FilterKind::Butterworth) {
        // Twierdzenie o próbkowaniu: odcięcie powyżej połowy częstotliwości
        // próbkowania nie ma sensu i daje niestabilne współczynniki.
        if (cfg_.sampleHz <= 0.0f || cfg_.cutoffHz <= 0.0f ||
            cfg_.cutoffHz >= cfg_.sampleHz / 2.0f) {
            return fail(Err::BadArgument);
        }

        // Biquad dolnoprzepustowy 2. rzędu, Q = 1/sqrt(2) (charakterystyka
        // Butterwortha — maksymalnie płaska w paśmie przepustowym).
        const float w0    = 2.0f * static_cast<float>(M_PI) * cfg_.cutoffHz / cfg_.sampleHz;
        const float cosw  = cosf(w0);
        const float sinw  = sinf(w0);
        const float alpha = sinw / 1.41421356f;

        const float a0 = 1.0f + alpha;
        b0_ = ((1.0f - cosw) / 2.0f) / a0;
        b1_ = (1.0f - cosw) / a0;
        b2_ = b0_;
        a1_ = (-2.0f * cosw) / a0;
        a2_ = (1.0f - alpha) / a0;
    }

    reset();
    return ok();
}

void ChannelFilter::reset() {
    primed_ = false;
    filled_ = 0;
    head_   = 0;
    ema_    = 0.0f;
    z1_     = 0.0f;
    z2_     = 0.0f;
    for (auto& v : window_) v = 0.0f;
}

float ChannelFilter::apply(float x) {
    switch (cfg_.kind) {
        case FilterKind::None:        return x;
        case FilterKind::Median:      return applyMedian(x);
        case FilterKind::Ema:         return applyEma(x);
        case FilterKind::Butterworth: return applyButterworth(x);
    }
    return x;
}

float ChannelFilter::applyMedian(float x) {
    window_[head_] = x;
    head_ = static_cast<u8>((head_ + 1) % cfg_.medianWindow);
    if (filled_ < cfg_.medianWindow) ++filled_;

    // Sortowanie przez wstawianie na kopii okna. Przy oknie ≤ 5 elementów jest
    // szybsze od czegokolwiek asymptotycznie lepszego i nie alokuje.
    float sorted[HYDRA_SENSE_MEDIAN_MAX];
    for (u8 i = 0; i < filled_; ++i) sorted[i] = window_[i];

    for (u8 i = 1; i < filled_; ++i) {
        const float key = sorted[i];
        i8 j = static_cast<i8>(i) - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            --j;
        }
        sorted[j + 1] = key;
    }

    // Przy parzystej liczbie próbek bierzemy element górny — bez uśredniania,
    // żeby wynik zawsze pochodził z rzeczywistego pomiaru.
    return sorted[filled_ / 2];
}

float ChannelFilter::applyEma(float x) {
    if (!primed_) {
        // Start od pierwszej próbki, a nie od zera: inaczej filtr przez
        // kilkanaście okresów zwracałby wartości, których czujnik nie zmierzył.
        ema_    = x;
        primed_ = true;
        return ema_;
    }
    ema_ = cfg_.emaAlpha * x + (1.0f - cfg_.emaAlpha) * ema_;
    return ema_;
}

float ChannelFilter::applyButterworth(float x) {
    if (!primed_) {
        // Naładowanie stanu tak, jakby na wejściu od zawsze była wartość x —
        // inaczej filtr zaczynałby od zera i pierwsze kilkanaście próbek
        // wyglądałoby jak skok mierzonej wielkości.
        //
        // Dla postaci DF-II transposed przy stałym wejściu i wzmocnieniu
        // jednostkowym w paśmie: y = x, więc
        //   z1 = y - b0*x = x*(1 - b0)
        //   z2 = b2*x - a2*y = x*(b2 - a2)
        z1_     = x * (1.0f - b0_);
        z2_     = x * (b2_ - a2_);
        primed_ = true;
    }
    const float y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
}

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
