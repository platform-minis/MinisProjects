/**
 * Testy elementów SDL (etap 4).
 *
 * Karty dźwiękowej w środowisku budowania nie ma, więc sprawdzamy dwie rzeczy,
 * które i tak są tu najważniejsze:
 *
 *  1. **Arytmetykę opóźnienia.** To ona decyduje, czy pauza reaguje po
 *     stu milisekundach, czy po dziesięciu sekundach — i jest w całości
 *     niezależna od SDL, więc daje się sprawdzić bez niego.
 *  2. **Odmowy.** Element, który zamiast odmówić otwiera okno z szumem albo
 *     gra ciszą, kosztuje godzinę szukania usterki w sprzęcie.
 *
 * Samego przepływu dźwięku nie da się tu sprawdzić i nie udajemy, że się da.
 */

#include "hydra_test.hpp"

#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Sdl.hpp"

using namespace hydra;
using namespace hydra::media;

// ---------------------------------------------------------------------------
// Opóźnienie
// ---------------------------------------------------------------------------

TEST("sdl: zapas w milisekundach przelicza się na bajty formatu") {
    // 100 ms przy 16 kHz mono S16 to 1600 ramek po 2 bajty.
    const MediaFormat mono = MediaFormat::audio(16000, SampleFormat::S16, 1);
    CHECK_EQ(static_cast<int>(bytesForMillis(mono, 100)), 3200);

    // Stereo to dwa razy tyle przy tej samej liczbie ramek.
    const MediaFormat stereo = MediaFormat::audio(16000, SampleFormat::S16, 2);
    CHECK_EQ(static_cast<int>(bytesForMillis(stereo, 100)), 6400);

    CHECK_EQ(static_cast<int>(bytesForMillis(MediaFormat{}, 100)), 0);
}

TEST("sdl: pełna kolejka wstrzymuje dokładanie") {
    // Bez tego progu potok czytający plik wepchnąłby całe nagranie w ułamku
    // sekundy i pauza reagowałaby z opóźnieniem kilkunastu sekund.
    CHECK_EQ(static_cast<int>(queueBudget(3200, 3200, 256)), 0);
    CHECK_EQ(static_cast<int>(queueBudget(4000, 3200, 256)), 0);
}

TEST("sdl: pusta kolejka pozwala dokładać, ale nie bez ograniczeń") {
    // Osiem bloków na krok. Bez górnego limitu pierwsze wywołanie po starcie
    // wepchnęłoby cały zapas naraz, zabierając czas domeny w chwili, gdy
    // reszta potoku dopiero się rozkręca.
    CHECK_EQ(static_cast<int>(queueBudget(0, 3200, 256)), 8);
    CHECK_EQ(static_cast<int>(queueBudget(0, 100000, 256)), 8);
}

TEST("sdl: częściowo zapełniona kolejka dostaje tyle, ile się mieści") {
    // Zostało 800 bajtów miejsca, blok ma 256 → trzy pełne bloki.
    CHECK_EQ(static_cast<int>(queueBudget(2400, 3200, 256)), 3);
    // Zostało mniej niż jeden blok → nic, bo połowy bloku nie wysyłamy.
    CHECK_EQ(static_cast<int>(queueBudget(3100, 3200, 256)), 0);
}

TEST("sdl: zerowy rozmiar bloku nie dzieli przez zero") {
    CHECK_EQ(static_cast<int>(queueBudget(0, 3200, 0)), 0);
}

// ---------------------------------------------------------------------------
// Odmowy
// ---------------------------------------------------------------------------

TEST("sdl: wyjście audio odrzuca format bez odpowiednika w SDL") {
    // S24 nie ma odpowiednika, a U8 jest bez znaku i wymagałby przesunięcia
    // punktu zerowego. Przeliczanie próbek to zadanie filtru, nie ujścia.
    u8 storage[256 * 4];
    Pipeline     pipeline;
    ToneSource   tone;
    SdlAudioSink sink;

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    tone.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 4);
    pipeline.add(tone);
    pipeline.add(sink);
    pipeline.link(tone, sink);

    // S16 przechodzi negocjację.
    REQUIRE(pipeline.prepare().has_value());

    // A U8 nie — sprawdzamy na samym elemencie, bo generator go nie wystawi.
    Pipeline     other;
    SdlAudioSink u8sink;
    other.add(u8sink);
    u8sink.input(0).configure(MediaFormat::audio(16000, SampleFormat::U8, 1));
    CHECK(!u8sink.onPrepare(other).has_value());
}

TEST("sdl: podgląd odmawia dla JPEG zamiast pokazywać szum") {
    // Okno z szumem wygląda na usterkę kamery i kosztuje godzinę szukania
    // w niewłaściwym miejscu. Dekoder to etap 5.
    u8 window[320 * 240 * 2];
    Pipeline     pipeline;
    SdlVideoSink video;

    video.configure(ByteSpan{window, sizeof(window)}, {});
    pipeline.add(video);
    video.input(0).configure(MediaFormat::video(FrameFormat::Jpeg, 320, 240));

    CHECK(!video.onPrepare(pipeline).has_value());
}

TEST("sdl: podgląd odmawia dla YUV — konwersja barw to osobny element") {
    u8 window[320 * 240 * 2];
    Pipeline     pipeline;
    SdlVideoSink video;

    video.configure(ByteSpan{window, sizeof(window)}, {});
    pipeline.add(video);
    video.input(0).configure(MediaFormat::video(FrameFormat::Yuv422, 320, 240));

    CHECK(!video.onPrepare(pipeline).has_value());
}

TEST("sdl: za mały bufor okna wychodzi przy prepare, nie przy pierwszej klatce") {
    u8 tooSmall[100];
    Pipeline     pipeline;
    SdlVideoSink video;

    video.configure(ByteSpan{tooSmall, sizeof(tooSmall)}, {});
    pipeline.add(video);
    video.input(0).configure(MediaFormat::video(FrameFormat::Rgb565, 320, 240));

    CHECK(!video.onPrepare(pipeline).has_value());
}

TEST("sdl: bufor o właściwym rozmiarze przechodzi negocjację") {
    u8 window[320 * 240 * 2];
    Pipeline     pipeline;
    SdlVideoSink video;

    video.configure(ByteSpan{window, sizeof(window)}, {});
    pipeline.add(video);
    video.input(0).configure(MediaFormat::video(FrameFormat::Rgb565, 320, 240));

    CHECK(video.onPrepare(pipeline).has_value());
}

// ---------------------------------------------------------------------------
// Build bez SDL
// ---------------------------------------------------------------------------

TEST("sdl: build bez SDL zgłasza brak, a nie gra ciszą") {
    // To jest normalna ścieżka dla CI i sesji ssh, nie atrapa testowa.
    // Milczące ujście byłoby nierozróżnialne od ciszy w materiale.
    u8 storage[256 * 4];
    Pipeline     pipeline;
    ToneSource   tone;
    SdlAudioSink sink;

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    tone.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 4);
    pipeline.add(tone);
    pipeline.add(sink);
    pipeline.link(tone, sink);
    REQUIRE(pipeline.prepare().has_value());

#if defined(HYDRA_WITH_SDL)
    // Z SDL start może się udać albo nie, zależnie od maszyny — nie ma tu
    // czego sprawdzać w sposób powtarzalny.
#else
    CHECK(!pipeline.start().has_value());
    // Nieudany start zatrzymuje to, co już ruszyło — potok nie zostaje
    // w połowie uruchomiony.
    CHECK_EQ(static_cast<int>(pipeline.state()), static_cast<int>(PipelineState::Ready));
#endif
}

TEST("sdl: wejście audio bez SDL nie startuje, ale negocjuje format") {
    // Negocjacja musi działać bez sprzętu, inaczej `prepare()` całego potoku
    // zależałby od tego, czy maszyna ma mikrofon.
    SdlAudioSource source;
    SdlAudioSource::Config cfg;
    cfg.format = MediaFormat::audio(8000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 128;
    REQUIRE(source.configure(cfg).has_value());

    auto format = source.negotiate(0, MediaFormat{});
    REQUIRE(format.has_value());
    CHECK_EQ(static_cast<int>(format->sampleRate), 8000);

    const MemReq req = source.memoryRequest(0);
    CHECK_EQ(static_cast<int>(req.blockSize), 256);
    CHECK(req.count >= 2);
}
