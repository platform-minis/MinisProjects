/**
 * Testy pełnego łańcucha rozpoznawania (etap 3).
 *
 *     źródło → MFCC → model → zdarzenie
 *
 * Trzy części były dotąd sprawdzane osobno. Tu chodzi o to, co widać dopiero
 * po ich połączeniu: czy wektory cech składają się w okno modelu bez
 * przesunięcia, czy stos ramek jest tym, czym ma być, i czy werdykt wychodzi
 * raz na okno — a nie raz na wektor albo raz na blok.
 *
 * ## Czego tu nie ma i dlaczego
 *
 * Prawdziwego modelu mowy. Model `micro_speech` z TFLM oczekuje 40 kanałów
 * ze swojego własnego frontendu, w swojej kwantyzacji i swoim układzie ramek
 * — podanie mu tych MFCC dałoby liczby, które nie znaczą nic. Model liczący
 * na tych cechach trzeba wytrenować na zbiorze nagrań, a to jest praca poza
 * urządzeniem.
 *
 * Silnik jest więc atrapowy, ale **łańcuch jest prawdziwy**: te same elementy,
 * te same pule, ta sama droga danych, którą pojedzie wytrenowany model.
 * Podmiana `MockEngine` na `TflmEngine` to jedna linijka — sprawdzone
 * w `test_tflm.cpp`.
 */

#include "hydra_test.hpp"

#include <math.h>
#include <string.h>

#include "hydra/infer/MockEngine.hpp"
#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Infer.hpp"
#include "hydra/media/elements/Mfcc.hpp"
#include "hydra/media/Pipeline.hpp"

using namespace hydra;
using namespace hydra::media;

namespace {

constexpr u8  kCoeffs = 13;    ///< współczynników na ramkę
constexpr u8  kFrames = 4;     ///< ramek na okno modelu
constexpr u16 kFft    = 256;
constexpr u16 kHop    = 128;   ///< zakładka połowy okna, jak przy mowie

/** Model: stos `kFrames` ramek po `kCoeffs` współczynników. */
infer::TensorInfo modelInput() {
    infer::TensorInfo t;
    t.type = infer::TensorType::F32;
    t.dims = 3;
    t.dim[0] = 1;
    t.dim[1] = kFrames;
    t.dim[2] = kCoeffs;
    return t;
}

/** Wyjście: dwie klasy — „słowo" i „cisza". */
infer::TensorInfo modelOutput() {
    infer::TensorInfo t;
    t.type = infer::TensorType::F32;
    t.dims = 2;
    t.dim[0] = 1;
    t.dim[1] = 2;
    return t;
}

/**
 * Pełny łańcuch w jednym potoku.
 *
 * Bloki audio są krótsze niż okno MFCC, a okno MFCC krótsze niż okno modelu —
 * dokładnie jak w prawdziwym układzie, gdzie każdy z tych rozmiarów bierze się
 * skądinąd: z DMA, z akustyki i z architektury modelu.
 */
struct KwsRig {
    static constexpr u16 kAudioFrames = 64;
    static constexpr u32 kAudioBlock  = kAudioFrames * 2;

    u8 audioStorage[kAudioBlock * 8 + 8] = {};
    u8 featureStorage[kCoeffs * sizeof(float) * 4 + 8] = {};
    alignas(8) u8 arena[4096] = {};
    alignas(4) u8 windowBuf[kFrames * kCoeffs * sizeof(float)] = {};

    Pipeline          pipeline;
    ToneSource        tone;
    MfccExtractor     mfcc;
    Inference         infer;
    infer::MockEngine engine;

    Status build(u32 freqHz) {
        engine.setShape(modelInput(), modelOutput());
        if (auto r = engine.open(arena, sizeof(arena)); !r) return r;
        if (auto r = engine.load(nullptr, 0); !r) return r;

        ToneSource::Config tc;
        tc.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        tc.frequencyHz = freqHz;
        tc.amplitude = 16000;
        tc.framesPerBlock = kAudioFrames;
        if (auto r = tone.configure(tc); !r) return r;

        MfccExtractor::Config mc;
        mc.fftSize = kFft;
        mc.hopSamples = kHop;
        mc.filterCount = 26;
        mc.coeffCount = kCoeffs;
        if (auto r = mfcc.configure(mc); !r) return r;

        infer.setEngine(&engine);
        infer.setWindowBuffer(windowBuf, sizeof(windowBuf));

        pipeline.addPool(ByteSpan{audioStorage, sizeof(audioStorage)}, kAudioBlock, 8);
        pipeline.addPool(ByteSpan{featureStorage, sizeof(featureStorage)},
                         kCoeffs * sizeof(float), 4);

        pipeline.add(tone);
        pipeline.add(mfcc);
        pipeline.add(infer);
        pipeline.link(tone, mfcc);
        pipeline.link(mfcc, infer);
        return pipeline.prepare();
    }

    void pump(int blocks) {
        for (int i = 0; i < blocks; ++i) {
            tone.process(0);
            mfcc.process(0);
            infer.process(0);
        }
    }
};

}  // namespace

TEST("kws: cechy z MFCC są przyjmowane jako wejście modelu") {
    // Do etapu 2 element inferencji przyjmował wyłącznie `Audio`. Cechy są
    // dla modelu tym samym — ciągiem liczb — więc rozdzielanie tego na dwa
    // elementy oznaczałoby dwie kopie składania okna.
    KwsRig rig;
    CHECK(rig.build(1000).has_value());

    const MediaFormat features = MediaFormat::features(kCoeffs, 125000);
    CHECK(rig.infer.negotiate(0, features).has_value());
}

TEST("kws: okno modelu musi być wielokrotnością wektora cech") {
    /*
     * Gdyby okno wypadało w połowie wektora, model dostawałby ramki
     * poprzesuwane o kilka współczynników i uczył się szumu zamiast treści.
     * Objaw byłby najgorszy z możliwych: model działa, tylko gorzej.
     */
    KwsRig rig;
    CHECK(rig.build(1000).has_value());

    // Wektor 5-elementowy nie dzieli okna 4×13 bez reszty.
    const MediaFormat mismatched = MediaFormat::features(5, 125000);
    CHECK(!rig.infer.negotiate(0, mismatched).has_value());
}

TEST("kws: pełny łańcuch dowozi werdykt") {
    KwsRig rig;
    CHECK(rig.build(1000).has_value());
    CHECK(rig.pipeline.start().has_value());

    static u32 verdicts = 0;
    verdicts = 0;
    const auto sub = EventBus::subscribe<infer::InferenceReady>(
        [](const infer::InferenceReady&) { ++verdicts; });
    CHECK(sub.has_value());

    /*
     * Ile bloków trzeba: okno MFCC to 256 próbek przy przesuwie 128, więc
     * pierwsza ramka wychodzi po 256 próbkach, każda następna po 128.
     * Cztery ramki to 256 + 3×128 = 640 próbek, czyli 10 bloków po 64.
     */
    rig.pump(10);

    CHECK(rig.mfcc.windows() >= kFrames);
    CHECK_EQ(rig.infer.inferences(), 1u);
    CHECK_EQ(verdicts, 1u);
}

TEST("kws: werdykt wychodzi raz na okno modelu, nie raz na ramkę") {
    // Cztery ramki na okno: model ma policzyć raz na cztery wektory cech,
    // a nie za każdym razem, gdy MFCC coś wypuści.
    KwsRig rig;
    CHECK(rig.build(1000).has_value());
    CHECK(rig.pipeline.start().has_value());

    rig.pump(10);
    const u32 afterFirst = rig.infer.inferences();
    const u32 framesAfterFirst = rig.mfcc.windows();

    rig.pump(8);   // kolejne 512 próbek → 4 kolejne ramki
    CHECK(rig.mfcc.windows() > framesAfterFirst);
    CHECK_EQ(rig.infer.inferences(), afterFirst + 1u);
}

TEST("kws: werdykt niesie czas liczenia") {
    KwsRig rig;
    CHECK(rig.build(1000).has_value());
    CHECK(rig.pipeline.start().has_value());

    rig.pump(10);
    CHECK_EQ(rig.infer.inferences(), 1u);

    // Bez pomiaru nie da się dobrać budżetu ani powiedzieć, czy model zdąży
    // między oknami — a to jest pierwsze pytanie przy wyborze modelu na MCU.
    const auto& last = rig.infer.last();
    CHECK_STR(last.element, "infer");
    CHECK(rig.infer.worstUs() >= last.elapsedUs);
}

TEST("kws: bloki wracają do obu pul") {
    KwsRig rig;
    CHECK(rig.build(1000).has_value());
    CHECK(rig.pipeline.start().has_value());

    const u16 audioFree = rig.pipeline.pool(0)->available();
    const u16 featureFree = rig.pipeline.pool(1)->available();

    rig.pump(20);

    // Dwie pule w łańcuchu, dwa miejsca na wyciek. Objaw jest ten sam
    // i pojawia się po kilku minutach: potok cichnie bez błędu.
    CHECK_EQ(rig.pipeline.pool(0)->available(), audioFree);
    CHECK_EQ(rig.pipeline.pool(1)->available(), featureFree);
}

TEST("kws: cisza i ton dają różne werdykty") {
    /*
     * Sprawdzenie, że przez cały łańcuch płyną **dane**, a nie zera.
     *
     * Atrapa liczy energię okna cech, więc werdykt zależy od tego, co MFCC
     * naprawdę policzyło. Gdyby gdziekolwiek po drodze okno przychodziło
     * puste, oba wyniki byłyby identyczne — i wszystkie pozostałe testy
     * przeszłyby mimo to.
     */
    float loud = 0.0f;
    float silent = 0.0f;

    {
        KwsRig rig;
        CHECK(rig.build(1000).has_value());
        CHECK(rig.pipeline.start().has_value());
        rig.pump(10);
        loud = rig.infer.last().score;
    }
    {
        KwsRig rig;
        CHECK(rig.build(1000).has_value());
        ToneSource::Config tc;
        tc.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        tc.frequencyHz = 1000;
        tc.amplitude = 0;   // cisza
        tc.framesPerBlock = KwsRig::kAudioFrames;
        CHECK(rig.tone.configure(tc).has_value());
        CHECK(rig.pipeline.start().has_value());
        rig.pump(10);
        silent = rig.infer.last().score;
    }

    CHECK(fabsf(loud - silent) > 0.01f);
}
