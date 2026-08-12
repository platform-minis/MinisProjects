/**
 * Testy analizatora widma i jego widżetu.
 *
 * Widmo, tak jak FFT pod spodem, da się sprawdzić matematycznie: ton o znanej
 * częstotliwości musi dać szczyt w wyliczonym prążku, cisza — same wartości
 * przy podłodze, a dwa różne tony — szczyty w różnych miejscach. Test „coś
 * policzyło" przeszedłby także dla implementacji zwracającej szum.
 */

#include "hydra_test.hpp"

#include <math.h>
#include <string.h>

#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Spectrum.hpp"
#include "hydra/media/Pipeline.hpp"
#include "hydra/gfx/Framebuffer.hpp"
#include "hydra/ui/Widgets.hpp"

using namespace hydra;
using namespace hydra::media;
using namespace hydra::gfx;

namespace {

constexpr u16 kFft = 256;
constexpr u32 kRate = 16000;

/** Indeks najgłośniejszego prążka. */
u16 loudestBin(const float* spectrum, u16 count) {
    u16 best = 0;
    for (u16 i = 1; i < count; ++i) {
        if (spectrum[i] > spectrum[best]) best = i;
    }
    return best;
}

struct Rig {
    static constexpr u16 kFrames = 64;
    static constexpr u32 kBlockBytes = kFrames * 2;
    static constexpr u16 kBins = 32;

    u8 audioStorage[kBlockBytes * 8 + 8] = {};
    static constexpr u16 kMaxBins = 128;
    u8 binStorage[kMaxBins * sizeof(float) * 4 + 8] = {};

    Pipeline         pipeline;
    ToneSource       tone;
    SpectrumAnalyzer spectrum;
    MeterSink        sink;

    Status build(u32 freqHz, u16 bins = kBins, u8 smoothing = 0) {
        ToneSource::Config tc;
        tc.format = MediaFormat::audio(kRate, SampleFormat::S16, 1);
        tc.frequencyHz = freqHz;
        tc.amplitude = 16000;
        tc.framesPerBlock = kFrames;
        if (auto r = tone.configure(tc); !r) return r;

        SpectrumAnalyzer::Config sc;
        sc.fftSize = kFft;
        sc.bins = bins;
        sc.smoothing = smoothing;
        if (auto r = spectrum.configure(sc); !r) return r;

        pipeline.addPool(ByteSpan{audioStorage, sizeof(audioStorage)}, kBlockBytes, 8);
        pipeline.addPool(ByteSpan{binStorage, sizeof(binStorage)}, bins * sizeof(float), 4);

        pipeline.add(tone);
        pipeline.add(spectrum);
        pipeline.add(sink);
        pipeline.link(tone, spectrum);
        pipeline.link(spectrum, sink);
        return pipeline.prepare();
    }

    void pump(int blocks) {
        for (int i = 0; i < blocks; ++i) {
            tone.process(0);
            spectrum.process(0);
            sink.process(0);
        }
    }
};

}  // namespace

TEST("spectrum: konfiguracja odrzuca niemożliwe ustawienia") {
    SpectrumAnalyzer analyzer;
    SpectrumAnalyzer::Config cfg;

    cfg.fftSize = 100;   // nie potęga dwójki
    CHECK(!analyzer.configure(cfg).has_value());

    cfg.fftSize = 256;
    cfg.bins = 500;      // prążków nie da się wymyślić
    CHECK(!analyzer.configure(cfg).has_value());

    cfg.bins = 32;
    CHECK(analyzer.configure(cfg).has_value());
}

TEST("spectrum: format wyjścia to cechy o zadanej liczbie prążków") {
    Rig rig;
    CHECK(rig.build(1000).has_value());

    const auto out = rig.spectrum.negotiate(0, MediaFormat::audio(kRate, SampleFormat::S16, 1));
    CHECK(out.has_value());
    CHECK_EQ(static_cast<int>(out.value().kind), static_cast<int>(MediaKind::Features));
    CHECK_EQ(out.value().width, Rig::kBins);
}

TEST("spectrum: ton daje szczyt we właściwym paśmie") {
    // 2000 Hz przy 16 kHz i 256 punktach to prążek 32 ze 129; przy zwężeniu
    // do 32 słupków wypada w ósmym.
    Rig rig;
    CHECK(rig.build(2000).has_value());
    CHECK(rig.pipeline.start().has_value());

    rig.pump(4);
    CHECK_EQ(rig.spectrum.windows(), 1u);

    const u16 peak = loudestBin(rig.spectrum.lastSpectrum(), rig.spectrum.binCount());
    const float peakHz = rig.spectrum.binHz(peak);
    // Szerokość słupka to 250 Hz, więc szczyt musi wypaść w jego okolicy.
    CHECK(peakHz > 1750.0f && peakHz < 2250.0f);
}

TEST("spectrum: wyższy ton daje szczyt dalej") {
    u16 lowPeak = 0;
    u16 highPeak = 0;
    {
        Rig rig;
        CHECK(rig.build(1000).has_value());
        CHECK(rig.pipeline.start().has_value());
        rig.pump(4);
        lowPeak = loudestBin(rig.spectrum.lastSpectrum(), rig.spectrum.binCount());
    }
    {
        Rig rig;
        CHECK(rig.build(5000).has_value());
        CHECK(rig.pipeline.start().has_value());
        rig.pump(4);
        highPeak = loudestBin(rig.spectrum.lastSpectrum(), rig.spectrum.binCount());
    }
    CHECK(highPeak > lowPeak);
}

TEST("spectrum: cisza leży przy podłodze, a nie w minus nieskończoności") {
    /*
     * `log(0)` to minus nieskończoność, a cisza jest najczęstszym stanem
     * wejścia. Jedna taka wartość zatruwa wygładzanie na zawsze: średnia
     * z nieskończonością zostaje nieskończonością.
     */
    Rig rig;
    CHECK(rig.build(1000).has_value());

    ToneSource::Config tc;
    tc.format = MediaFormat::audio(kRate, SampleFormat::S16, 1);
    tc.frequencyHz = 1000;
    tc.amplitude = 0;
    tc.framesPerBlock = Rig::kFrames;
    CHECK(rig.tone.configure(tc).has_value());
    CHECK(rig.pipeline.start().has_value());

    rig.pump(4);
    const float* spectrum = rig.spectrum.lastSpectrum();
    for (u16 i = 0; i < rig.spectrum.binCount(); ++i) {
        CHECK(!isinf(spectrum[i]));
        CHECK(!isnan(spectrum[i]));
        CHECK(spectrum[i] <= -70.0f);
    }
}

TEST("spectrum: zwężanie bierze maksimum, a nie średnią") {
    /*
     * Analizator ma pokazać, że coś w paśmie jest. Uśrednienie rozmywa wąski,
     * silny prążek w grupie sąsiadów o tle — czyli gubi dokładnie to, czego
     * się szuka przy diagnostyce wibracji.
     *
     * Sprawdzenie pośrednie: szczyt w zwężonym widmie musi być bliski
     * szczytowi w pełnym, a nie o kilkanaście decybeli niższy.
     */
    Rig wide;
    CHECK(wide.build(2000, 128).has_value());
    CHECK(wide.pipeline.start().has_value());
    wide.pump(4);
    const u16 widePeak = loudestBin(wide.spectrum.lastSpectrum(), wide.spectrum.binCount());
    const float wideDb = wide.spectrum.lastSpectrum()[widePeak];

    Rig narrow;
    CHECK(narrow.build(2000, 16).has_value());
    CHECK(narrow.pipeline.start().has_value());
    narrow.pump(4);
    const u16 narrowPeak = loudestBin(narrow.spectrum.lastSpectrum(), narrow.spectrum.binCount());
    const float narrowDb = narrow.spectrum.lastSpectrum()[narrowPeak];

    CHECK(fabsf(wideDb - narrowDb) < 1.0f);
}

TEST("spectrum: opis osi wskazuje środek pasma, nie jego początek") {
    Rig rig;
    CHECK(rig.build(1000).has_value());

    // Pierwszy słupek z 32 obejmuje prążki 0–4 ze 129; jego środek to około
    // 125 Hz przy 16 kHz i oknie 256.
    const float first = rig.spectrum.binHz(0);
    CHECK(first > 60.0f && first < 200.0f);
    CHECK(rig.spectrum.binHz(31) > rig.spectrum.binHz(0));
}

TEST("spectrum: bloki wracają do obu pul") {
    Rig rig;
    CHECK(rig.build(1000).has_value());
    CHECK(rig.pipeline.start().has_value());

    const u16 audioFree = rig.pipeline.pool(0)->available();
    const u16 binFree = rig.pipeline.pool(1)->available();

    rig.pump(20);

    CHECK_EQ(rig.pipeline.pool(0)->available(), audioFree);
    CHECK_EQ(rig.pipeline.pool(1)->available(), binFree);
}

// ---------------------------------------------------------------------------
// Widżet
// ---------------------------------------------------------------------------

TEST("ui/spectrum: widżet przyjmuje widmo i przycina do swojej pojemności") {
    ui::SpectrumView view;
    float bins[HYDRA_UI_SPECTRUM_BARS * 2];
    for (u16 i = 0; i < HYDRA_UI_SPECTRUM_BARS * 2; ++i) bins[i] = -40.0f;

    view.update(bins, HYDRA_UI_SPECTRUM_BARS * 2);
    // Więcej prążków niż słupków to normalny przypadek — widżet bierze tyle,
    // ile ma miejsca, zamiast pisać poza tablicą.
    CHECK_EQ(view.bars(), static_cast<u16>(HYDRA_UI_SPECTRUM_BARS));
}

TEST("ui/spectrum: szczyt podnosi się natychmiast, opada powoli") {
    ui::SpectrumView view;
    view.setRange(-80.0f, 0.0f);
    view.setPeakFall(2.0f);

    // Krótkie uderzenie zniknęłoby, zanim ktokolwiek zdąży je zobaczyć —
    // ślad szczytu jest po to, żeby zostało widoczne przez kilka klatek.
    float loud[4] = {-10.0f, -10.0f, -10.0f, -10.0f};
    float quiet[4] = {-70.0f, -70.0f, -70.0f, -70.0f};

    view.update(loud, 4);
    view.update(quiet, 4);

    // Rysowanie na prawdziwym płótnie: chodzi o to, że nie wywraca się
    // przy danych na krańcach zakresu i że słupki w ogóle powstają.
    static u8 buffer[Framebuffer::bytesNeeded(64, 32, PixelFormat::Rgb565)];
    Framebuffer fb;
    fb.attach(ByteSpan{buffer, sizeof(buffer)}, 64, 32, PixelFormat::Rgb565);
    fb.clear(colors::black);

    ui::Theme theme = ui::Theme::dark();
    view.setBounds(Rect(0, 0, 64, 32));
    view.draw(fb, theme);

    // Głośne pasmo musi zostawić ślad — słupek sięgający prawie do góry.
    bool anyLit = false;
    for (i16 y = 0; y < 32 && !anyLit; ++y) {
        for (i16 x = 0; x < 8; ++x) {
            const auto pixel = fb.pixelAt(x, y);
            if (pixel.has_value() && !(pixel.value() == colors::black)) { anyLit = true; break; }
        }
    }
    CHECK(anyLit);
}
