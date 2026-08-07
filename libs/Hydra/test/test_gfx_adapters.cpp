/**
 * Testy adapterów bibliotek graficznych (etap 4a).
 *
 * Adaptery są szablonami sprawdzającymi typ przez użycie, więc atrapa
 * o tym samym kształcie API co biblioteka producenta wystarcza, by sprawdzić
 * je w całości na hoście: czy operacje trafiają we właściwe metody, czy
 * konwersja koloru jest właściwa dla danej biblioteki i czy przycinanie działa
 * także wtedy, gdy biblioteka go nie zna.
 *
 * To jest właśnie powód, dla którego adaptery są szablonami, a nie klasami:
 * gdyby włączały nagłówki producenta, tego testu nie dałoby się napisać,
 * a Hydra złamałaby własną regułę zależności.
 */

#include "hydra_test.hpp"

#include "hydra/gfx/adapters/AdafruitSurface.hpp"
#include "hydra/gfx/adapters/LovyanSurface.hpp"
#include "hydra/gfx/adapters/MinisGfxSurface.hpp"
#include "hydra/gfx/adapters/TftEspiSurface.hpp"
#include "hydra/gfx/adapters/U8g2Surface.hpp"

using namespace hydra;
using namespace hydra::gfx;

namespace {

/** Zapis pojedynczego wywołania — pozwala sprawdzić, co adapter naprawdę zrobił. */
struct Call {
    const char* what = "";
    i32 a = 0, b = 0, c = 0, d = 0;
    u32 color = 0;
};

struct Recorder {
    static constexpr int kMax = 64;
    Call calls[kMax];
    int  count = 0;

    void record(const char* what, i32 a = 0, i32 b = 0, i32 c = 0, i32 d = 0, u32 col = 0) {
        if (count < kMax) calls[count++] = Call{what, a, b, c, d, col};
    }
    int countOf(const char* what) const {
        int n = 0;
        for (int i = 0; i < count; ++i) {
            if (calls[i].what == what) ++n;
        }
        return n;
    }
    const Call* first(const char* what) const {
        for (int i = 0; i < count; ++i) {
            if (calls[i].what == what) return &calls[i];
        }
        return nullptr;
    }
    void clear() { count = 0; }
};

// --- atrapy urządzeń o kształcie API poszczególnych bibliotek ---------------

/** Kształt API Adafruit_GFX. */
struct FakeAdafruit : Recorder {
    int  width() const { return 32; }
    int  height() const { return 16; }
    void drawPixel(i16 x, i16 y, u16 c) { record("pixel", x, y, 0, 0, c); }
    void fillRect(i16 x, i16 y, i16 w, i16 h, u16 c) { record("fillRect", x, y, w, h, c); }
    void fillScreen(u16 c) { record("fillScreen", 0, 0, 0, 0, c); }
    void startWrite() { record("startWrite"); }
    void endWrite() { record("endWrite"); }
};

/** Kształt API TFT_eSPI. */
struct FakeTftEspi : Recorder {
    i16  width() const { return 240; }
    i16  height() const { return 135; }
    void drawPixel(i32 x, i32 y, u32 c) { record("pixel", x, y, 0, 0, c); }
    void fillRect(i32 x, i32 y, i32 w, i32 h, u32 c) { record("fillRect", x, y, w, h, c); }
    void drawFastHLine(i32 x, i32 y, i32 w, u32 c) { record("hLine", x, y, w, 0, c); }
    void drawFastVLine(i32 x, i32 y, i32 h, u32 c) { record("vLine", x, y, h, 0, c); }
    void fillScreen(u32 c) { record("fillScreen", 0, 0, 0, 0, c); }
    void pushImage(i32 x, i32 y, i32 w, i32 h, u16*) { record("pushImage", x, y, w, h); }
    void startWrite() { record("startWrite"); }
    void endWrite() { record("endWrite"); }
};

/** Kształt API LovyanGFX — kolor jako pełne RGB888. */
struct FakeLovyan : Recorder {
    i32  width() const { return 320; }
    i32  height() const { return 240; }
    void drawPixel(i32 x, i32 y, u32 c) { record("pixel", x, y, 0, 0, c); }
    void fillRect(i32 x, i32 y, i32 w, i32 h, u32 c) { record("fillRect", x, y, w, h, c); }
    void drawFastHLine(i32 x, i32 y, i32 w, u32 c) { record("hLine", x, y, w, 0, c); }
    void drawFastVLine(i32 x, i32 y, i32 h, u32 c) { record("vLine", x, y, h, 0, c); }
    void fillScreen(u32 c) { record("fillScreen", 0, 0, 0, 0, c); }
    void drawCircle(i32 x, i32 y, i32 r, u32 c) { record("circle", x, y, r, 0, c); }
    void fillCircle(i32 x, i32 y, i32 r, u32 c) { record("fillCircle", x, y, r, 0, c); }
    void startWrite() { record("startWrite"); }
    void endWrite() { record("endWrite"); }
};

/** Kształt API U8g2 — jednobitowy, kolor ustawiany osobno. */
struct FakeU8g2 : Recorder {
    u8 drawColor = 1;

    u16  getDisplayWidth() const { return 128; }
    u16  getDisplayHeight() const { return 64; }
    void setDrawColor(u8 c) { drawColor = c; }
    void drawPixel(u16 x, u16 y) { record("pixel", x, y, drawColor); }
    void drawBox(u16 x, u16 y, u16 w, u16 h) { record("box", x, y, w, h, drawColor); }
    void clearBuffer() { record("clear"); }
    void sendBuffer() { record("send"); }
};

/** Kolor w kształcie MGColor z MinisGfx. */
struct FakeMgColor {
    u8 r, g, b, a;
    FakeMgColor(u8 red, u8 green, u8 blue, u8 alpha) : r(red), g(green), b(blue), a(alpha) {}
};

/** Kształt API MinisGfx. */
struct FakeMinisGfx : Recorder {
    int  width() const { return 200; }
    int  height() const { return 100; }
    void begin() { record("begin"); }
    void display() { record("display"); }
    void startWrite() { record("startWrite"); }
    void endWrite() { record("endWrite"); }
    void drawPixel(int x, int y, const FakeMgColor& c) {
        record("pixel", x, y, 0, 0, packed(c));
    }
    void fillRect(int x, int y, int w, int h, const FakeMgColor& c) {
        record("fillRect", x, y, w, h, packed(c));
    }
    void fillScreen(const FakeMgColor& c) { record("fillScreen", 0, 0, 0, 0, packed(c)); }
    void drawCircle(int x, int y, int r, const FakeMgColor& c) {
        record("circle", x, y, r, 0, packed(c));
    }
    void fillCircle(int x, int y, int r, const FakeMgColor& c) {
        record("fillCircle", x, y, r, 0, packed(c));
    }

    static u32 packed(const FakeMgColor& c) {
        return (static_cast<u32>(c.r) << 24) | (static_cast<u32>(c.g) << 16) |
               (static_cast<u32>(c.b) << 8) | c.a;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Adafruit_GFX
// ---------------------------------------------------------------------------

TEST("Adapter Adafruit: rozmiar i format bierze z urządzenia") {
    FakeAdafruit dev;
    AdafruitSurface<FakeAdafruit> s(dev);

    CHECK(s.size() == Size(32, 16));
    CHECK(s.pixelFormat() == PixelFormat::Rgb565);
    CHECK(s.bounds() == Rect(0, 0, 32, 16));
}

TEST("Adapter Adafruit: kolor konwertowany do RGB565") {
    FakeAdafruit dev;
    AdafruitSurface<FakeAdafruit> s(dev);

    REQUIRE(s.drawPixel(3, 4, colors::red).has_value());
    const Call* c = dev.first("pixel");
    REQUIRE(c != nullptr);
    CHECK_EQ(static_cast<int>(c->a), 3);
    CHECK_EQ(static_cast<int>(c->b), 4);
    CHECK_EQ(static_cast<long long>(c->color), static_cast<long long>(colors::red.rgb565()));
}

TEST("Adapter Adafruit: wypełnienia idą jednym wywołaniem, nie pikselami") {
    FakeAdafruit dev;
    AdafruitSurface<FakeAdafruit> s(dev);

    REQUIRE(s.fillRect(Rect(2, 2, 8, 4), colors::blue).has_value());
    // Sedno adaptera: 32 piksele to jedno wywołanie biblioteki, nie 32.
    CHECK_EQ(dev.countOf("fillRect"), 1);
    CHECK_EQ(dev.countOf("pixel"), 0);

    dev.clear();
    REQUIRE(s.fill(colors::black).has_value());
    CHECK_EQ(dev.countOf("fillScreen"), 1);
}

TEST("Adapter Adafruit: przycinanie działa mimo braku obsługi w bibliotece") {
    FakeAdafruit dev;
    AdafruitSurface<FakeAdafruit> s(dev);
    s.setClip(Rect(4, 4, 4, 4));

    REQUIRE(s.fillRect(Rect(0, 0, 32, 16), colors::white).has_value());
    const Call* c = dev.first("fillRect");
    REQUIRE(c != nullptr);
    // Adafruit_GFX nie zna przycinania — obszar musi być zawężony po naszej stronie.
    CHECK_EQ(static_cast<int>(c->a), 4);
    CHECK_EQ(static_cast<int>(c->c), 4);

    // Wypełnienie całości przy zawężonym obszarze nie może użyć fillScreen.
    dev.clear();
    REQUIRE(s.fill(colors::white).has_value());
    CHECK_EQ(dev.countOf("fillScreen"), 0);
}

TEST("Adapter Adafruit: transfer bufora przez podaną funkcję") {
    FakeAdafruit dev;
    AdafruitSurface<FakeAdafruit> s(dev);

    int flushes = 0;
    s.setFlush([&] {
        ++flushes;
        return ok();
    });

    REQUIRE(s.drawPixel(0, 0, colors::white).has_value());
    CHECK(!s.dirty().empty());
    REQUIRE(s.flush().has_value());
    CHECK_EQ(flushes, 1);
    CHECK(s.dirty().empty());
}

TEST("Adapter Adafruit: prymitywy złożone korzystają z programowej ścieżki") {
    FakeAdafruit dev;
    AdafruitSurface<FakeAdafruit> s(dev);

    // Okrąg nie ma odpowiednika w adapterze, więc idzie przez writePixel —
    // i mimo to jest narysowany.
    REQUIRE(s.drawCircle(8, 8, 3, colors::white).has_value());
    CHECK(dev.countOf("pixel") > 8);
    CHECK_EQ(dev.countOf("startWrite"), dev.countOf("endWrite"));
}

// ---------------------------------------------------------------------------
// TFT_eSPI
// ---------------------------------------------------------------------------

TEST("Adapter TFT_eSPI: linie proste trafiają w dedykowane metody") {
    FakeTftEspi dev;
    TftEspiSurface<FakeTftEspi> s(dev);

    CHECK(s.size() == Size(240, 135));

    REQUIRE(s.hLine(10, 20, 50, colors::green).has_value());
    REQUIRE(s.vLine(10, 20, 50, colors::green).has_value());
    CHECK_EQ(dev.countOf("hLine"), 1);
    CHECK_EQ(dev.countOf("vLine"), 1);
    CHECK_EQ(dev.countOf("pixel"), 0);
}

TEST("Adapter TFT_eSPI: obraz w całości idzie jednym transferem") {
    FakeTftEspi dev;
    TftEspiSurface<FakeTftEspi> s(dev);

    const u16 pixels[4] = {0, 1, 2, 3};
    REQUIRE(s.drawBitmapRgb565(10, 10, pixels, 2, 2).has_value());
    CHECK_EQ(dev.countOf("pushImage"), 1);
    CHECK_EQ(dev.countOf("pixel"), 0);
}

TEST("Adapter TFT_eSPI: obraz przycięty idzie ścieżką programową") {
    FakeTftEspi dev;
    TftEspiSurface<FakeTftEspi> s(dev);
    s.setClip(Rect(0, 0, 11, 11));

    const u16 pixels[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    REQUIRE(s.drawBitmapRgb565(10, 10, pixels, 2, 2).has_value());
    // pushImage nie zna przycinania, więc zamazałby sąsiada — stąd obejście.
    CHECK_EQ(dev.countOf("pushImage"), 0);
    CHECK_EQ(dev.countOf("pixel"), 1);
}

TEST("Adapter TFT_eSPI: transakcja obejmuje całą serię rysowania") {
    FakeTftEspi dev;
    TftEspiSurface<FakeTftEspi> s(dev);

    REQUIRE(s.line(0, 0, 20, 13, colors::white).has_value());
    // Jedna transakcja SPI zamiast otwierania jej przy każdym pikselu.
    CHECK_EQ(dev.countOf("startWrite"), 1);
    CHECK_EQ(dev.countOf("endWrite"), 1);
}

// ---------------------------------------------------------------------------
// LovyanGFX
// ---------------------------------------------------------------------------

TEST("Adapter Lovyan: kolor przekazywany jako pełne RGB888") {
    FakeLovyan dev;
    LovyanSurface<FakeLovyan> s(dev);

    CHECK(s.pixelFormat() == PixelFormat::Rgb888);

    REQUIRE(s.drawPixel(1, 1, Color(0x12, 0x34, 0x56)).has_value());
    const Call* c = dev.first("pixel");
    REQUIRE(c != nullptr);
    // Konwersja do RGB565 gubiłaby informację na panelach o większej głębi.
    CHECK_EQ(static_cast<long long>(c->color), 0x123456LL);
}

TEST("Adapter Lovyan: okręgi rysuje biblioteka, nie nasza pętla") {
    FakeLovyan dev;
    LovyanSurface<FakeLovyan> s(dev);

    REQUIRE(s.drawCircle(50, 50, 20, colors::white).has_value());
    REQUIRE(s.fillCircle(50, 50, 20, colors::white).has_value());
    CHECK_EQ(dev.countOf("circle"), 1);
    CHECK_EQ(dev.countOf("fillCircle"), 1);
    CHECK_EQ(dev.countOf("pixel"), 0);

    CHECK(s.drawCircle(0, 0, -1, colors::white).error() == Err::BadArgument);
}

// ---------------------------------------------------------------------------
// U8g2
// ---------------------------------------------------------------------------

TEST("Adapter U8g2: kolor progowany do jednego bitu") {
    FakeU8g2 dev;
    U8g2Surface<FakeU8g2> s(dev);

    CHECK(s.size() == Size(128, 64));
    CHECK(s.pixelFormat() == PixelFormat::Mono1);

    REQUIRE(s.drawPixel(1, 1, colors::white).has_value());
    REQUIRE(s.drawPixel(2, 2, Color(10, 10, 10)).has_value());

    CHECK_EQ(dev.countOf("pixel"), 2);
    // Jasny zapala, ciemny gasi — to jedyne, co ten panel potrafi.
    CHECK_EQ(static_cast<int>(dev.calls[0].c), 1);
    CHECK_EQ(static_cast<int>(dev.calls[1].c), 0);
    // Kolor rysowania wraca do jedynki, żeby nie zaskoczyć kodu aplikacji,
    // który sięga po urządzenie bezpośrednio.
    CHECK_EQ(static_cast<int>(dev.drawColor), 1);
}

TEST("Adapter U8g2: czyszczenie i transfer bufora") {
    FakeU8g2 dev;
    U8g2Surface<FakeU8g2> s(dev);

    REQUIRE(s.fill(colors::black).has_value());
    CHECK_EQ(dev.countOf("clear"), 1);
    CHECK_EQ(dev.countOf("box"), 0);  // czerń to samo wyczyszczenie

    dev.clear();
    REQUIRE(s.fill(colors::white).has_value());
    CHECK_EQ(dev.countOf("clear"), 1);
    CHECK_EQ(dev.countOf("box"), 1);  // biel wymaga zamalowania

    dev.clear();
    REQUIRE(s.flush().has_value());
    CHECK_EQ(dev.countOf("send"), 1);
}

// ---------------------------------------------------------------------------
// MinisGfx
// ---------------------------------------------------------------------------

TEST("Adapter MinisGfx: most do istniejących backendów MinisLib") {
    FakeMinisGfx dev;
    MinisGfxSurface<FakeMinisGfx, FakeMgColor> s(dev);

    CHECK(s.size() == Size(200, 100));
    // MinisGfx przenosi kolor jako RGBA8888, tak samo jak Hydra — nie ma tu
    // żadnej stratnej konwersji.
    CHECK(s.pixelFormat() == PixelFormat::Rgba8888);

    REQUIRE(s.begin().has_value());
    CHECK_EQ(dev.countOf("begin"), 1);

    REQUIRE(s.drawPixel(5, 6, Color(0x11, 0x22, 0x33, 0xFF)).has_value());
    const Call* c = dev.first("pixel");
    REQUIRE(c != nullptr);
    CHECK_EQ(static_cast<long long>(c->color), 0x112233FFLL);

    REQUIRE(s.flush().has_value());
    CHECK_EQ(dev.countOf("display"), 1);
}

TEST("Adapter MinisGfx: przycinanie dokładane przez Hydrę") {
    FakeMinisGfx dev;
    MinisGfxSurface<FakeMinisGfx, FakeMgColor> s(dev);
    s.setClip(Rect(10, 10, 20, 20));

    REQUIRE(s.fillRect(Rect(0, 0, 200, 100), colors::white).has_value());
    const Call* c = dev.first("fillRect");
    REQUIRE(c != nullptr);
    // MinisGfx nie zna przycinania; bez tej warstwy widżet zamazałby sąsiada.
    CHECK_EQ(static_cast<int>(c->a), 10);
    CHECK_EQ(static_cast<int>(c->b), 10);
    CHECK_EQ(static_cast<int>(c->c), 20);
    CHECK_EQ(static_cast<int>(c->d), 20);

    // Piksel poza obszarem nie dociera do biblioteki w ogóle.
    dev.clear();
    REQUIRE(s.drawPixel(0, 0, colors::white).has_value());
    CHECK_EQ(dev.countOf("pixel"), 0);
}

TEST("Adapter MinisGfx: prymitywy obecne w bibliotece są jej oddawane") {
    FakeMinisGfx dev;
    MinisGfxSurface<FakeMinisGfx, FakeMgColor> s(dev);

    REQUIRE(s.fillCircle(50, 50, 10, colors::white).has_value());
    CHECK_EQ(dev.countOf("fillCircle"), 1);
    CHECK_EQ(dev.countOf("pixel"), 0);

    // A te, których nie ma, rysuje ścieżka programowa Hydry.
    dev.clear();
    REQUIRE(s.drawText(0, 0, "A", colors::white, font8x8()).has_value());
    CHECK(dev.countOf("pixel") > 0);
}

// ---------------------------------------------------------------------------
// Wspólny kontrakt
// ---------------------------------------------------------------------------

TEST("Wszystkie adaptery spełniają ten sam kontrakt powierzchni") {
    FakeAdafruit a;
    FakeTftEspi  t;
    FakeLovyan   l;
    FakeU8g2     u;
    FakeMinisGfx m;

    AdafruitSurface<FakeAdafruit>              sa(a);
    TftEspiSurface<FakeTftEspi>                st(t);
    LovyanSurface<FakeLovyan>                  sl(l);
    U8g2Surface<FakeU8g2>                      su(u);
    MinisGfxSurface<FakeMinisGfx, FakeMgColor> sm(m);

    ISurface* surfaces[] = {&sa, &st, &sl, &su, &sm};

    // Ten sam kod rysujący na pięciu różnych bibliotekach — o to w tej
    // warstwie chodzi.
    for (ISurface* s : surfaces) {
        CHECK(!s->size().empty());
        CHECK(s->clip() == s->bounds());
        REQUIRE(s->fill(colors::black).has_value());
        REQUIRE(s->fillRect(Rect(2, 2, 5, 5), colors::white).has_value());
        REQUIRE(s->drawText(2, 2, "Hi", colors::white, font8x8()).has_value());
        REQUIRE(s->drawRoundRect(Rect(1, 1, 20, 10), 3, colors::white).has_value());
        REQUIRE(s->flush().has_value());
        CHECK(s->dirty().empty());
    }
}
