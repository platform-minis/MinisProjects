/**
 * Hydra — FFT radix-2 dla sygnału rzeczywistego. Patrz nagłówek po decyzje.
 */

#include "hydra/util/Fft.hpp"

#include "hydra/core/RealMath.hpp"

namespace hydra {
namespace util {
namespace {

/**
 * Odwrócenie bitów indeksu — przestawienie próbek przed motylkami.
 *
 * Radix-2 na miejscu wymaga, żeby wejście leżało w kolejności odwróconych
 * bitów; dopiero wtedy kolejne poziomy motylków czytają sąsiadujące pary.
 * Wersja z osobną tablicą indeksów byłaby szybsza, ale kosztowałaby tyle
 * pamięci, co samo przekształcenie.
 */
void bitReverse(float* re, float* im, u16 n) {
    u16 j = 0;
    for (u16 i = 1; i < n; ++i) {
        u16 bit = static_cast<u16>(n >> 1);
        for (; (j & bit) != 0; bit = static_cast<u16>(bit >> 1)) {
            j = static_cast<u16>(j ^ bit);
        }
        j = static_cast<u16>(j ^ bit);

        if (i < j) {
            const float tr = re[i]; re[i] = re[j]; re[j] = tr;
            const float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
}

}  // namespace

void applyHann(float* samples, u16 n) {
    if (samples == nullptr || n == 0) return;

    // Okno symetryczne: `n - 1` w mianowniku, a nie `n`. Różnica jednego
    // punktu jest przy 256 próbkach nieistotna dla ucha, ale przy 16 —
    // widoczna jako przeciek do sąsiedniego prążka.
    const float denom = static_cast<float>(n > 1 ? n - 1 : 1);
    for (u16 i = 0; i < n; ++i) {
        const float w = 0.5f - 0.5f * cosReal(kTwoPi * static_cast<float>(i) / denom);
        samples[i] *= w;
    }
}

bool powerSpectrum(float* samples, float* scratch, u16 n, float* power) {
    if (samples == nullptr || scratch == nullptr || power == nullptr) return false;
    if (!isValidFftSize(n)) return false;

    float* re = samples;
    float* im = scratch;
    for (u16 i = 0; i < n; ++i) im[i] = 0.0f;

    bitReverse(re, im, n);

    // Motylki: log2(n) poziomów, w każdym pary odległe o `len/2`.
    for (u16 len = 2; len <= n; len = static_cast<u16>(len << 1)) {
        const float angle = -kTwoPi / static_cast<float>(len);
        const float wRe = cosReal(angle);
        const float wIm = sinReal(angle);

        for (u16 i = 0; i < n; i = static_cast<u16>(i + len)) {
            /*
             * Współczynnik obrotu liczony przyrostowo w obrębie poziomu.
             *
             * Wywołanie `sinf`/`cosf` na każdą parę byłoby dokładniejsze, ale
             * kosztowałoby n·log(n) wywołań funkcji przestępnych — na MCU bez
             * jednostki zmiennoprzecinkowej to różnica rzędu wielkości.
             * Narastanie błędu przy n ≤ 1024 zostaje poniżej szumu kwantyzacji
             * 16-bitowego przetwornika.
             */
            float curRe = 1.0f;
            float curIm = 0.0f;

            for (u16 k = 0; k < len / 2; ++k) {
                const u16 a = static_cast<u16>(i + k);
                const u16 b = static_cast<u16>(i + k + len / 2);

                const float tRe = re[b] * curRe - im[b] * curIm;
                const float tIm = re[b] * curIm + im[b] * curRe;

                re[b] = re[a] - tRe;
                im[b] = im[a] - tIm;
                re[a] += tRe;
                im[a] += tIm;

                const float nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }

    // Widmo sygnału rzeczywistego jest symetryczne — zwracamy połowę plus
    // składową stałą i Nyquista.
    const u16 bins = spectrumBins(n);
    for (u16 i = 0; i < bins; ++i) {
        power[i] = re[i] * re[i] + im[i] * im[i];
    }
    return true;
}

}  // namespace util
}  // namespace hydra
