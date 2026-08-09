/**
 * Testy rdzenia potoku multimedialnego (etap 1).
 *
 * Trzy rzeczy są tu warte sprawdzenia i wszystkie trzy są niewidoczne gołym
 * uchem: czy bloki wracają do puli, czy polityka przepełnienia gubi to, co
 * miała gubić, i czy znaczniki czasu rosną zgodnie z częstotliwością
 * próbkowania. Wyciek bloków objawia się po kilku minutach jako cisza,
 * a dryf PTS — po kilku godzinach jako rozjazd obrazu z dźwiękiem.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/media/elements/Basic.hpp"

using namespace hydra;
using namespace hydra::media;

namespace {

/** Potok tonu z pulą; domyślnie 16 kHz mono, bloki po 64 ramki. */
struct Rig {
    static constexpr u16 kFrames = 64;
    static constexpr u32 kBlock  = kFrames * 2;   // S16 mono
    static constexpr u16 kBlocks = 4;

    u8         storage[kBlock * kBlocks + 8] = {};
    Pipeline   pipeline;
    ToneSource tone;
    Gain       gain;
    MeterSink  meter;

    Rig() {
        ToneSource::Config cfg;
        cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        cfg.frequencyHz = 1000;
        cfg.amplitude = 16000;
        cfg.framesPerBlock = kFrames;
        tone.configure(cfg);

        pipeline.addPool(ByteSpan{storage, sizeof(storage)}, kBlock, kBlocks);
    }

    /** Łańcuch tone → gain → meter w jednej domenie. */
    void chain() {
        pipeline.add(tone);
        pipeline.add(gain);
        pipeline.add(meter);
        pipeline.link(tone, gain);
        pipeline.link(gain, meter);
    }

    BlockPool& pool() { return *pipeline.pool(0); }
};

}  // namespace

// ---------------------------------------------------------------------------
// Pula
// ---------------------------------------------------------------------------

TEST("media: pula wydaje i odbiera bloki bez fragmentacji") {
    u8 storage[4 * 32];
    BlockPool pool;
    REQUIRE(pool.attach(0, ByteSpan{storage, sizeof(storage)}, 32, 4).has_value());

    Block a = pool.acquire();
    Block b = pool.acquire();
    CHECK(a.valid());
    CHECK(b.valid());
    CHECK(a.data != b.data);
    CHECK_EQ(static_cast<int>(pool.available()), 2);

    pool.release(a);
    CHECK_EQ(static_cast<int>(pool.available()), 3);
    // Zwolniony uchwyt jest wyzerowany — użycie go po zwolnieniu ma być
    // widoczne od razu, a nie jako zapis pod adres oddany komu innemu.
    CHECK(!a.valid());
}

TEST("media: wyczerpana pula zgłasza się licznikiem, nie zwróconym śmieciem") {
    u8 storage[2 * 16];
    BlockPool pool;
    REQUIRE(pool.attach(0, ByteSpan{storage, sizeof(storage)}, 16, 2).has_value());

    Block a = pool.acquire();
    Block b = pool.acquire();
    Block c = pool.acquire();

    CHECK(a.valid());
    CHECK(b.valid());
    CHECK(!c.valid());
    CHECK_EQ(static_cast<int>(pool.exhausted()), 1);
    CHECK_EQ(static_cast<int>(pool.lowWater()), 0);
}

TEST("media: dodatkowe odwołanie trzyma blok do drugiego zwolnienia") {
    // To jest cały mechanizm rozgałęzienia: dwie gałęzie, jeden bufor.
    u8 storage[2 * 16];
    BlockPool pool;
    REQUIRE(pool.attach(0, ByteSpan{storage, sizeof(storage)}, 16, 2).has_value());

    Block block = pool.acquire();
    pool.retain(block);
    CHECK_EQ(static_cast<int>(pool.available()), 1);

    Block first = block;
    pool.release(first);
    CHECK_EQ(static_cast<int>(pool.available()), 1);   // wciąż zajęty

    Block second = block;
    pool.release(second);
    CHECK_EQ(static_cast<int>(pool.available()), 2);
}

TEST("media: pula wyrównuje początek i rozmiar bloku") {
    // Bufor DMA o niewyrównanym adresie kończy się na ESP32 błędem magistrali.
    u8 storage[4 * 40 + 32];
    BlockPool pool;
    REQUIRE(pool.attach(0, ByteSpan{storage + 1, sizeof(storage) - 1}, 30, 4, 16)
                .has_value());

    Block block = pool.acquire();
    REQUIRE(block.valid());
    CHECK_EQ(static_cast<int>(reinterpret_cast<uintptr_t>(block.data) % 16), 0);
    CHECK_EQ(static_cast<int>(block.capacity), 32);   // 30 zaokrąglone w górę
}

TEST("media: za mała pamięć to błąd przy podpięciu, a nie mniejsza pula") {
    u8 storage[10];
    BlockPool pool;
    CHECK(!pool.attach(0, ByteSpan{storage, sizeof(storage)}, 16, 4).has_value());
}

// ---------------------------------------------------------------------------
// Pad
// ---------------------------------------------------------------------------

TEST("media: DropOldest robi miejsce najświeższemu") {
    Pad pad;
    pad.configure(MediaFormat::audio(16000, SampleFormat::S16, 1),
                  OverflowPolicy::DropOldest);

    Block evicted;
    for (u32 i = 0; i < HYDRA_MEDIA_PAD_DEPTH; ++i) {
        Block block;
        block.pool = 0;
        block.slot = static_cast<u16>(i);
        block.data = reinterpret_cast<u8*>(1);
        CHECK(pad.push(block, evicted));
        CHECK(!evicted.valid());
    }
    CHECK(pad.full());

    Block extra;
    extra.pool = 0;
    extra.slot = 99;
    extra.data = reinterpret_cast<u8*>(1);
    CHECK(pad.push(extra, evicted));
    // Wyleciał najstarszy, wszedł nowy.
    CHECK_EQ(static_cast<int>(evicted.slot), 0);

    Block first;
    CHECK(pad.pop(first));
    CHECK_EQ(static_cast<int>(first.slot), 1);
}

TEST("media: DropNewest zachowuje kolejność, dziura powstaje na końcu") {
    Pad pad;
    pad.configure(MediaFormat{}, OverflowPolicy::DropNewest);

    Block evicted;
    for (u32 i = 0; i < HYDRA_MEDIA_PAD_DEPTH; ++i) {
        Block block;
        block.pool = 0;
        block.slot = static_cast<u16>(i);
        block.data = reinterpret_cast<u8*>(1);
        pad.push(block, evicted);
    }

    Block extra;
    extra.pool = 0;
    extra.slot = 99;
    extra.data = reinterpret_cast<u8*>(1);
    CHECK(pad.push(extra, evicted));
    CHECK_EQ(static_cast<int>(evicted.slot), 99);   // odrzucony jest nowy

    Block first;
    CHECK(pad.pop(first));
    CHECK_EQ(static_cast<int>(first.slot), 0);      // kolejność nienaruszona
}

TEST("media: Reject zostawia blok u nadawcy") {
    // Źródło ma wtedy wstrzymać produkcję, a nie oddać coś, czego nie przyjmiemy.
    Pad pad;
    pad.configure(MediaFormat{}, OverflowPolicy::Reject);

    Block evicted;
    for (u32 i = 0; i < HYDRA_MEDIA_PAD_DEPTH; ++i) {
        Block block;
        block.pool = 0;
        block.data = reinterpret_cast<u8*>(1);
        pad.push(block, evicted);
    }

    Block extra;
    extra.pool = 0;
    extra.data = reinterpret_cast<u8*>(1);
    CHECK(!pad.push(extra, evicted));
    CHECK(!evicted.valid());
}

// ---------------------------------------------------------------------------
// Potok
// ---------------------------------------------------------------------------

TEST("media: negocjacja przenosi format ze źródła do ujścia") {
    Rig rig;
    rig.chain();
    REQUIRE(rig.pipeline.prepare().has_value());

    const MediaFormat& atSink = rig.meter.input(0).format();
    CHECK_EQ(static_cast<int>(atSink.kind), static_cast<int>(MediaKind::Audio));
    CHECK_EQ(static_cast<int>(atSink.sampleRate), 16000);
    CHECK_EQ(static_cast<int>(atSink.channels), 1);
    CHECK_EQ(static_cast<int>(rig.pipeline.state()), static_cast<int>(PipelineState::Ready));
}

TEST("media: połączenie pod prąd jest odrzucane przy budowie") {
    // Kolejność rejestracji jest kierunkiem przepływu; odwrotne połączenie
    // dawałoby okres opóźnienia na każdym obiegu zamiast błędu.
    Rig rig;
    rig.pipeline.add(rig.tone);
    rig.pipeline.add(rig.meter);
    CHECK(!rig.pipeline.link(rig.meter, rig.tone).has_value());
}

TEST("media: dane przechodzą całym łańcuchem i bloki wracają do puli") {
    Rig rig;
    rig.chain();
    REQUIRE(rig.pipeline.prepare().has_value());
    REQUIRE(rig.pipeline.start().has_value());

    for (int i = 0; i < 50; ++i) rig.pipeline.stepAll(0);

    CHECK(rig.meter.blocks() >= 40);
    CHECK_EQ(static_cast<int>(rig.meter.bytes()),
             static_cast<int>(rig.meter.blocks() * Rig::kBlock));
    // Sedno testu: po pięćdziesięciu obiegach wszystkie bloki są z powrotem
    // w puli. Wyciek jednego na obieg objawiłby się ciszą po kilku minutach.
    CHECK_EQ(static_cast<int>(rig.pool().available()),
             static_cast<int>(rig.pool().capacityBlocks()));
}

TEST("media: znaczniki czasu rosną zgodnie z częstotliwością próbkowania") {
    // 64 ramki przy 16 kHz to dokładnie 4000 µs. Dryf tutaj rozjeżdża obraz
    // z dźwiękiem po godzinach, więc sprawdzamy co do mikrosekundy.
    Rig rig;
    rig.chain();
    REQUIRE(rig.pipeline.start().has_value());

    rig.pipeline.stepAll(0);
    const u64 first = rig.meter.lastPts();
    rig.pipeline.stepAll(0);
    const u64 second = rig.meter.lastPts();

    CHECK_EQ(static_cast<int>(first), 0);
    CHECK_EQ(static_cast<int>(second), 4000);
}

TEST("media: wzmocnienie skaluje i przycina, zamiast zawijać") {
    // Zawinięcie i16 daje trzask o pełnej amplitudzie — najgłośniejszy możliwy
    // objaw najcichszego błędu.
    Rig rig;
    rig.chain();
    rig.gain.setGainQ8_8(1024);          // ×4 przy amplitudzie 16000
    REQUIRE(rig.pipeline.start().has_value());

    for (int i = 0; i < 10; ++i) rig.pipeline.stepAll(0);

    // 32768, a nie 32767: szczyt wypada na ujemnej szynie (−32768), której
    // wartość bezwzględna nie mieści się w i16. Miernik liczy ją na 32 bitach
    // i oddaje jako u16 — gdyby liczył na i16, wynik zawinąłby się na ujemny.
    CHECK_EQ(static_cast<int>(rig.meter.takePeak()), 32768);
    CHECK(rig.gain.clipped() > 0);
}

TEST("media: wzmocnienie neutralne przepuszcza amplitudę bez zmian") {
    Rig rig;
    rig.chain();
    REQUIRE(rig.pipeline.start().has_value());

    for (int i = 0; i < 10; ++i) rig.pipeline.stepAll(0);

    const u16 peak = rig.meter.takePeak();
    // Amplituda 16000 przy tablicy sinusa o skali 32767 — dopuszczamy próbkę
    // różnicy na zaokrągleniu.
    CHECK(peak >= 15990 && peak <= 16000);
}

TEST("media: rozgałęzienie karmi obie gałęzie bez kopiowania bufora") {
    Rig rig;
    Tee       tee;
    MeterSink second;

    rig.pipeline.add(rig.tone);
    rig.pipeline.add(tee);
    rig.pipeline.add(rig.meter);
    rig.pipeline.add(second);
    rig.pipeline.link(rig.tone, tee);
    rig.pipeline.link(tee, 0, rig.meter, 0);
    rig.pipeline.link(tee, 1, second, 0);

    REQUIRE(rig.pipeline.start().has_value());
    for (int i = 0; i < 30; ++i) rig.pipeline.stepAll(0);

    CHECK(rig.meter.blocks() > 5);
    CHECK_EQ(static_cast<int>(rig.meter.blocks()), static_cast<int>(second.blocks()));
    // Obie gałęzie zwolniły swoje odwołania — pula jest z powrotem pełna.
    CHECK_EQ(static_cast<int>(rig.pool().available()),
             static_cast<int>(rig.pool().capacityBlocks()));
}

TEST("media: koniec strumienia dociera do ujścia") {
    Rig rig;
    rig.chain();
    REQUIRE(rig.pipeline.start().has_value());

    rig.pipeline.stepAll(0);
    rig.tone.finish();
    for (int i = 0; i < 5; ++i) rig.pipeline.stepAll(0);

    CHECK(rig.meter.sawEos());
}

TEST("media: zatrzymanie oddaje bloki uwięzione w kolejkach") {
    // Bez tego drugi start zaczyna z pulą uszczuploną o zawartość kolejek —
    // i po kilku cyklach start/stop źródło nie ma z czego brać.
    Rig rig;
    rig.pipeline.add(rig.tone);
    rig.pipeline.add(rig.meter);
    rig.pipeline.link(rig.tone, rig.meter);

    REQUIRE(rig.pipeline.start().has_value());
    // Same kroki źródła: bloki zostają w kolejce ujścia, nikt ich nie odbiera.
    for (int i = 0; i < 3; ++i) rig.tone.process(0);
    CHECK(rig.pool().available() < rig.pool().capacityBlocks());

    rig.pipeline.stop();
    CHECK_EQ(static_cast<int>(rig.pool().available()),
             static_cast<int>(rig.pool().capacityBlocks()));
}

TEST("media: potok bez puli nie startuje po cichu") {
    Pipeline pipeline;
    ToneSource tone;
    MeterSink  meter;

    ToneSource::Config cfg;
    cfg.framesPerBlock = 64;
    tone.configure(cfg);

    pipeline.add(tone);
    pipeline.add(meter);
    pipeline.link(tone, meter);

    CHECK(!pipeline.prepare().has_value());
    CHECK_EQ(static_cast<int>(pipeline.state()), static_cast<int>(PipelineState::Idle));
}

TEST("media: domeny rozdzielają, co jest przetwarzane w danym kroku") {
    // Bez tego cały potok chodziłby w jednym rytmie: źródło co 1 ms razem
    // z zapisem do pliku co 20 ms.
    Rig rig;
    rig.pipeline.add(rig.tone, 0);
    rig.pipeline.add(rig.gain, 1);
    rig.pipeline.add(rig.meter, 1);
    rig.pipeline.link(rig.tone, rig.gain);
    rig.pipeline.link(rig.gain, rig.meter);

    REQUIRE(rig.pipeline.start().has_value());

    rig.pipeline.step(0, 0);            // tylko źródło
    CHECK_EQ(static_cast<int>(rig.meter.blocks()), 0);

    rig.pipeline.step(1, 0);            // filtr i ujście
    CHECK_EQ(static_cast<int>(rig.meter.blocks()), 1);
}

TEST("media: format obrazu liczy rozmiar klatki, a JPEG zostaje zmienny") {
    const MediaFormat raw = MediaFormat::video(FrameFormat::Rgb565, 320, 240);
    CHECK_EQ(static_cast<int>(raw.frameBytes()), 320 * 240 * 2);

    const MediaFormat jpeg = MediaFormat::video(FrameFormat::Jpeg, 640, 480);
    CHECK_EQ(static_cast<int>(jpeg.frameBytes()), 0);
    CHECK(jpeg.valid());
}

TEST("media: porównanie formatów pomija zmienną liczbę klatek") {
    // Kamera podaje ją orientacyjnie i potrafi się wahać przy zmianie
    // oświetlenia; niezgodność tutaj oznaczałaby potok odmawiający startu.
    const MediaFormat a = MediaFormat::video(FrameFormat::Yuv422, 320, 240, 30000);
    const MediaFormat b = MediaFormat::video(FrameFormat::Yuv422, 320, 240, 24000);
    CHECK(a.equals(b));

    const MediaFormat c = MediaFormat::video(FrameFormat::Yuv422, 640, 480, 30000);
    CHECK(!a.equals(c));
}
