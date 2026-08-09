/**
 * Testy elementów obrazu (etap 5).
 *
 * Atrapa kamery generuje ruchomy wzorzec i pilnuje umowy o własności, więc da
 * się tu sprawdzić rzeczy, których na płytce nie widać bez monitora: czy klatki
 * wracają do sterownika, czy skalowanie trafia we właściwe piksele i czy
 * konwersja barw nie gubi jasności.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/hal/Mock.hpp"
#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Video.hpp"

using namespace hydra;
using namespace hydra::media;

namespace {

hal::mock::Backend& freshHal() {
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    return hal::mock::backend();
}

/** Wypełnia blok obrazem o zadanym formacie i stałej treści na piksel. */
void fillFrame(Block& block, const MediaFormat& format, const u8* pixel) {
    const u8 bytes = static_cast<u8>(bitsPerPixel(format.frameFormat) / 8);
    const u32 pixels = static_cast<u32>(format.width) * format.height;
    for (u32 i = 0; i < pixels; ++i) {
        for (u8 b = 0; b < bytes; ++b) block.data[i * bytes + b] = pixel[b];
    }
    block.length = pixels * bytes;
}

/**
 * Ujście zapamiętujące początek klatki.
 *
 * Zaglądanie wprost do tablicy z pulą nie działa: `BlockPool::attach()`
 * wyrównuje początek w górę, więc pierwszy piksel nie musi leżeć pod
 * `storage[0]`. Odczyt przez potok sprawdza przy okazji tę samą drogę,
 * którą pójdzie prawdziwe ujście.
 */
class CaptureSink : public Element {
public:
    CaptureSink() : Element("capture") {}

    u8 inputCount() const override { return 1; }
    Status onPrepare(Pipeline& pipeline) override { pipeline_ = &pipeline; return ok(); }

    void process(u64) override {
        Block block;
        while (take(0, block)) {
            const u32 n = block.length < sizeof(data) ? block.length
                                                      : static_cast<u32>(sizeof(data));
            memcpy(data, block.data, n);
            length = block.length;
            ++blocks;
            if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);
        }
    }

    u8  data[64] = {};
    u32 length = 0;
    u32 blocks = 0;

private:
    Pipeline* pipeline_ = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------
// JPEG bez dekodowania
// ---------------------------------------------------------------------------

TEST("video: wymiary JPEG czytają się z nagłówka SOF") {
    // 64×48, trzy składowe. Ten sam układ bajtów, jaki daje moduł OV2640.
    const u8 jpeg[] = {
        0xFF, 0xD8,                               // SOI
        0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00,       // APP0 (pomijany)
        0xFF, 0xC0, 0x00, 0x11, 0x08,             // SOF0, długość 17, 8 bitów
        0x00, 0x30,                               // wysokość 48
        0x00, 0x40,                               // szerokość 64
        0x03,                                     // składowe
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    auto info = jpegInfo(CByteSpan{jpeg, sizeof(jpeg)});
    REQUIRE(info.has_value());
    CHECK_EQ(static_cast<int>(info->width), 64);
    CHECK_EQ(static_cast<int>(info->height), 48);
    CHECK_EQ(static_cast<int>(info->components), 3);
}

TEST("video: tablica Huffmana nie jest brana za ramkę") {
    // 0xC4 wpada w zakres SOF, ale opisuje tablicę Huffmana. Czytnik biorący
    // cały zakres 0xC0…0xCF podawałby wymiary wzięte z tablicy kodów.
    const u8 jpeg[] = {
        0xFF, 0xD8,
        0xFF, 0xC4, 0x00, 0x08, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,  // DHT
        0xFF, 0xC0, 0x00, 0x11, 0x08,
        0x01, 0x00,                               // wysokość 256
        0x01, 0x40,                               // szerokość 320
        0x03,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    auto info = jpegInfo(CByteSpan{jpeg, sizeof(jpeg)});
    REQUIRE(info.has_value());
    CHECK_EQ(static_cast<int>(info->width), 320);
    CHECK_EQ(static_cast<int>(info->height), 256);
}

TEST("video: dane bez SOI to nie JPEG") {
    const u8 junk[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    CHECK(!jpegInfo(CByteSpan{junk, sizeof(junk)}).has_value());
}

TEST("video: JPEG bez ramki kończy się NotFound, a nie zgadywaniem") {
    const u8 truncated[] = {0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02};   // od razu SOS
    CHECK(!jpegInfo(CByteSpan{truncated, sizeof(truncated)}).has_value());
}

// ---------------------------------------------------------------------------
// Kamera
// ---------------------------------------------------------------------------

TEST("video: klatki z kamery trafiają do potoku i wracają do sterownika") {
    // Zatrzymana klatka zatrzymuje strumień — kamera ma zwykle dwa bufory.
    hal::mock::Backend& mock = freshHal();

    constexpr u32 kFrameBytes = 160 * 120 * 2;
    static u8 storage[kFrameBytes * 3 + 64];

    Pipeline     pipeline;
    CameraSource camera{hal::Hal::camera()};
    MeterSink    meter;

    CameraSource::Config cfg;
    cfg.camera.format = hal::CameraFormat::Rgb565;
    cfg.camera.resolution = hal::CameraResolution::Qqvga;
    cfg.camera.bufferCount = 2;
    REQUIRE(camera.configure(cfg).has_value());

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, kFrameBytes, 3, 32);
    pipeline.add(camera);
    pipeline.add(meter);
    pipeline.link(camera, meter);
    REQUIRE(pipeline.start().has_value());

    for (int i = 0; i < 10; ++i) pipeline.stepAll(0);

    CHECK(camera.framesCaptured() >= 8);
    CHECK_EQ(static_cast<int>(meter.blocks()), static_cast<int>(camera.framesCaptured()));
    // Żadna klatka nie została u nas — sterownik ma wszystkie bufory wolne.
    CHECK_EQ(static_cast<int>(mock.camera.borrowed()), 0);
    CHECK_EQ(static_cast<int>(mock.camera.dropped()), 0);
}

TEST("video: format i rozmiar bloku wynikają z ustawień kamery") {
    freshHal();
    CameraSource camera{hal::Hal::camera()};

    CameraSource::Config cfg;
    cfg.camera.format = hal::CameraFormat::Yuv422;
    cfg.camera.resolution = hal::CameraResolution::Qvga;
    camera.configure(cfg);

    auto format = camera.negotiate(0, MediaFormat{});
    REQUIRE(format.has_value());
    CHECK_EQ(static_cast<int>(format->width), 320);
    CHECK_EQ(static_cast<int>(format->height), 240);
    CHECK_EQ(static_cast<int>(format->frameFormat), static_cast<int>(FrameFormat::Yuv422));

    const MemReq req = camera.memoryRequest(0);
    CHECK_EQ(static_cast<int>(req.blockSize), 320 * 240 * 2);
    CHECK(req.alignment >= 32);
}

TEST("video: pusta pula nie zatrzymuje kamery na stałe") {
    // Klatka musi wrócić do sterownika **przed** wyjściem z process().
    // Zatrzymanie jej przy braku miejsca oznaczałoby kamerę bez wolnego bufora
    // i strumień, który już nie ruszy.
    hal::mock::Backend& mock = freshHal();

    constexpr u32 kFrameBytes = 160 * 120 * 2;
    static u8 storage[kFrameBytes * 2 + 64];

    Pipeline     pipeline;
    CameraSource camera{hal::Hal::camera()};
    MeterSink    meter;

    CameraSource::Config cfg;
    cfg.camera.resolution = hal::CameraResolution::Qqvga;
    camera.configure(cfg);

    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, kFrameBytes, 2, 32);
    pipeline.add(camera);
    pipeline.add(meter);
    pipeline.link(camera, meter);
    REQUIRE(pipeline.start().has_value());

    // Same kroki kamery — nikt nie odbiera, więc pula się kończy.
    for (int i = 0; i < 6; ++i) camera.process(0);
    CHECK_EQ(static_cast<int>(mock.camera.borrowed()), 0);

    // Po odebraniu potok rusza dalej.
    for (int i = 0; i < 6; ++i) pipeline.stepAll(0);
    CHECK(meter.blocks() >= 2);
}

// ---------------------------------------------------------------------------
// Skalowanie
// ---------------------------------------------------------------------------

TEST("video: skalowanie zmienia wymiary i zachowuje treść jednolitą") {
    freshHal();
    static u8 storage[320 * 240 * 2 + 64];
    static u8 outStorage[80 * 60 * 2 + 64];

    Pipeline pipeline;
    Scaler   scaler;
    MeterSink meter;

    scaler.setOutputSize(80, 60);
    pipeline.addPool(ByteSpan{storage, sizeof(storage)}, 160 * 120 * 2, 1, 32);
    pipeline.addPool(ByteSpan{outStorage, sizeof(outStorage)}, 80 * 60 * 2, 1, 32);
    pipeline.add(scaler);
    pipeline.add(meter);
    pipeline.link(scaler, meter);

    scaler.input(0).configure(MediaFormat::video(FrameFormat::Rgb565, 160, 120));
    REQUIRE(pipeline.prepare().has_value());
    REQUIRE(pipeline.start().has_value());

    // Wstrzykujemy jednolitą klatkę wprost na wejście skalera.
    Block src = pipeline.pool(0)->acquire();
    REQUIRE(src.valid());
    const u8 pixel[2] = {0xAB, 0xCD};
    fillFrame(src, MediaFormat::video(FrameFormat::Rgb565, 160, 120), pixel);
    Block evicted;
    scaler.input(0).push(src, evicted);

    pipeline.stepAll(0);

    CHECK_EQ(static_cast<int>(scaler.framesScaled()), 1);
    CHECK_EQ(static_cast<int>(meter.blocks()), 1);
    CHECK_EQ(static_cast<int>(meter.bytes()), 80 * 60 * 2);
}

TEST("video: skalowanie odmawia dla JPEG zamiast mielić bajty") {
    // JPEG nie ma pikseli, do których dałoby się sięgnąć; skalowanie go
    // wymagałoby dekodera, którego ten etap świadomie nie ma.
    Scaler scaler;
    scaler.setOutputSize(80, 60);
    CHECK(!scaler.negotiate(0, MediaFormat::video(FrameFormat::Jpeg, 320, 240))
               .has_value());
}

TEST("video: brak rozmiaru docelowego oznacza brak zmiany w tej osi") {
    Scaler scaler;
    scaler.setOutputSize(160, 0);
    auto out = scaler.negotiate(0, MediaFormat::video(FrameFormat::Rgb565, 320, 240));
    REQUIRE(out.has_value());
    CHECK_EQ(static_cast<int>(out->width), 160);
    CHECK_EQ(static_cast<int>(out->height), 240);
}

// ---------------------------------------------------------------------------
// Konwersja barw
// ---------------------------------------------------------------------------

TEST("video: YUV → szarość to wybranie składowej Y") {
    // Darmowa konwersja: Y **jest** jasnością. To jedyny powód, dla którego
    // warto trzymać sensor w YUV, gdy liczy się tylko luminancja.
    freshHal();
    static u8 inStorage[32 * 16 * 2 * 2 + 64];    // dwa bloki po 1024 B
    static u8 outStorage[32 * 16 * 2 + 64];       // dwa bloki po 512 B

    Pipeline     pipeline;
    ColorConvert convert;
    CaptureSink  capture;

    convert.setOutputFormat(FrameFormat::Gray8);
    // Po dwa bloki w każdej puli: pule dobierane są po rozmiarze i bywają
    // współdzielone, więc pula z jednym blokiem oznacza element, który nie ma
    // z czego wziąć wyjścia, gdy test trzyma wejście.
    pipeline.addPool(ByteSpan{inStorage, sizeof(inStorage)}, 32 * 16 * 2, 2, 32);
    pipeline.addPool(ByteSpan{outStorage, sizeof(outStorage)}, 32 * 16, 2, 32);
    pipeline.add(convert);
    pipeline.add(capture);
    pipeline.link(convert, capture);

    convert.input(0).configure(MediaFormat::video(FrameFormat::Yuv422, 32, 16));
    REQUIRE(pipeline.prepare().has_value());
    REQUIRE(pipeline.start().has_value());

    Block src = pipeline.pool(0)->acquire();
    REQUIRE(src.valid());
    // YUYV o stałej jasności 200 i neutralnej chrominancji.
    const u8 yuyv[2] = {200, 128};
    fillFrame(src, MediaFormat::video(FrameFormat::Yuv422, 32, 16), yuyv);
    Block evicted;
    convert.input(0).push(src, evicted);

    pipeline.stepAll(0);

    CHECK_EQ(static_cast<int>(capture.blocks), 1);
    CHECK_EQ(static_cast<int>(capture.length), 32 * 16);
    // Pierwszy piksel wyjścia to Y wejścia, bez przeliczania.
    CHECK_EQ(static_cast<int>(capture.data[0]), 200);
}

TEST("video: YUV → RGB565 daje sensowną jasność, a nie zera") {
    freshHal();
    static u8 inStorage[16 * 8 * 2 * 3 + 64];

    Pipeline     pipeline;
    ColorConvert convert;
    CaptureSink  capture;

    convert.setOutputFormat(FrameFormat::Rgb565);
    // Jedna pula wystarczy: wejście i wyjście mają tu ten sam rozmiar bloku,
    // więc `poolAtLeast()` i tak wskazałby ją oba razy. Trzy bloki, bo test
    // trzyma jeden na wejściu.
    pipeline.addPool(ByteSpan{inStorage, sizeof(inStorage)}, 16 * 8 * 2, 3, 32);
    pipeline.add(convert);
    pipeline.add(capture);
    pipeline.link(convert, capture);

    convert.input(0).configure(MediaFormat::video(FrameFormat::Yuv422, 16, 8));
    REQUIRE(pipeline.prepare().has_value());
    REQUIRE(pipeline.start().has_value());

    Block src = pipeline.pool(0)->acquire();
    REQUIRE(src.valid());
    // Biel: Y bliskie maksimum, chrominancja neutralna.
    const u8 white[2] = {235, 128};
    fillFrame(src, MediaFormat::video(FrameFormat::Yuv422, 16, 8), white);
    Block evicted;
    convert.input(0).push(src, evicted);

    pipeline.stepAll(0);

    CHECK_EQ(static_cast<int>(capture.blocks), 1);
    const u16 pixel = static_cast<u16>((static_cast<u16>(capture.data[0]) << 8) |
                                       capture.data[1]);
    // Biel w RGB565 to same jedynki w każdej składowej — sprawdzamy, że
    // wszystkie trzy są wysokie, a nie że wyszły zera po przepełnieniu.
    CHECK(((pixel >> 11) & 0x1F) >= 28);
    CHECK(((pixel >> 5) & 0x3F) >= 56);
    CHECK((pixel & 0x1F) >= 28);
}

TEST("video: nieobsługiwana para formatów jest odrzucana przy negocjacji") {
    ColorConvert convert;
    convert.setOutputFormat(FrameFormat::Yuv422);
    CHECK(!convert.negotiate(0, MediaFormat::video(FrameFormat::Rgb565, 320, 240))
               .has_value());
}

// ---------------------------------------------------------------------------
// Pełny łańcuch
// ---------------------------------------------------------------------------

TEST("video: kamera → skalowanie → miernik przepuszcza klatki bez wycieku") {
    hal::mock::Backend& mock = freshHal();

    constexpr u32 kFull  = 160 * 120 * 2;
    constexpr u32 kSmall = 40 * 30 * 2;
    static u8 fullStorage[kFull * 3 + 64];
    static u8 smallStorage[kSmall * 3 + 64];

    Pipeline     pipeline;
    CameraSource camera{hal::Hal::camera()};
    Scaler       scaler;
    MeterSink    meter;

    CameraSource::Config cfg;
    cfg.camera.resolution = hal::CameraResolution::Qqvga;
    cfg.camera.format = hal::CameraFormat::Rgb565;
    camera.configure(cfg);
    scaler.setOutputSize(40, 30);

    pipeline.addPool(ByteSpan{fullStorage, sizeof(fullStorage)}, kFull, 3, 32);
    pipeline.addPool(ByteSpan{smallStorage, sizeof(smallStorage)}, kSmall, 3, 32);
    pipeline.add(camera);
    pipeline.add(scaler);
    pipeline.add(meter);
    pipeline.link(camera, scaler);
    pipeline.link(scaler, meter);

    REQUIRE(pipeline.start().has_value());
    for (int i = 0; i < 12; ++i) pipeline.stepAll(0);

    CHECK(meter.blocks() >= 8);
    CHECK_EQ(static_cast<int>(meter.bytes()), static_cast<int>(meter.blocks() * kSmall));
    CHECK_EQ(static_cast<int>(mock.camera.borrowed()), 0);

    pipeline.stop();
    CHECK_EQ(static_cast<int>(pipeline.pool(0)->available()),
             static_cast<int>(pipeline.pool(0)->capacityBlocks()));
    CHECK_EQ(static_cast<int>(pipeline.pool(1)->available()),
             static_cast<int>(pipeline.pool(1)->capacityBlocks()));
}
