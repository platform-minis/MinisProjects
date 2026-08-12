/**
 * Testy źródła muzyki modułowej.
 *
 * Moduł do testu jest **budowany w pamięci**, a nie wpisany jako tablica
 * bajtów. Najmniejszy prawdziwy `.mod` z repozytorium pocketmoda waży 118 kB —
 * wpisanie go do testów obciążyłoby repozytorium bez powodu, skoro format
 * ma stałą, prostą strukturę: 1084 bajty nagłówka, potem wzorce po 1024 bajty
 * i próbki instrumentów.
 *
 * Test sprawdza więc to, co naprawdę należy do Hydry: czy adapter poprawnie
 * przekłada `float` z odtwarzacza na `i16` potoku, czy bloki wracają do puli
 * i czy znacznik czasu rośnie zgodnie z liczbą ramek. Poprawność samego
 * odtwarzania nut jest sprawą pocketmoda i jego własnych testów.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Tracker.hpp"
#include "hydra/media/Pipeline.hpp"

using namespace hydra;
using namespace hydra::media;

namespace {

/*
 * Najmniejszy moduł, jaki pocketmod przyjmie.
 *
 * Układ pliku Protrackera: 20 bajtów nazwy, 31 instrumentów po 30 bajtów,
 * długość ścieżki, bajt niegdyś oznaczający tempo, 128 pozycji ścieżki,
 * czteroznakowa sygnatura — razem 1084 bajty. Potem wzorce po 1024 bajty
 * (64 wiersze × 4 kanały × 4 bajty) i próbki instrumentów.
 */
constexpr u32 kHeaderBytes  = 1084;
constexpr u32 kPatternBytes = 1024;
constexpr u32 kSampleBytes  = 64;
constexpr u32 kModBytes     = kHeaderBytes + kPatternBytes + kSampleBytes;

/** Buduje moduł z jedną nutą na pierwszym kanale. */
void buildModule(u8* out) {
    memset(out, 0, kModBytes);

    // Nazwa modułu.
    memcpy(out, "hydra test", 10);

    /*
     * Pierwszy instrument: 64 bajty próbki, głośność maksymalna.
     * Długość jest w słowach 16-bitowych i zapisana grubym końcem — cały
     * format pochodzi z Amigi, więc wszystkie liczby są big-endian.
     */
    u8* instrument = out + 20;
    const u16 lengthWords = static_cast<u16>(kSampleBytes / 2);
    instrument[22] = static_cast<u8>(lengthWords >> 8);
    instrument[23] = static_cast<u8>(lengthWords & 0xFF);
    instrument[25] = 64;      // głośność
    instrument[27] = 1;       // długość pętli w słowach — zero bywa odrzucane

    out[950] = 1;             // długość ścieżki: jedna pozycja
    out[951] = 127;           // bajt zgodności, wartość bez znaczenia
    out[952] = 0;             // pozycja 0 gra wzorzec 0

    memcpy(out + 1080, "M.K.", 4);   // cztery kanały

    /*
     * Pierwszy wiersz wzorca: instrument 1, okres 428 (nuta C-2).
     * Bajty wiersza: [instr_hi|period_hi] [period_lo] [instr_lo|efekt] [arg]
     */
    u8* pattern = out + kHeaderBytes;
    const u16 period = 428;
    pattern[0] = static_cast<u8>((1 & 0xF0) | ((period >> 8) & 0x0F));
    pattern[1] = static_cast<u8>(period & 0xFF);
    pattern[2] = static_cast<u8>((1 & 0x0F) << 4);
    pattern[3] = 0;

    // Próbka: prostokąt, żeby wyjście nie było ciszą.
    u8* sample = out + kHeaderBytes + kPatternBytes;
    for (u32 i = 0; i < kSampleBytes; ++i) {
        sample[i] = static_cast<u8>((i < kSampleBytes / 2) ? 100 : 0x9C);
    }
}

struct Rig {
    static constexpr u16 kFrames = 64;
    static constexpr u32 kBlockBytes = kFrames * 2 * sizeof(i16);   // stereo S16
    static constexpr u16 kBlocks = 4;

    u8 module[kModBytes] = {};
    u8 storage[kBlockBytes * kBlocks + 8] = {};

    Pipeline  pipeline;
    ModSource mod;
    MeterSink sink;

    Status build(u32 rate = 22050, u8 maxLoops = 0) {
        buildModule(module);

        ModSource::Config cfg;
        cfg.sampleRate = rate;
        cfg.framesPerBlock = kFrames;
        cfg.maxLoops = maxLoops;
        if (auto r = mod.load(CByteSpan{module, sizeof(module)}, cfg); !r) return r;

        pipeline.addPool(ByteSpan{storage, sizeof(storage)}, kBlockBytes, kBlocks);
        pipeline.add(mod);
        pipeline.add(sink);
        pipeline.link(mod, sink);
        return pipeline.prepare();
    }

    void pump(int blocks) {
        for (int i = 0; i < blocks; ++i) {
            mod.process(0);
            sink.process(0);
        }
    }
};

}  // namespace

TEST("mod: moduł się wczytuje i podaje format") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.mod.ready());

    const auto format = rig.mod.negotiate(0, MediaFormat{});
    CHECK(format.has_value());
    CHECK_EQ(static_cast<int>(format.value().kind), static_cast<int>(MediaKind::Audio));
    CHECK_EQ(format.value().sampleRate, 22050u);
    // Stereo zawsze: mieszanie do mono to osobna decyzja i osobny element.
    CHECK_EQ(format.value().channels, 2u);
    CHECK_EQ(static_cast<int>(format.value().sampleFormat), static_cast<int>(SampleFormat::S16));
}

TEST("mod: plik, który nie jest modułem, jest odrzucany") {
    // Nagłówek Protrackera ma stałą sygnaturę, więc to jest sprawdzenie
    // formatu, nie tylko rozmiaru.
    u8 garbage[kModBytes];
    memset(garbage, 0xAB, sizeof(garbage));

    ModSource mod;
    ModSource::Config cfg;
    CHECK(!mod.load(CByteSpan{garbage, sizeof(garbage)}, cfg).has_value());
    CHECK(!mod.ready());
}

TEST("mod: bez modułu prepare odmawia") {
    ModSource mod;
    Pipeline pipeline;
    CHECK(!mod.onPrepare(pipeline).has_value());
}

TEST("mod: źródło produkuje bloki z dźwiękiem") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.pipeline.start().has_value());

    rig.pump(3);

    CHECK(rig.sink.blocks() >= 3u);
    CHECK_EQ(rig.mod.framesProduced(), 3u * Rig::kFrames);
    // Szczyt niezerowy: gdyby konwersja float→i16 gubiła sygnał, bloki
    // przychodziłyby ciche i cała reszta testów przeszłaby mimo to.
    CHECK(rig.sink.takePeak() > 0u);
}

TEST("mod: znacznik czasu rośnie zgodnie z liczbą ramek") {
    Rig rig;
    CHECK(rig.build(22050).has_value());
    CHECK(rig.pipeline.start().has_value());

    rig.pump(1);
    const u64 first = rig.sink.lastPts();
    rig.pump(1);
    const u64 second = rig.sink.lastPts();

    // 64 ramki przy 22050 Hz to ~2902 µs. Liczony z ramek, a nie z zegara —
    // strumień ma być ciągły niezależnie od tego, kiedy task zdążył go wypełnić.
    const u64 delta = second - first;
    CHECK(delta > 2800u && delta < 3000u);
}

TEST("mod: bloki wracają do puli") {
    Rig rig;
    CHECK(rig.build().has_value());
    CHECK(rig.pipeline.start().has_value());

    const u16 free0 = rig.pipeline.pool(0)->available();
    rig.pump(12);

    // Wyciek w źródle objawia się po kilku sekundach jako cisza — a wtedy
    // nikt nie kojarzy jej z pulą.
    CHECK_EQ(rig.pipeline.pool(0)->available(), free0);
}

TEST("mod: granie bez końca nie kończy strumienia") {
    // Zero przejść znaczy „graj bez końca" — dla muzyki w tle to stan
    // normalny, a nie brak decyzji.
    Rig rig;
    CHECK(rig.build(22050, 0).has_value());
    CHECK(rig.pipeline.start().has_value());

    rig.pump(40);
    CHECK(!rig.sink.sawEos());
}

TEST("mod: rozmiar bloku wynika z konfiguracji") {
    Rig rig;
    CHECK(rig.build().has_value());

    const MemReq req = rig.mod.memoryRequest(0);
    // Stereo S16: dwa kanały po dwa bajty na ramkę.
    CHECK_EQ(req.blockSize, static_cast<u32>(Rig::kFrames) * 2 * sizeof(i16));
    CHECK(req.count >= 2u);
}
