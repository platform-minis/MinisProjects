/**
 * Testy FFT i MFCC (etap 2).
 *
 * Widmo i cechy da się sprawdzić **matematycznie**, i tak są tu sprawdzane:
 * sinusoida o znanej częstotliwości musi dać pik w wyliczonym prążku, cisza —
 * same zera, a ton wysoki i niski muszą dać różne cechy. Test „policzyło się
 * bez błędu" przeszedłby także dla implementacji zwracającej śmieci.
 */

#include "hydra_test.hpp"

#include <math.h>
#include <string.h>

#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Mfcc.hpp"
#include "hydra/media/Pipeline.hpp"
#include "hydra/util/Fft.hpp"
#include "hydra/core/RealMath.hpp"

using namespace hydra;
using namespace hydra::media;

namespace {

/** Wypełnia bufor sinusoidą o zadanej częstotliwości. */
void fillSine(float* out, u16 n, float freqHz, float sampleRate, float amplitude = 1.0f) {
    for (u16 i = 0; i < n; ++i) {
        out[i] = amplitude * sinf(kTwoPi * freqHz * static_cast<float>(i) / sampleRate);
    }
}

/** Indeks największego prążka. */
u16 peakBin(const float* power, u16 bins) {
    u16 best = 0;
    for (u16 i = 1; i < bins; ++i) {
        if (power[i] > power[best]) best = i;
    }
    return best;
}

}  // namespace

// ---------------------------------------------------------------------------
// FFT
// ---------------------------------------------------------------------------

TEST("fft: rozmiar musi być potęgą dwójki") {
    CHECK(util::isValidFftSize(256));
    CHECK(util::isValidFftSize(4));
    CHECK(!util::isValidFftSize(100));
    CHECK(!util::isValidFftSize(2));   // za mało na cokolwiek sensownego
    CHECK(!util::isValidFftSize(0));
}

TEST("fft: sinusoida daje pik w swoim prążku") {
    constexpr u16 kN = 256;
    constexpr float kRate = 16000.0f;
    // 1000 Hz przy 16 kHz i 256 punktach wypada dokładnie w prążku 16 —
    // częstotliwość dobrana tak, żeby nie było przecieku między prążkami.
    constexpr float kFreq = 1000.0f;

    float samples[kN];
    float scratch[kN];
    float power[util::spectrumBins(kN)];
    fillSine(samples, kN, kFreq, kRate);

    CHECK(util::powerSpectrum(samples, scratch, kN, power));

    const u16 expected = static_cast<u16>(kFreq * kN / kRate);
    CHECK_EQ(peakBin(power, util::spectrumBins(kN)), expected);
}

TEST("fft: wyższy ton daje pik dalej") {
    constexpr u16 kN = 256;
    constexpr float kRate = 16000.0f;

    float samples[kN], scratch[kN], power[util::spectrumBins(kN)];

    fillSine(samples, kN, 500.0f, kRate);
    CHECK(util::powerSpectrum(samples, scratch, kN, power));
    const u16 low = peakBin(power, util::spectrumBins(kN));

    fillSine(samples, kN, 4000.0f, kRate);
    CHECK(util::powerSpectrum(samples, scratch, kN, power));
    const u16 high = peakBin(power, util::spectrumBins(kN));

    CHECK(high > low);
}

TEST("fft: składowa stała siedzi w prążku zerowym") {
    constexpr u16 kN = 64;
    float samples[kN], scratch[kN], power[util::spectrumBins(kN)];
    for (u16 i = 0; i < kN; ++i) samples[i] = 1.0f;

    CHECK(util::powerSpectrum(samples, scratch, kN, power));
    CHECK_EQ(peakBin(power, util::spectrumBins(kN)), 0u);
}

TEST("fft: cisza daje zerowe widmo") {
    constexpr u16 kN = 64;
    float samples[kN] = {}, scratch[kN], power[util::spectrumBins(kN)];

    CHECK(util::powerSpectrum(samples, scratch, kN, power));
    for (u16 i = 0; i < util::spectrumBins(kN); ++i) CHECK(power[i] < 1e-6f);
}

TEST("fft: zły rozmiar jest odrzucany, a nie liczony") {
    // Cicha zgoda dałaby widmo, które wygląda poprawnie i jest nieprawdą.
    float samples[100] = {}, scratch[100], power[51];
    CHECK(!util::powerSpectrum(samples, scratch, 100, power));
}

TEST("fft: okno Hanninga wygasza krańce") {
    constexpr u16 kN = 64;
    float samples[kN];
    for (u16 i = 0; i < kN; ++i) samples[i] = 1.0f;

    util::applyHann(samples, kN);
    // Bez okna każdy blok kończy się skokiem do zera, a skok rozlewa się
    // po całym widmie.
    CHECK(samples[0] < 0.001f);
    CHECK(samples[kN - 1] < 0.001f);
    CHECK(samples[kN / 2] > 0.99f);
}

// ---------------------------------------------------------------------------
// MFCC
// ---------------------------------------------------------------------------

namespace {

/** Potok: źródło tonu → MFCC. Blok krótszy niż okno, jak w prawdziwym I2S. */
struct MfccRig {
    static constexpr u16 kFrames = 64;
    static constexpr u32 kBlockBytes = kFrames * 2;
    static constexpr u16 kBlocks = 8;

    u8 storage[kBlockBytes * kBlocks + 8] = {};
    u8 featureStorage[13 * sizeof(float) * 4 + 8] = {};

    Pipeline      pipeline;
    ToneSource    tone;
    MfccExtractor mfcc;
    MeterSink     sink;

    Status build(u32 freqHz, u16 fftSize = 256, u16 hop = 0) {
        ToneSource::Config cfg;
        cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        cfg.frequencyHz = freqHz;
        cfg.amplitude = 16000;
        cfg.framesPerBlock = kFrames;
        if (auto r = tone.configure(cfg); !r) return r;

        MfccExtractor::Config mc;
        mc.fftSize = fftSize;
        mc.hopSamples = hop;
        mc.filterCount = 26;
        mc.coeffCount = 13;
        if (auto r = mfcc.configure(mc); !r) return r;

        pipeline.addPool(ByteSpan{storage, sizeof(storage)}, kBlockBytes, kBlocks);
        pipeline.addPool(ByteSpan{featureStorage, sizeof(featureStorage)},
                         13 * sizeof(float), 4);

        pipeline.add(tone);
        pipeline.add(mfcc);
        pipeline.add(sink);
        pipeline.link(tone, mfcc);
        pipeline.link(mfcc, sink);
        return pipeline.prepare();
    }

    /** Przepuszcza tyle bloków, żeby powstało co najmniej jedno okno. */
    void pump(int blocks) {
        for (int i = 0; i < blocks; ++i) {
            tone.process(0);
            mfcc.process(0);
            sink.process(0);
        }
    }
};

}  // namespace

TEST("mfcc: konfiguracja odrzuca niemożliwe ustawienia") {
    MfccExtractor mfcc;
    MfccExtractor::Config cfg;

    cfg.fftSize = 100;   // nie potęga dwójki
    CHECK(!mfcc.configure(cfg).has_value());

    cfg.fftSize = 256;
    cfg.coeffCount = 30;
    cfg.filterCount = 26;
    // DCT nie tworzy informacji — więcej współczynników niż filtrów nie ma
    // z czego policzyć.
    CHECK(!mfcc.configure(cfg).has_value());

    cfg.coeffCount = 13;
    CHECK(mfcc.configure(cfg).has_value());
}

TEST("mfcc: format wyjścia to cechy, nie audio") {
    MfccRig rig;
    CHECK(rig.build(1000).has_value());

    const auto out = rig.mfcc.negotiate(0, MediaFormat::audio(16000, SampleFormat::S16, 1));
    CHECK(out.has_value());
    CHECK_EQ(static_cast<int>(out.value().kind), static_cast<int>(MediaKind::Features));
    CHECK_EQ(out.value().width, 13u);
    // Wektor cech jest jednostką w całości: blok z połową byłby danymi
    // nie do zinterpretowania.
    CHECK_EQ(out.value().unitBytes(), 13u * sizeof(float));
}

TEST("mfcc: strumień wielokanałowy jest odrzucany") {
    // Mieszanie kanałów to osobna decyzja i osobny element.
    MfccExtractor mfcc;
    MfccExtractor::Config cfg;
    CHECK(mfcc.configure(cfg).has_value());
    CHECK(!mfcc.negotiate(0, MediaFormat::audio(16000, SampleFormat::S16, 2)).has_value());
}

TEST("mfcc: okno składa się z bloków i daje wektor cech") {
    MfccRig rig;
    CHECK(rig.build(1000).has_value());
    CHECK(rig.pipeline.start().has_value());

    // Cztery bloki po 64 próbki to dokładnie jedno okno 256.
    rig.pump(4);
    CHECK_EQ(rig.mfcc.windows(), 1u);
    // Wektor musi dopłynąć do odbiorcy, a nie tylko powstać.
    CHECK(rig.sink.blocks() >= 1u);
    CHECK_EQ(rig.sink.bytes(), 13u * sizeof(float));
}

TEST("mfcc: ton niski i wysoki dają różne cechy") {
    /*
     * Sedno całego elementu. Implementacja zwracająca stałą przeszłaby każdy
     * test na kształt i rozmiar — nie przejdzie tego.
     */
    float lowCoeffs[13] = {};
    float highCoeffs[13] = {};

    {
        MfccRig rig;
        CHECK(rig.build(300).has_value());
        CHECK(rig.pipeline.start().has_value());
        rig.pump(4);
        memcpy(lowCoeffs, rig.mfcc.lastCoeffs(), sizeof(lowCoeffs));
    }
    {
        MfccRig rig;
        CHECK(rig.build(4000).has_value());
        CHECK(rig.pipeline.start().has_value());
        rig.pump(4);
        memcpy(highCoeffs, rig.mfcc.lastCoeffs(), sizeof(highCoeffs));
    }

    // Pierwszy współczynnik niesie głośność — obie próbki mają tę samą
    // amplitudę, więc różnicy trzeba szukać w kształcie widma, czyli dalej.
    float maxDiff = 0.0f;
    for (int i = 1; i < 13; ++i) {
        const float diff = fabsf(lowCoeffs[i] - highCoeffs[i]);
        if (diff > maxDiff) maxDiff = diff;
    }
    CHECK(maxDiff > 1.0f);
}

TEST("mfcc: cisza nie daje NaN") {
    /*
     * `log(0)` to minus nieskończoność, a jedna taka wartość zatruwa całe DCT
     * i model dostaje wektor złożony z NaN. Podłoga energii istnieje właśnie
     * po to — a cisza jest najczęstszym stanem mikrofonu.
     */
    MfccRig rig;
    CHECK(rig.build(1000).has_value());
    CHECK(rig.pipeline.start().has_value());

    // Amplituda zero: źródło produkuje ciszę.
    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    cfg.frequencyHz = 1000;
    cfg.amplitude = 0;
    cfg.framesPerBlock = MfccRig::kFrames;
    CHECK(rig.tone.configure(cfg).has_value());

    rig.pump(4);
    CHECK_EQ(rig.mfcc.windows(), 1u);

    const float* coeffs = rig.mfcc.lastCoeffs();
    for (int i = 0; i < 13; ++i) {
        CHECK(!isnan(coeffs[i]));
        CHECK(!isinf(coeffs[i]));
    }
}

TEST("mfcc: zakładka zwiększa liczbę okien") {
    MfccRig rig;
    // Przesuw o połowę okna — głoska krótsza od okna wypadłaby inaczej
    // na styku dwóch i model nie zobaczyłby jej ani razu w całości.
    CHECK(rig.build(1000, 256, 128).has_value());
    CHECK(rig.pipeline.start().has_value());

    rig.pump(8);   // 512 próbek
    // Okna na pozycjach 0, 128, 256 — trzy razy zamiast dwóch.
    CHECK_EQ(rig.mfcc.windows(), 3u);
}

TEST("mfcc: bloki wracają do puli") {
    MfccRig rig;
    CHECK(rig.build(1000).has_value());
    CHECK(rig.pipeline.start().has_value());

    BlockPool& audio = *rig.pipeline.pool(0);
    const u16 free0 = audio.available();

    rig.pump(12);

    // Wyciek objawia się po kilku minutach jako cisza — a wtedy nikt już nie
    // kojarzy go z MFCC.
    CHECK_EQ(audio.available(), free0);
}
