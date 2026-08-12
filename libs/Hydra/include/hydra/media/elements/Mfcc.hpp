#pragma once
/**
 * Hydra — współczynniki mel-cepstralne jako element potoku.
 *
 * Zamienia okno próbek na krótki wektor liczb opisujący **barwę** dźwięku.
 * To jest to, czym karmi się model rozpoznający słowo kluczowe: surowe próbki
 * niosą tysiąc liczb na okno, cechy — kilkanaście, a rozpoznanie zależy
 * od tych kilkunastu.
 *
 * ## Droga sygnału
 *
 *     okno próbek → Hann → FFT → moc → filtry mel → log → DCT → cepstrum
 *
 * Skala mel odwzorowuje to, że słuch rozróżnia niskie tony lepiej niż wysokie:
 * filtry są gęste przy 100 Hz i rzadkie przy 4 kHz. Logarytm — że głośność
 * odbieramy potęgowo, więc bez niego cechy zmieniałyby się z odległością
 * od źródła. DCT dekoreluje wynik, dzięki czemu model może patrzeć na kilka
 * pierwszych współczynników zamiast na cały bank.
 *
 * ## Pamięć
 *
 * Bufory robocze są w obiekcie, a nie podawane z zewnątrz — inaczej niż okno
 * w `Inference`. Powód: ich rozmiar wynika z **rozmiaru przekształcenia**,
 * który jest cechą projektu i jest znany przy kompilacji, a nie z modelu,
 * którego przy kompilacji jeszcze nie ma. `HYDRA_MFCC_MAX_FFT` ustawia sufit.
 *
 * Przy domyślnych 256 punktach to około 3 kB — mieści się w RAM-ie każdej
 * płytki, na którą Hydra celuje.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/media/Element.hpp"

/** Największy obsługiwany rozmiar FFT. Decyduje o rozmiarze obiektu. */
#ifndef HYDRA_MFCC_MAX_FFT
#  define HYDRA_MFCC_MAX_FFT 256
#endif

/** Największa liczba filtrów w banku mel. */
#ifndef HYDRA_MFCC_MAX_FILTERS
#  define HYDRA_MFCC_MAX_FILTERS 32
#endif

/** Największa liczba zwracanych współczynników. */
#ifndef HYDRA_MFCC_MAX_COEFFS
#  define HYDRA_MFCC_MAX_COEFFS 16
#endif

namespace hydra {
namespace media {

class MfccExtractor : public Element {
public:
    struct Config {
        /** Rozmiar przekształcenia; potęga dwójki, ≤ HYDRA_MFCC_MAX_FFT. */
        u16 fftSize = 256;
        /**
         * Przesuw okna w próbkach. Zero znaczy „o całe okno".
         *
         * Przy mowie standardem jest zakładka rzędu połowy okna: głoska
         * krótsza od okna wypadłaby inaczej na styku dwóch i model nie
         * zobaczyłby jej ani razu w całości.
         */
        u16 hopSamples = 0;
        /** Liczba filtrów mel. 26 to wartość klasyczna dla mowy. */
        u8  filterCount = 26;
        /** Ile współczynników zwracać. 13 wystarcza dla mowy. */
        u8  coeffCount = 13;
        /** Dolna granica banku filtrów. */
        u16 lowHz = 80;
        /** Górna granica; 0 znaczy Nyquist. */
        u16 highHz = 0;
    };

    MfccExtractor() : Element("mfcc") {}

    Status configure(const Config& cfg);
    const Config& config() const { return cfg_; }

    u8 inputCount() const override { return 1; }
    u8 outputCount() const override { return 1; }
    Result<MediaFormat> negotiate(u8 outPad, const MediaFormat& in) override;
    MemReq memoryRequest(u8 outPad) const override;
    Status onPrepare(Pipeline& pipeline) override;
    void   process(u64 nowUs) override;

    /** Ile okien policzono — potok bez cech wygląda tak samo jak sprawny. */
    u32 windows() const { return windows_; }
    /** Ostatni wektor cech; `coeffCount` wartości. */
    const float* lastCoeffs() const { return coeffs_; }

private:
    /** Buduje bank filtrów mel dla podanej częstotliwości próbkowania. */
    void buildFilterBank(u32 sampleRate);
    /** Liczy cechy z pełnego okna. */
    void computeWindow();
    /** Dokłada próbki do okna; zwraca liczbę przyjętych bajtów. */
    u32 fill(const i16* samples, u32 count);
    void slide();

    Config     cfg_{};
    Pipeline*  pipeline_ = nullptr;
    BlockPool* pool_ = nullptr;
    u32        sampleRate_ = 0;
    u32        windows_ = 0;

    /** Okno próbek w postaci zmiennoprzecinkowej — wejście przekształcenia. */
    float window_[HYDRA_MFCC_MAX_FFT] = {};
    u16   filled_ = 0;

    /** Przestrzeń robocza FFT (część urojona) i widmo mocy. */
    float scratch_[HYDRA_MFCC_MAX_FFT] = {};
    float power_[HYDRA_MFCC_MAX_FFT / 2 + 1] = {};

    /**
     * Bank filtrów jako trójki (początek, szczyt, koniec) w prążkach.
     *
     * Trzymamy granice, a nie wagi: wagi to `filterCount × bins` liczb, czyli
     * przy 26 filtrach i 129 prążkach ponad 13 kB. Granice zajmują 156 bajtów,
     * a wagę trójkąta liczy się w miejscu jednym dzieleniem.
     */
    u16 filterStart_[HYDRA_MFCC_MAX_FILTERS] = {};
    u16 filterPeak_[HYDRA_MFCC_MAX_FILTERS] = {};
    u16 filterEnd_[HYDRA_MFCC_MAX_FILTERS] = {};

    float energies_[HYDRA_MFCC_MAX_FILTERS] = {};
    float coeffs_[HYDRA_MFCC_MAX_COEFFS] = {};
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
