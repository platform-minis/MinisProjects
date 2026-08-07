#pragma once
/**
 * Hydra — filtry łańcucha przetwarzania próbek (rozdz. 8).
 *
 * Trzy filtry pokrywają praktycznie wszystkie zastosowania w urządzeniach IoT
 * i robotach:
 *   - mediana usuwa pojedyncze zakłócenia impulsowe (iskra, zwarcie na
 *     magistrali) bez rozmazywania zbocza,
 *   - EMA wygładza szum ciągły przy koszcie jednego mnożenia na próbkę,
 *   - Butterworth 2. rzędu daje płaską charakterystykę w paśmie przepustowym
 *     tam, gdzie EMA zniekształca zbyt mocno.
 *
 * Współczynniki liczone są w configure(), a nie przy każdej próbce — funkcje
 * trygonometryczne nie mają prawa pojawić się w ścieżce akwizycji.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace sense {

/** Maksymalne okno mediany. Każdy kanał rezerwuje tyle floatów. */
#ifndef HYDRA_SENSE_MEDIAN_MAX
#  define HYDRA_SENSE_MEDIAN_MAX 5
#endif

enum class FilterKind : u8 {
    None = 0,
    Median,
    Ema,
    Butterworth,
};

struct FilterCfg {
    FilterKind kind = FilterKind::None;
    /** Okno mediany; przycinane do HYDRA_SENSE_MEDIAN_MAX. Nieparzyste jest lepsze. */
    u8    medianWindow = 5;
    /** Współczynnik EMA: 1.0 = brak wygładzania, 0.05 = bardzo wolne. */
    float emaAlpha     = 0.2f;
    /** Częstotliwość odcięcia i próbkowania dla Butterwortha [Hz]. */
    float cutoffHz     = 1.0f;
    float sampleHz     = 10.0f;
};

/** Filtr jednego kanału. Bezstanowy przy FilterKind::None. */
class ChannelFilter {
public:
    /** Ustawia parametry i zeruje stan. Bezpieczne do wołania wielokrotnie. */
    Status configure(const FilterCfg& cfg);

    /** Przepuszcza próbkę. Pierwsza próbka po reset() inicjalizuje stan. */
    float apply(float x);

    void reset();

    FilterKind kind() const { return cfg_.kind; }

private:
    float applyMedian(float x);
    float applyEma(float x);
    float applyButterworth(float x);

    FilterCfg cfg_{};
    bool      primed_ = false;

    // mediana: bufor pierścieniowy okna
    float window_[HYDRA_SENSE_MEDIAN_MAX] = {};
    u8    filled_ = 0;
    u8    head_   = 0;

    // EMA
    float ema_ = 0.0f;

    // biquad (direct form II transposed)
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
