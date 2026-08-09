/**
 * Testy elementów audio na sprzęcie (etap 2).
 *
 * Atrapa I2S ma pętlę zwrotną, więc da się tu zrobić rzecz, której na płytce
 * nie zrobi się bez oscyloskopu: przepuścić ton przez wyjście, wciągnąć go
 * wejściem i porównać próbki co do bitu.
 *
 * Sprawdzamy przede wszystkim rzeczy, które na sprzęcie objawiają się jako
 * „coś nie gra": bufory zatrzymane u sterownika, tempo liczone z wywołań
 * zamiast z czasu i rozjazd między formatem potoku a ustawieniami kontrolera.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/hal/Mock.hpp"
#include "hydra/media/elements/Audio.hpp"
#include "hydra/media/elements/Basic.hpp"

using namespace hydra;
using namespace hydra::media;

namespace {

hal::mock::Backend& freshHal() {
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    return hal::mock::backend();
}

hal::I2sConfig i2sCfg(u32 rate = 16000, u8 channels = 1) {
    hal::I2sConfig cfg;
    cfg.sampleRate = rate;
    cfg.bitsPerSample = 16;
    cfg.channels = channels;
    cfg.bclk = 1;
    cfg.ws   = 2;
    cfg.dout = 3;
    cfg.din  = 4;
    return cfg;
}

/** Ton → I2S out, wszystko w jednej domenie. */
struct OutRig {
    static constexpr u16 kFrames = 64;
    static constexpr u32 kBlock  = kFrames * 2;

    hal::mock::Backend& hal_;
    u8         storage[kBlock * 6 + 32] = {};
    Pipeline   pipeline;
    ToneSource tone;
    I2sSink    sink;

    OutRig() : hal_(freshHal()), sink(hal::Hal::i2s()) {
        ToneSource::Config cfg;
        cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        cfg.frequencyHz = 1000;
        cfg.amplitude = 10000;
        cfg.framesPerBlock = kFrames;
        tone.configure(cfg);

        sink.configure(i2sCfg());
        pipeline.addPool(ByteSpan{storage, sizeof(storage)}, kBlock, 6, 32);
        pipeline.add(tone);
        pipeline.add(sink);
        pipeline.link(tone, sink);
    }

    BlockPool& pool() { return *pipeline.pool(0); }
};

}  // namespace

// ---------------------------------------------------------------------------
// I2S — wyjście
// ---------------------------------------------------------------------------

TEST("audio: I2S wypuszcza próbki i oddaje bufory do puli") {
    OutRig rig;
    REQUIRE(rig.pipeline.start().has_value());

    for (int i = 0; i < 20; ++i) rig.pipeline.stepAll(0);

    CHECK(rig.sink.submitted() >= 15);
    CHECK(rig.hal_.i2s.captured().size() >= 15 * OutRig::kBlock);

    // W obiegu zostaje najwyżej tyle bloków, ile sterownik trzyma naraz —
    // gdyby element którykolwiek gubił, po dwudziestu krokach pula byłaby
    // pusta, a nie niepełna o jeden.
    CHECK(rig.pool().available() >= rig.pool().capacityBlocks() - HYDRA_MEDIA_I2S_INFLIGHT);

    // Po zatrzymaniu wszystko wraca — to jest właściwy sprawdzian szczelności.
    rig.pipeline.stop();
    CHECK_EQ(static_cast<int>(rig.pool().available()),
             static_cast<int>(rig.pool().capacityBlocks()));
}

TEST("audio: bufory zatrzymane przez sterownik wracają dopiero po zwolnieniu") {
    // Prawdziwy kontroler oddaje bufor po przerwaniu, nie natychmiast. Element
    // musi to znosić i nie zgłaszać braku pamięci przy pierwszym opóźnieniu.
    OutRig rig;
    rig.hal_.i2s.setLatency(3);
    REQUIRE(rig.pipeline.start().has_value());

    for (int i = 0; i < 3; ++i) rig.pipeline.stepAll(0);
    CHECK(rig.pool().available() < rig.pool().capacityBlocks());

    for (int i = 0; i < 20; ++i) rig.pipeline.stepAll(0);
    CHECK(rig.sink.submitted() > 5);
}

TEST("audio: zatrzymanie oddaje bufory uwięzione u sterownika") {
    OutRig rig;
    rig.hal_.i2s.setLatency(5);
    REQUIRE(rig.pipeline.start().has_value());
    for (int i = 0; i < 4; ++i) rig.pipeline.stepAll(0);

    CHECK(rig.pool().available() < rig.pool().capacityBlocks());
    rig.pipeline.stop();
    CHECK_EQ(static_cast<int>(rig.pool().available()),
             static_cast<int>(rig.pool().capacityBlocks()));
}

TEST("audio: rozjazd formatu z ustawieniami I2S wychodzi przy prepare") {
    // Objawem na sprzęcie jest dźwięk o złej wysokości — wygląda jak zepsuty
    // przetwornik, a jest literówką w konfiguracji.
    freshHal();
    u8 storage[128 * 4];
    Pipeline pipeline;
    ToneSource tone;
    I2sSink sink{hal::Hal::i2s()};

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    tone.configure(cfg);
    sink.configure(i2sCfg(44100, 2));      // sprzęt ustawiony inaczej

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 4);
    pipeline.add(tone);
    pipeline.add(sink);
    pipeline.link(tone, sink);

    CHECK(!pipeline.prepare().has_value());
}

// ---------------------------------------------------------------------------
// I2S — wejście
// ---------------------------------------------------------------------------

TEST("audio: I2S wciąga próbki i nadaje im znaczniki czasu z licznika ramek") {
    hal::mock::Backend& mock = freshHal();

    // Materiał wejściowy: rozpoznawalny wzorzec.
    i16 pattern[256];
    for (size_t i = 0; i < 256; ++i) pattern[i] = static_cast<i16>(i * 37);
    mock.i2s.feed(CByteSpan{reinterpret_cast<const u8*>(pattern), sizeof(pattern)});

    u8 storage[128 * 6 + 32];
    Pipeline  pipeline;
    I2sSource source{hal::Hal::i2s()};
    MeterSink meter;

    source.configure(i2sCfg(16000, 1));
    source.setFramesPerBlock(64);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6, 32);
    pipeline.add(source);
    pipeline.add(meter);
    pipeline.link(source, meter);

    REQUIRE(pipeline.start().has_value());
    for (int i = 0; i < 10; ++i) pipeline.stepAll(0);

    CHECK(meter.blocks() >= 4);
    // 64 ramki przy 16 kHz to 4000 µs na blok; czas bierze się z licznika
    // ramek, nie z zegara systemowego, bo przetwornik ma własny kwarc.
    CHECK_EQ(static_cast<int>(meter.lastPts() % 4000), 0);
    CHECK(meter.lastPts() > 0);
}

TEST("audio: pętla zwrotna atrapy przenosi próbki bez zmiany") {
    // Ton → I2S out → (pętla) → I2S in → miernik. Na płytce wymagałoby to
    // oscyloskopu; tutaj porównujemy szczyt amplitudy.
    hal::mock::Backend& mock = freshHal();
    HYDRA_UNUSED(mock);

    OutRig out;
    REQUIRE(out.pipeline.start().has_value());
    for (int i = 0; i < 10; ++i) out.pipeline.stepAll(0);

    u8 storage[128 * 6 + 32];
    Pipeline  inPipe;
    I2sSource source{hal::Hal::i2s()};
    MeterSink meter;

    // Kanał przechodzi na odbiór; dane z pętli czekają w atrapie.
    source.configure(i2sCfg(16000, 1));
    source.setFramesPerBlock(64);
    inPipe.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6, 32);
    inPipe.add(source);
    inPipe.add(meter);
    inPipe.link(source, meter);

    REQUIRE(inPipe.start().has_value());
    for (int i = 0; i < 10; ++i) inPipe.stepAll(0);

    const u16 peak = meter.takePeak();
    CHECK(peak >= 9990 && peak <= 10000);   // amplituda tonu bez zmian
}

// ---------------------------------------------------------------------------
// PWM i DAC
// ---------------------------------------------------------------------------

TEST("audio: PWM wystawia tyle próbek, ile minęło czasu") {
    // Tempo liczone z liczby wywołań zmieniałoby wysokość dźwięku przy każdym
    // drganiu okresu taska. Budżet z zegara nie ma tej wady.
    hal::mock::Backend& mock = freshHal();

    u8 storage[128 * 4];
    Pipeline     pipeline;
    ToneSource   tone;
    PwmAudioSink pwm{hal::Hal::pwm(), 5};

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(8000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    cfg.amplitude = 20000;
    tone.configure(cfg);
    pwm.configure(80000, 10);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 4);
    pipeline.add(tone);
    pipeline.add(pwm);
    pipeline.link(tone, pwm);
    REQUIRE(pipeline.start().has_value());

    // Pierwszy krok tylko ustawia punkt odniesienia.
    pipeline.stepAll(0);
    for (u64 t = 1000; t <= 100000; t += 1000) pipeline.stepAll(t);

    // 100 ms przy 8 kHz to 800 próbek; dopuszczamy blok tolerancji.
    CHECK(pwm.samplesWritten() >= 700 && pwm.samplesWritten() <= 800);
    CHECK(mock.pwm.channel(5).active);
    CHECK_EQ(static_cast<int>(mock.pwm.channel(5).freqHz), 80000);
}

TEST("audio: brak danych na wejściu PWM to underrun, a nie przyspieszenie") {
    // Nadrabianie zaległości po przerwie oznaczałoby dźwięk odtwarzany szybciej
    // — objaw dużo gorszy od samej przerwy.
    freshHal();
    u8 storage[128 * 2];
    Pipeline     pipeline;
    ToneSource   tone;
    PwmAudioSink pwm{hal::Hal::pwm(), 5};

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(8000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    tone.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 2);
    pipeline.add(tone);
    pipeline.add(pwm);
    pipeline.link(tone, pwm);
    REQUIRE(pipeline.start().has_value());

    pwm.process(0);
    // Skok o sekundę bez ani jednego bloku na wejściu.
    pwm.process(1000000);

    CHECK(pwm.starved() > 0);
    CHECK_EQ(static_cast<int>(pwm.samplesWritten()), 0);
}

TEST("audio: DAC dostaje próbki przeskalowane do swojej rozdzielczości") {
    hal::mock::Backend& mock = freshHal();

    u8 storage[128 * 4];
    Pipeline     pipeline;
    ToneSource   tone;
    DacAudioSink dac{hal::Hal::dac(), 0};

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(8000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    cfg.amplitude = 32000;
    tone.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 4);
    pipeline.add(tone);
    pipeline.add(dac);
    pipeline.link(tone, dac);
    REQUIRE(pipeline.start().has_value());

    pipeline.stepAll(0);
    for (u64 t = 1000; t <= 20000; t += 1000) pipeline.stepAll(t);

    CHECK(dac.samplesWritten() > 100);
    CHECK(mock.dac.enabled(0));
    // Atrapa ma 8 bitów, więc próbki mieszczą się w 0…255.
    CHECK(mock.dac.value(0) <= 255);
    CHECK(mock.dac.writes(0) > 100);
}

TEST("audio: zatrzymanie ustawia wyjście na ciszę, nie na zero") {
    // Skok do zera to trzask w głośniku o pełnej amplitudzie — słychać go
    // przy każdym zatrzymaniu odtwarzania.
    hal::mock::Backend& mock = freshHal();

    u8 storage[128 * 4];
    Pipeline     pipeline;
    ToneSource   tone;
    PwmAudioSink pwm{hal::Hal::pwm(), 5};

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(8000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    tone.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 4);
    pipeline.add(tone);
    pipeline.add(pwm);
    pipeline.link(tone, pwm);
    REQUIRE(pipeline.start().has_value());
    pipeline.stepAll(0);
    pipeline.stepAll(10000);

    pipeline.stop();
    CHECK_EQ(static_cast<int>(mock.pwm.channel(5).permille), 500);
}

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------

TEST("audio: ADC zbiera próbki w bloki i usuwa składową stałą") {
    // Sygnał z mikrofonu jest spolaryzowany do połowy zakresu; bez usunięcia
    // składowej stałej po wzmocnieniu natychmiast wychodzi poza skalę.
    hal::mock::Backend& mock = freshHal();
    // Atrapa podaje napięcie; 1650 mV to połowa zakresu 3,3 V, czyli
    // typowa polaryzacja mikrofonu elektretowego.
    mock.adc.setPinMv(6, 1650);

    u8 storage[256 * 4];
    Pipeline       pipeline;
    AdcAudioSource source{hal::Hal::adc(), 6};
    MeterSink      meter;

    AdcAudioSource::Config cfg;
    cfg.sampleRate = 8000;
    cfg.framesPerBlock = 64;
    cfg.removeDc = true;
    source.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 4);
    pipeline.add(source);
    pipeline.add(meter);
    pipeline.link(source, meter);
    REQUIRE(pipeline.start().has_value());

    pipeline.stepAll(0);
    for (u64 t = 1000; t <= 60000; t += 1000) pipeline.stepAll(t);

    CHECK(meter.blocks() > 0);
    // Stałe wejście po usunięciu składowej stałej daje ciszę, a nie szynę.
    CHECK(meter.takePeak() < 2000);
}
