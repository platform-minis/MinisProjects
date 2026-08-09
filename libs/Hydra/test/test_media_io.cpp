/**
 * Testy plikowych i sieciowych elementów potoku (etap 3).
 *
 * Najciekawsze są tu przypadki, w których plik albo strumień jest **nie taki,
 * jak go zapisaliśmy**: nagłówek WAV z dodatkowym fragmentem w środku,
 * nagranie ucięte zanikiem zasilania, odbiornik wpięty w połowie bloku.
 * Wszystkie trzy zdarzają się w praktyce i wszystkie trzy dawały wcześniej
 * objaw postaci „nie działa" bez żadnego tropu.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/hal/HostFileSystem.hpp"
#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Files.hpp"
#include "hydra/media/elements/Net.hpp"

using namespace hydra;
using namespace hydra::media;

namespace {

const char* kDir = "build/media-io";

hal::HostFileSystem& freshFs() {
    static hal::HostFileSystem fs{kDir};
    fs.mount();
    return fs;
}

/** Gniazdo-atrapa: pętla zwrotna z limitem pojemności. */
class LoopSocket : public net::IClient {
public:
    Status connect(const char*, u16, u32) override { open_ = true; return ok(); }
    void   stop() override { open_ = false; }
    bool   connected() const override { return open_; }

    size_t write(CByteSpan data) override {
        const size_t room = capacity_ - len_;
        const size_t take = data.size() < room ? data.size() : room;
        memcpy(buf_ + len_, data.data(), take);
        len_ += take;
        return take;
    }
    size_t read(ByteSpan out) override {
        const size_t ready = len_ - read_;
        const size_t take = ready < out.size() ? ready : out.size();
        memcpy(out.data(), buf_ + read_, take);
        read_ += take;
        return take;
    }
    size_t available() override { return len_ - read_; }

    /** Wstawia surowe bajty tak, jakby przyszły z sieci. */
    void inject(const void* data, size_t n) {
        memcpy(buf_ + len_, data, n);
        len_ += n;
    }
    void setCapacity(size_t bytes) { capacity_ = bytes; }
    size_t buffered() const { return len_; }

private:
    u8     buf_[16384] = {};
    size_t len_ = 0;
    size_t read_ = 0;
    size_t capacity_ = sizeof(buf_);
    bool   open_ = false;
};

}  // namespace

// ---------------------------------------------------------------------------
// Nagłówek WAV
// ---------------------------------------------------------------------------

TEST("io: nagłówek WAV wraca z tymi samymi parametrami") {
    u8 header[64];
    const MediaFormat format = MediaFormat::audio(44100, SampleFormat::S16, 2);
    const size_t n = buildWavHeader(ByteSpan{header, sizeof(header)}, format, 8000);
    CHECK_EQ(static_cast<int>(n), 44);

    auto info = parseWavHeader(CByteSpan{header, n});
    REQUIRE(info.has_value());
    CHECK_EQ(static_cast<int>(info->format.sampleRate), 44100);
    CHECK_EQ(static_cast<int>(info->format.channels), 2);
    CHECK_EQ(static_cast<int>(info->dataOffset), 44);
    CHECK_EQ(static_cast<int>(info->dataBytes), 8000);
}

TEST("io: fragment LIST między fmt a data nie psuje odczytu") {
    // Czytnik zakładający stałe przesunięcie 44 bajtów działa wyłącznie
    // z plikami, które sam wyprodukował. Edytory wstawiają tu LIST z nazwą
    // programu, a rekordery fact.
    u8 header[80] = {};
    memcpy(header, "RIFF", 4);
    header[4] = 72;
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    header[16] = 16;
    header[20] = 1;                       // PCM
    header[22] = 1;                       // mono
    header[24] = 0x80; header[25] = 0x3E; // 16000 Hz
    header[34] = 16;                      // bity
    memcpy(header + 36, "LIST", 4);
    header[40] = 8;                       // długość fragmentu obcego
    memcpy(header + 44, "INFOxxxx", 8);
    memcpy(header + 52, "data", 4);
    header[56] = 100;

    auto info = parseWavHeader(CByteSpan{header, sizeof(header)});
    REQUIRE(info.has_value());
    CHECK_EQ(static_cast<int>(info->format.sampleRate), 16000);
    CHECK_EQ(static_cast<int>(info->dataOffset), 60);
    CHECK_EQ(static_cast<int>(info->dataBytes), 100);
}

TEST("io: WAV skompresowany jest odrzucany, a nie odtwarzany jako szum") {
    // ADPCM ma ten sam nagłówek i inny ładunek; potraktowany jak PCM daje
    // szum o pełnej głośności — najgorszy możliwy komunikat o błędzie.
    u8 header[64];
    buildWavHeader(ByteSpan{header, sizeof(header)},
                   MediaFormat::audio(16000, SampleFormat::S16, 1), 100);
    header[20] = 2;   // WAVE_FORMAT_ADPCM

    CHECK(!parseWavHeader(CByteSpan{header, 44}).has_value());
}

TEST("io: nie-WAV jest odrzucany") {
    const char* junk = "To nie jest plik dzwiekowy, tylko notatka.";
    CHECK(!parseWavHeader(CByteSpan{reinterpret_cast<const u8*>(junk), strlen(junk)})
               .has_value());
}

// ---------------------------------------------------------------------------
// Zapis i odczyt pliku
// ---------------------------------------------------------------------------

TEST("io: nagranie zapisuje się i odczytuje z tym samym formatem") {
    hal::HostFileSystem& fs = freshFs();
    fs.remove("ton.wav");

    // --- zapis ---
    {
        u8 storage[128 * 6];
        Pipeline   pipeline;
        ToneSource tone;
        FileSink   sink{fs};

        ToneSource::Config toneCfg;
        toneCfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
        toneCfg.framesPerBlock = 64;
        toneCfg.amplitude = 12000;
        tone.configure(toneCfg);

        FileSink::Config sinkCfg;
        sinkCfg.path = "ton.wav";
        REQUIRE(sink.configure(sinkCfg).has_value());

        pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6);
        pipeline.add(tone);
        pipeline.add(sink);
        pipeline.link(tone, sink);
        REQUIRE(pipeline.start().has_value());

        for (int i = 0; i < 20; ++i) pipeline.stepAll(0);
        pipeline.stop();

        CHECK(sink.bytesWritten() >= 15 * 128);
        CHECK_EQ(static_cast<int>(sink.writeErrors()), 0);
    }

    // --- odczyt ---
    {
        u8 storage[128 * 6];
        Pipeline   pipeline;
        FileSource source{fs};
        MeterSink  meter;

        FileSource::Config srcCfg;
        srcCfg.path = "ton.wav";
        srcCfg.framesPerBlock = 64;
        REQUIRE(source.configure(srcCfg).has_value());

        pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6);
        pipeline.add(source);
        pipeline.add(meter);
        pipeline.link(source, meter);
        REQUIRE(pipeline.prepare().has_value());

        // Format pochodzi z nagłówka pliku, nie z konfiguracji — plik 44,1 kHz
        // odtworzony jako 16 kHz brzmiałby jak nagranie zwolnione.
        CHECK_EQ(static_cast<int>(source.info().format.sampleRate), 16000);
        CHECK_EQ(static_cast<int>(source.info().format.channels), 1);

        REQUIRE(pipeline.start().has_value());
        for (int i = 0; i < 40; ++i) pipeline.stepAll(0);

        CHECK(meter.blocks() > 10);
        const u16 peak = meter.takePeak();
        CHECK(peak >= 11990 && peak <= 12000);   // amplituda przeżyła obieg
        CHECK(source.finished());
        CHECK(meter.sawEos());
    }
}

TEST("io: nagłówek jest uzupełniany w trakcie, nie tylko przy zamknięciu") {
    // Nagranie przerwane zanikiem zasilania ma wtedy poprawny nagłówek
    // z dokładnością do ogona, zamiast zer, przy których odtwarzacz mówi
    // „plik uszkodzony".
    hal::HostFileSystem& fs = freshFs();
    fs.remove("ucięte.wav");

    u8 storage[128 * 6];
    Pipeline   pipeline;
    ToneSource tone;
    FileSink   sink{fs};

    ToneSource::Config toneCfg;
    toneCfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    toneCfg.framesPerBlock = 64;
    tone.configure(toneCfg);

    FileSink::Config sinkCfg;
    sinkCfg.path = "ucięte.wav";
    sinkCfg.patchEvery = 2;              // uzupełniaj często
    sink.configure(sinkCfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6);
    pipeline.add(tone);
    pipeline.add(sink);
    pipeline.link(tone, sink);
    REQUIRE(pipeline.start().has_value());

    for (int i = 0; i < 10; ++i) pipeline.stepAll(0);
    // Bez `stop()` — udajemy zanik zasilania. Plik zostaje otwarty, ale
    // nagłówek był uzupełniany po drodze.

    auto file = fs.open("ucięte.wav", hal::OpenMode::Read);
    REQUIRE(file.has_value());
    u8 header[44];
    (*file)->read(ByteSpan{header, sizeof(header)});
    (*file)->close();

    auto info = parseWavHeader(CByteSpan{header, sizeof(header)});
    REQUIRE(info.has_value());
    CHECK(info->dataBytes > 0);
}

TEST("io: plik surowy wymaga podania formatu") {
    hal::HostFileSystem& fs = freshFs();
    fs.remove("surowe.pcm");

    {
        auto file = fs.open("surowe.pcm", hal::OpenMode::Write);
        REQUIRE(file.has_value());
        i16 samples[128];
        for (size_t i = 0; i < 128; ++i) samples[i] = static_cast<i16>(i * 100);
        (*file)->write(CByteSpan{reinterpret_cast<const u8*>(samples), sizeof(samples)});
        (*file)->close();
    }

    FileSource withoutFormat{fs};
    FileSource::Config bad;
    bad.path = "surowe.pcm";
    withoutFormat.configure(bad);
    CHECK(!withoutFormat.negotiate(0, MediaFormat{}).has_value());

    FileSource withFormat{fs};
    FileSource::Config good;
    good.path = "surowe.pcm";
    good.rawFormat = MediaFormat::audio(8000, SampleFormat::S16, 1);
    withFormat.configure(good);
    auto format = withFormat.negotiate(0, MediaFormat{});
    REQUIRE(format.has_value());
    CHECK_EQ(static_cast<int>(format->sampleRate), 8000);
}

// ---------------------------------------------------------------------------
// Sieć
// ---------------------------------------------------------------------------

TEST("io: nagłówek sieciowy wraca z długością, czasem i flagami") {
    u8 header[kNetHeaderSize];
    CHECK_EQ(static_cast<int>(buildNetHeader(ByteSpan{header, sizeof(header)},
                                             1234, 987654321ull, kBlockEos)),
             static_cast<int>(kNetHeaderSize));

    u32 length = 0;
    u64 pts = 0;
    u8  flags = 0;
    CHECK(parseNetHeader(CByteSpan{header, sizeof(header)}, length, pts, flags));
    CHECK_EQ(static_cast<int>(length), 1234);
    CHECK(pts == 987654321ull);
    CHECK_EQ(static_cast<int>(flags), static_cast<int>(kBlockEos));
}

TEST("io: bloki przechodzą przez gniazdo i wracają z tym samym czasem") {
    LoopSocket socket;
    socket.connect("host", 1234, 0);

    u8 storageOut[128 * 6];
    u8 storageIn[128 * 6];

    Pipeline   outPipe;
    ToneSource tone;
    NetSink    netOut{socket};

    ToneSource::Config toneCfg;
    toneCfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    toneCfg.framesPerBlock = 64;
    toneCfg.amplitude = 9000;
    tone.configure(toneCfg);

    outPipe.addPool(ByteSpan{storageOut, sizeof(storageOut)}, 128, 6);
    outPipe.add(tone);
    outPipe.add(netOut);
    outPipe.link(tone, netOut);
    REQUIRE(outPipe.start().has_value());
    for (int i = 0; i < 10; ++i) outPipe.stepAll(0);

    CHECK(netOut.bytesSent() > 0);
    CHECK_EQ(static_cast<int>(netOut.dropped()), 0);

    Pipeline  inPipe;
    NetSource netIn{socket};
    MeterSink meter;

    NetSource::Config srcCfg;
    srcCfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    srcCfg.framesPerBlock = 64;
    srcCfg.blocksPerStep = 4;
    REQUIRE(netIn.configure(srcCfg).has_value());

    inPipe.addPool(ByteSpan{storageIn, sizeof(storageIn)}, 128, 6);
    inPipe.add(netIn);
    inPipe.add(meter);
    inPipe.link(netIn, meter);
    REQUIRE(inPipe.start().has_value());
    for (int i = 0; i < 10; ++i) inPipe.stepAll(0);

    CHECK(meter.blocks() >= 5);
    const u16 peak = meter.takePeak();
    CHECK(peak >= 8990 && peak <= 9000);
    // Czas przeszedł przez sieć, a nie został wyliczony od nowa.
    CHECK(meter.lastPts() > 0);
    CHECK_EQ(static_cast<int>(meter.lastPts() % 4000), 0);
}

TEST("io: odbiornik wpięty w połowie strumienia synchronizuje się po magii") {
    // Bez magii czytałby długość ze środka próbek i czekał na dwa gigabajty.
    LoopSocket socket;
    socket.connect("host", 1234, 0);

    // Śmieci, a po nich poprawny blok.
    const u8 junk[37] = {0xAB, 0xCD, 'H', 0x11, 0x00, 0xFF};
    socket.inject(junk, sizeof(junk));

    u8 header[kNetHeaderSize];
    i16 samples[64];
    for (size_t i = 0; i < 64; ++i) samples[i] = 5000;
    buildNetHeader(ByteSpan{header, sizeof(header)}, sizeof(samples), 12345, 0);
    socket.inject(header, sizeof(header));
    socket.inject(samples, sizeof(samples));

    u8 storage[128 * 6];
    Pipeline  pipeline;
    NetSource netIn{socket};
    MeterSink meter;

    NetSource::Config cfg;
    cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    cfg.blocksPerStep = 4;
    netIn.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6);
    pipeline.add(netIn);
    pipeline.add(meter);
    pipeline.link(netIn, meter);
    REQUIRE(pipeline.start().has_value());
    for (int i = 0; i < 5; ++i) pipeline.stepAll(0);

    CHECK_EQ(static_cast<int>(meter.blocks()), 1);
    CHECK(netIn.resyncs() > 0);
    CHECK_EQ(static_cast<int>(meter.lastPts()), 12345);
}

TEST("io: zapchane gniazdo gubi cały blok, nie połowę") {
    // Połowa bloku po drugiej stronie rozsypuje ramkowanie na resztę
    // połączenia; brakujący cały to jedna przerwa.
    LoopSocket socket;
    socket.connect("host", 1234, 0);
    socket.setCapacity(kNetHeaderSize + 20);   // starczy na nagłówek i kawałek

    u8 storage[128 * 6];
    Pipeline   pipeline;
    ToneSource tone;
    NetSink    netOut{socket};

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    tone.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6);
    pipeline.add(tone);
    pipeline.add(netOut);
    pipeline.link(tone, netOut);
    REQUIRE(pipeline.start().has_value());

    for (int i = 0; i < 5; ++i) pipeline.stepAll(0);

    CHECK(netOut.dropped() > 0);
    // Bloki mimo to wróciły do puli — porzucenie nie może być wyciekiem.
    pipeline.stop();
    CHECK_EQ(static_cast<int>(pipeline.pool(0)->available()),
             static_cast<int>(pipeline.pool(0)->capacityBlocks()));
}

TEST("io: brak połączenia nie zatrzymuje potoku") {
    // Dźwięk ma dalej grać lokalnie; strumień wznowi się, gdy gniazdo wróci.
    LoopSocket socket;   // nie połączone

    u8 storage[128 * 6];
    Pipeline   pipeline;
    ToneSource tone;
    NetSink    netOut{socket};

    ToneSource::Config cfg;
    cfg.format = MediaFormat::audio(16000, SampleFormat::S16, 1);
    cfg.framesPerBlock = 64;
    tone.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6);
    pipeline.add(tone);
    pipeline.add(netOut);
    pipeline.link(tone, netOut);
    REQUIRE(pipeline.start().has_value());

    for (int i = 0; i < 10; ++i) pipeline.stepAll(0);

    CHECK(netOut.dropped() > 0);
    CHECK_EQ(static_cast<int>(netOut.bytesSent()), 0);
    CHECK_EQ(static_cast<int>(pipeline.pool(0)->available()),
             static_cast<int>(pipeline.pool(0)->capacityBlocks()));
}
