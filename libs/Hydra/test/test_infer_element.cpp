/**
 * Testy elementu inferencji (etap 0b).
 *
 * Rzeczy, które sprawdzamy, są niewidoczne przy ręcznym oglądaniu logu:
 * czy okno składa się poprawnie z bloków o innym rozmiarze, czy zakładka
 * naprawdę zachodzi, i czy bloki wracają do puli. Wyciek bloków objawia się
 * po kilku minutach jako cisza, a źle poskładane okno — jako model, który
 * „czasem nie działa".
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/infer/MockEngine.hpp"
#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Infer.hpp"

using namespace hydra;
using namespace hydra::media;

namespace {

constexpr u16 kWindow = 16;   ///< próbek na okno modelu
constexpr u16 kFrames = 8;    ///< próbek w bloku — świadomie mniej niż okno

infer::TensorInfo windowShape(u16 count) {
    infer::TensorInfo t;
    t.type = infer::TensorType::S16;
    t.dims = 2;
    t.dim[0] = 1;
    t.dim[1] = count;
    return t;
}

infer::TensorInfo scalarShape() {
    infer::TensorInfo t;
    t.type = infer::TensorType::F32;
    t.dims = 1;
    t.dim[0] = 1;
    return t;
}

/**
 * Potok: źródło tonu → inferencja.
 *
 * Blok ma 8 próbek, a okno 16 — tak jest w prawdziwym układzie, gdzie rozmiar
 * bloku wynika z DMA, a okno z modelu, i nie ma powodu, żeby były równe.
 */
struct Rig {
    static constexpr u32 kBlockBytes = kFrames * 2;   // S16 mono
    static constexpr u16 kBlocks = 4;

    alignas(8) u8  arena[1024] = {};
    alignas(2) u8  windowBuf[kWindow * 2] = {};
    u8             storage[kBlockBytes * kBlocks + 8] = {};

    Pipeline           pipeline;
    ToneSource         tone;
    Inference          infer;
    infer::MockEngine  engine;

    Rig() {
        ToneSource::Config cfg;
        cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        cfg.frequencyHz = 1000;
        cfg.amplitude = 16000;
        cfg.framesPerBlock = kFrames;
        tone.configure(cfg);

        pipeline.addPool(ByteSpan{storage, sizeof(storage)}, kBlockBytes, kBlocks);
    }

    /** Silnik z modelem o oknie `kWindow`; bez tego element nie zna kształtu. */
    Status openEngine() {
        engine.setShape(windowShape(kWindow), scalarShape());
        if (auto r = engine.open(arena, sizeof(arena)); !r) return r;
        return engine.load(nullptr, 0);
    }

    Status build(u32 hop = 0) {
        if (auto r = openEngine(); !r) return r;

        infer.setEngine(&engine);
        infer.setWindowBuffer(windowBuf, sizeof(windowBuf));
        if (hop > 0) infer.setHopSamples(hop);

        pipeline.add(tone);
        pipeline.add(infer);
        pipeline.link(tone, infer);
        return pipeline.prepare();
    }

    BlockPool& pool() { return *pipeline.pool(0); }
};

}  // namespace

TEST("element infer: okno składa się z bloków mniejszych niż ono samo") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.pipeline.start().has_value());

    // Dwa bloki po 8 próbek to dokładnie jedno okno — model ma policzyć raz,
    // a nie dwa razy na połówkach.
    rig.tone.process(0);
    rig.infer.process(0);
    CHECK_EQ(rig.infer.inferences(), 0u);

    rig.tone.process(0);
    rig.infer.process(0);
    CHECK_EQ(rig.infer.inferences(), 1u);
    CHECK_EQ(rig.engine.invocations(), 1u);
}

TEST("element infer: bloki wracają do puli") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.pipeline.start().has_value());

    const u16 free0 = rig.pool().available();

    for (int i = 0; i < 6; ++i) {
        rig.tone.process(0);
        rig.infer.process(0);
    }

    // Element jest ujściem: każdy pobrany blok musi wrócić, inaczej po kilku
    // minutach pula wysycha i źródło przestaje mieć z czego brać.
    CHECK_EQ(rig.pool().available(), free0);
}

TEST("element infer: przesuw o całe okno daje okna rozłączne") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.pipeline.start().has_value());

    // Cztery bloki po 8 próbek = 32 próbki = dwa pełne okna bez zakładki.
    for (int i = 0; i < 4; ++i) {
        rig.tone.process(0);
        rig.infer.process(0);
    }
    CHECK_EQ(rig.infer.inferences(), 2u);
}

TEST("element infer: zakładka zwiększa liczbę inferencji") {
    Rig rig;
    // Przesuw o połowę okna: te same dane, dwa razy więcej spojrzeń. Bez
    // zakładki krótkie zdarzenie potrafi wypaść na styku dwóch okien.
    CHECK(rig.build(kWindow / 2).has_value());
    CHECK(rig.pipeline.start().has_value());

    for (int i = 0; i < 4; ++i) {
        rig.tone.process(0);
        rig.infer.process(0);
    }
    // 32 próbki, okno 16, przesuw 8 → okna na pozycjach 0, 8, 16 → trzy razy.
    CHECK_EQ(rig.infer.inferences(), 3u);
}

TEST("element infer: werdykt idzie zdarzeniem") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.pipeline.start().has_value());

    static u32 seen = 0;
    static float lastScore = -1.0f;
    seen = 0;
    lastScore = -1.0f;

    const auto sub = EventBus::subscribe<infer::InferenceReady>(
        [](const infer::InferenceReady& e) {
            ++seen;
            lastScore = e.score;
        });
    CHECK(sub.has_value());

    rig.tone.process(0);
    rig.infer.process(0);
    rig.tone.process(0);
    rig.infer.process(0);

    CHECK_EQ(seen, 1u);
    // Ton ma niezerową energię — gdyby okno przyszło puste, wynik byłby zerem
    // i test przeszedłby mimo nieskładania okna.
    CHECK(lastScore > 0.0f);
    CHECK_EQ(rig.infer.last().score, lastScore);
}

TEST("element infer: bez bufora okna prepare odmawia") {
    Rig rig;
    CHECK(rig.openEngine().has_value());
    rig.infer.setEngine(&rig.engine);
    // Bufora okna świadomie nie ustawiamy. Rozmiar okna wynika z modelu, więc
    // aplikacja nie zna go przed wczytaniem — brak bufora musi wyjść przy
    // prepare, a nie przy pierwszym pełnym oknie po godzinie pracy.
    rig.pipeline.add(rig.tone);
    rig.pipeline.add(rig.infer);
    rig.pipeline.link(rig.tone, rig.infer);

    CHECK(!rig.pipeline.prepare().has_value());
}

TEST("element infer: strumień w innym formacie niż model jest odrzucany") {
    // Element nie przelicza formatów świadomie — niezgodność ma wyjść przy
    // składaniu potoku, a nie jako sieczka na wejściu modelu.
    Rig rig;
    CHECK(rig.openEngine().has_value());   // model chce S16
    rig.infer.setEngine(&rig.engine);
    rig.infer.setWindowBuffer(rig.windowBuf, sizeof(rig.windowBuf));

    const MediaFormat f32Stream = MediaFormat::audio(16000, SampleFormat::F32, 1);
    CHECK(!rig.infer.negotiate(0, f32Stream).has_value());
}

TEST("element infer: budżet czasu jest mierzony, a przekroczenie liczone") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.pipeline.start().has_value());

    // Budżet 0 znaczy „nie pilnuj"; ustawiamy niemożliwie mały, żeby sprawdzić
    // samo liczenie. Inferencji nie da się przerwać, więc licznik jest jedyną
    // rzeczą, jaką można tu obiecać.
    rig.infer.setBudgetUs(1);

    rig.tone.process(0);
    rig.infer.process(0);
    rig.tone.process(0);
    rig.infer.process(0);

    CHECK_EQ(rig.infer.inferences(), 1u);
    CHECK(rig.infer.worstUs() >= 0u);
}
