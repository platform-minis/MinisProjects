#pragma once
/**
 * Hydra — analizator widma jako element potoku.
 *
 * Zamienia okno próbek na prążki widma w decybelach. To najtańszy sposób, żeby
 * zobaczyć, **co** dzieje się w sygnale: dominującą częstotliwość wibracji,
 * pasmo zakłócenia, harmoniczne silnika. Diagnostyka, dla której nie trzeba
 * modelu ani uczenia — a często wystarcza.
 *
 * ## Czym różni się od MFCC
 *
 * `MfccExtractor` odrzuca prawie całą informację: 129 prążków zamienia na
 * 13 liczb opisujących barwę, bo tego chce model. Tu chodzi o coś odwrotnego
 * — o zachowanie tego, co widać. Prążki idą wprost do widżetu albo do progu
 * na paśmie, a decybele są dlatego, że słuch i mechanika reagują na rząd
 * wielkości, nie na wartość bezwzględną.
 *
 * Oba elementy stoją na tym samym FFT (`util::Fft`), więc dołożenie widma
 * kosztuje pole na wynik, a nie drugą implementację przekształcenia.
 *
 * ## Uśrednianie
 *
 * Widmo pojedynczego okna skacze — sąsiednie okna tego samego dźwięku różnią
 * się o kilka decybeli. Wykładnicze uśrednianie po oknach (`smoothing`)
 * uspokaja obraz na tyle, żeby dało się go czytać; zero wyłącza je zupełnie
 * dla pomiarów, w których liczy się pojedyncze okno.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/media/Element.hpp"

/** Największy obsługiwany rozmiar przekształcenia. Decyduje o rozmiarze obiektu. */
#ifndef HYDRA_SPECTRUM_MAX_FFT
#  define HYDRA_SPECTRUM_MAX_FFT 256
#endif

namespace hydra {
namespace media {

class SpectrumAnalyzer : public Element {
public:
    struct Config {
        /** Rozmiar przekształcenia; potęga dwójki, ≤ HYDRA_SPECTRUM_MAX_FFT. */
        u16 fftSize = 256;
        /** Przesuw okna w próbkach. Zero znaczy „o całe okno". */
        u16 hopSamples = 0;
        /**
         * Ile prążków wystawić na wyjściu. Zero znaczy wszystkie.
         *
         * Widżet o szerokości 64 pikseli nie ma co zrobić ze 129 prążkami,
         * a zwężanie ich po drodze oznaczałoby, że każdy odbiorca robi to sam.
         */
        u16 bins = 0;
        /**
         * Wygładzanie po oknach, 0–255. 0 wyłącza, 200 daje spokojny obraz.
         * Wartość jest wagą **poprzedniego** widma w Q8.
         */
        u8  smoothing = 0;
        /** Dolna granica skali decybelowej; poniżej niej prążek to zero. */
        float floorDb = -80.0f;
    };

    SpectrumAnalyzer() : Element("spectrum") {}

    Status configure(const Config& cfg);
    const Config& config() const { return cfg_; }

    u8 inputCount() const override { return 1; }
    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    /** Ile okien przeliczono. */
    u32 windows() const { return windows_; }
    /** Ostatnie widmo w decybelach; `binCount()` wartości. */
    const float* lastSpectrum() const { return output_; }
    /** Ile prążków naprawdę wychodzi. */
    u16 binCount() const { return binCount_; }
    /** Częstotliwość środka prążka — do opisania osi. */
    float binHz(u16 index) const;

private:
    void computeWindow();
    u32  fill(const i16* samples, u32 count);
    void slide();

    Config     cfg_{};
    Pipeline*  pipeline_ = nullptr;
    BlockPool* pool_ = nullptr;
    u32        sampleRate_ = 0;
    u32        windows_ = 0;
    u16        binCount_ = 0;

    float window_[HYDRA_SPECTRUM_MAX_FFT] = {};
    u16   filled_ = 0;

    float scratch_[HYDRA_SPECTRUM_MAX_FFT] = {};
    float power_[HYDRA_SPECTRUM_MAX_FFT / 2 + 1] = {};
    /** Widmo wyjściowe w dB, już zwężone i wygładzone. */
    float output_[HYDRA_SPECTRUM_MAX_FFT / 2 + 1] = {};
    bool  primed_ = false;
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
