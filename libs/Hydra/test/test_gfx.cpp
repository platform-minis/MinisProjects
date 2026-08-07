/**
 * Testy warstwy graficznej (etap 4a).
 *
 * Framebuffer w pamięci pozwala sprawdzać wynik rasteryzacji piksel po pikselu,
 * bez sprzętu. To jedyny sposób, żeby wyłapać błędy o jeden w prymitywach —
 * na ekranie są niewidoczne, a psują układ interfejsu.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/gfx/Framebuffer.hpp"

using namespace hydra;
using namespace hydra::gfx;

namespace {

/** Framebuffer 32×16 RGB565 z własnym buforem — domyślne płótno testów. */
struct Canvas {
    static constexpr i16 kW = 32;
    static constexpr i16 kH = 16;

    u8          buffer[Framebuffer::bytesNeeded(kW, kH, PixelFormat::Rgb565)] = {};
    Framebuffer fb;

    Canvas() { fb.attach(ByteSpan{buffer, sizeof(buffer)}, kW, kH, PixelFormat::Rgb565); }

    Color at(i16 x, i16 y) { return fb.pixelAt(x, y).value_or(colors::transparent); }
    bool  isSet(i16 x, i16 y) { return at(x, y) != colors::black; }

    /** Liczba pikseli różnych od czerni — do sprawdzania „ile w ogóle narysowano". */
    int litCount() {
        int n = 0;
        for (i16 y = 0; y < kH; ++y) {
            for (i16 x = 0; x < kW; ++x) {
                if (isSet(x, y)) ++n;
            }
        }
        return n;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Kolor
// ---------------------------------------------------------------------------

TEST("Color: konwersje w obie strony zachowują skrajne wartości") {
    CHECK_EQ(static_cast<int>(colors::black.rgb565()), 0x0000);
    CHECK_EQ(static_cast<int>(colors::white.rgb565()), 0xFFFF);

    // Rozciągnięcie 5/6 bitów musi zaokrąglać: samo przesunięcie w lewo dałoby
    // biel o wartości 248, czyli widocznie szarą.
    const Color white = Color::fromRgb565(0xFFFF);
    CHECK_EQ(static_cast<int>(white.r), 255);
    CHECK_EQ(static_cast<int>(white.g), 255);
    CHECK_EQ(static_cast<int>(white.b), 255);

    const Color red = Color::fromRgb565(0xF800);
    CHECK_EQ(static_cast<int>(red.r), 255);
    CHECK_EQ(static_cast<int>(red.g), 0);
    CHECK_EQ(static_cast<int>(red.b), 0);

    CHECK_EQ(static_cast<int>(Color::fromRgb888(0x336699).r), 0x33);
    CHECK_EQ(static_cast<int>(Color::fromRgb888(0x336699).b), 0x99);
    CHECK_EQ(static_cast<long long>(Color(0x11, 0x22, 0x33).rgb888()), 0x112233LL);
}

TEST("Color: progowanie na wyświetlaczu jednobitowym") {
    CHECK(colors::white.mono());
    CHECK(!colors::black.mono());
    // Kolory nasycone rozkładają się po obu stronach progu zgodnie z Rec.601 —
    // żółty jest jasny, granatowy ciemny, mimo pełnego nasycenia obu.
    CHECK(colors::yellow.mono());
    CHECK(!Color(0, 0, 128).mono());
}

TEST("Color: mieszanie z kanałem alfa") {
    const Color half = Color(255, 255, 255, 128);
    const Color out  = half.over(colors::black);
    CHECK(out.r > 120 && out.r < 135);
    CHECK(out.opaque());

    CHECK(Color(1, 2, 3, 0).over(colors::white) == colors::white);
    CHECK(colors::white.over(colors::black) == colors::white);
}

// ---------------------------------------------------------------------------
// Geometria
// ---------------------------------------------------------------------------

TEST("Rect: przecięcie, suma i zawieranie") {
    const Rect a(0, 0, 10, 10);
    const Rect b(5, 5, 10, 10);

    CHECK(a.intersect(b) == Rect(5, 5, 5, 5));
    CHECK(a.intersect(Rect(20, 20, 5, 5)).empty());
    CHECK(a.unite(b) == Rect(0, 0, 15, 15));

    CHECK(a.contains(0, 0));
    CHECK(a.contains(9, 9));
    CHECK(!a.contains(10, 10));
    CHECK(!a.contains(-1, 0));

    // Suma z pustym prostokątem nie może rozciągać wyniku do początku układu.
    CHECK(a.unite(Rect()) == a);
    CHECK(Rect().unite(a) == a);
}

TEST("Rect: konstrukcja z narożników niezależna od kolejności") {
    CHECK(Rect::fromCorners(2, 3, 5, 7) == Rect(2, 3, 4, 5));
    CHECK(Rect::fromCorners(5, 7, 2, 3) == Rect(2, 3, 4, 5));
    CHECK(Rect::fromCorners(4, 4, 4, 4) == Rect(4, 4, 1, 1));
}

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------

TEST("Framebuffer: rozmiar bufora zależy od formatu") {
    // 1 bit na piksel z wyrównaniem wiersza do bajtu: 128 px → 16 bajtów.
    CHECK_EQ(static_cast<int>(Framebuffer::bytesNeeded(128, 64, PixelFormat::Mono1)),
             16 * 64);
    CHECK_EQ(static_cast<int>(Framebuffer::bytesNeeded(128, 64, PixelFormat::Rgb565)),
             128 * 2 * 64);
    CHECK_EQ(static_cast<int>(Framebuffer::bytesNeeded(10, 10, PixelFormat::Rgba8888)),
             400);
    // Szerokość niepodzielna przez 8 dopełnia wiersz do pełnego bajtu.
    CHECK_EQ(static_cast<int>(Framebuffer::bytesNeeded(9, 2, PixelFormat::Mono1)), 4);
}

TEST("Framebuffer: za mały bufor jest odrzucany") {
    u8          tiny[8] = {};
    Framebuffer fb;
    CHECK(fb.attach(ByteSpan{tiny, sizeof(tiny)}, 32, 16, PixelFormat::Rgb565).error() ==
          Err::OutOfRange);
    CHECK(!fb.attached());

    // Operacje na niepodpiętym buforze zgłaszają błąd zamiast pisać w pustkę.
    CHECK(fb.fill(colors::white).error() == Err::NotInitialized);
}

TEST("Framebuffer: zapis i odczyt piksela") {
    Canvas c;
    REQUIRE(c.fb.drawPixel(3, 4, colors::white).has_value());

    CHECK(c.at(3, 4) == colors::white);
    CHECK(c.at(3, 5) == colors::black);
    CHECK_EQ(c.litCount(), 1);
}

TEST("Framebuffer: piksele poza powierzchnią są pomijane, nie są błędem") {
    Canvas c;
    // Rysowanie częściowo poza ekranem to normalna sytuacja (przewijana lista),
    // więc kończy się sukcesem i nie zostawia śladu.
    CHECK(c.fb.drawPixel(-1, 0, colors::white).has_value());
    CHECK(c.fb.drawPixel(0, -1, colors::white).has_value());
    CHECK(c.fb.drawPixel(Canvas::kW, 0, colors::white).has_value());
    CHECK(c.fb.drawPixel(0, Canvas::kH, colors::white).has_value());
    CHECK_EQ(c.litCount(), 0);
}

TEST("Framebuffer: piksel całkowicie przezroczysty nie zmienia zawartości") {
    Canvas c;
    REQUIRE(c.fb.drawPixel(1, 1, colors::white).has_value());
    REQUIRE(c.fb.drawPixel(1, 1, colors::transparent).has_value());
    CHECK(c.at(1, 1) == colors::white);
}

TEST("Framebuffer: format jednobitowy zapisuje właściwe bity") {
    u8          buf[Framebuffer::bytesNeeded(16, 2, PixelFormat::Mono1)] = {};
    Framebuffer fb;
    REQUIRE(fb.attach(ByteSpan{buf, sizeof(buf)}, 16, 2, PixelFormat::Mono1).has_value());

    REQUIRE(fb.drawPixel(0, 0, colors::white).has_value());
    REQUIRE(fb.drawPixel(7, 0, colors::white).has_value());
    REQUIRE(fb.drawPixel(8, 1, colors::white).has_value());

    // Bit najstarszy odpowiada lewej kolumnie — układ zgodny z bitmapami
    // Adafruit i pamięcią obrazu kontrolerów e-papieru.
    CHECK_EQ(static_cast<int>(buf[0]), 0x81);
    CHECK_EQ(static_cast<int>(buf[1]), 0x00);
    CHECK_EQ(static_cast<int>(buf[3]), 0x80);

    // Zapis czerni gasi bit, zamiast go zostawiać.
    REQUIRE(fb.drawPixel(0, 0, colors::black).has_value());
    CHECK_EQ(static_cast<int>(buf[0]), 0x01);
}

TEST("Framebuffer: wypełnienie kolorem obejmuje wszystkie piksele") {
    Canvas c;
    REQUIRE(c.fb.fill(colors::white).has_value());
    CHECK_EQ(c.litCount(), Canvas::kW * Canvas::kH);
    CHECK(c.at(0, 0) == colors::white);
    CHECK(c.at(Canvas::kW - 1, Canvas::kH - 1) == colors::white);
}

TEST("Framebuffer: flush woła funkcję prezentacji z zawartością bufora") {
    Canvas c;
    int    presents = 0;
    size_t bytes    = 0;
    Size   reported;

    c.fb.setPresent([&](CByteSpan data, Size s, PixelFormat) {
        ++presents;
        bytes    = data.size();
        reported = s;
        return ok();
    });

    REQUIRE(c.fb.drawPixel(0, 0, colors::white).has_value());
    REQUIRE(c.fb.flush().has_value());

    CHECK_EQ(presents, 1);
    CHECK_EQ(static_cast<int>(bytes), static_cast<int>(sizeof(c.buffer)));
    CHECK(reported == Size(Canvas::kW, Canvas::kH));
    // Po transferze obszar zmieniony jest pusty.
    CHECK(c.fb.dirty().empty());
}

// ---------------------------------------------------------------------------
// Przycinanie
// ---------------------------------------------------------------------------

TEST("Przycinanie: domyślnie obowiązuje cała powierzchnia") {
    Canvas c;
    CHECK(c.fb.clip() == c.fb.bounds());
}

TEST("Przycinanie: ogranicza wszystkie prymitywy") {
    Canvas c;
    c.fb.setClip(Rect(4, 4, 4, 4));

    REQUIRE(c.fb.fill(colors::white).has_value());
    CHECK_EQ(c.litCount(), 16);
    CHECK(c.isSet(4, 4));
    CHECK(c.isSet(7, 7));
    CHECK(!c.isSet(3, 4));
    CHECK(!c.isSet(8, 8));

    c.fb.resetClip();
    CHECK(c.fb.clip() == c.fb.bounds());
}

TEST("Przycinanie: obszar nie może wyjść poza powierzchnię") {
    Canvas c;
    c.fb.setClip(Rect(-10, -10, 1000, 1000));
    CHECK(c.fb.clip() == c.fb.bounds());
}

// ---------------------------------------------------------------------------
// Obszar zmieniony
// ---------------------------------------------------------------------------

TEST("Obszar zmieniony: obejmuje dokładnie to, co narysowano") {
    Canvas c;
    CHECK(c.fb.dirty().empty());

    REQUIRE(c.fb.drawPixel(5, 6, colors::white).has_value());
    CHECK(c.fb.dirty() == Rect(5, 6, 1, 1));

    REQUIRE(c.fb.drawPixel(10, 2, colors::white).has_value());
    // Suma obu punktów, nie tylko ostatniego.
    CHECK(c.fb.dirty() == Rect(5, 2, 6, 5));

    c.fb.clearDirty();
    CHECK(c.fb.dirty().empty());
}

TEST("Obszar zmieniony: rysowanie poza ekranem go nie powiększa") {
    Canvas c;
    REQUIRE(c.fb.fillRect(Rect(-5, -5, 3, 3), colors::white).has_value());
    // Cały prostokąt leży poza powierzchnią — nie ma czego odświeżać.
    CHECK(c.fb.dirty().empty());
}

// ---------------------------------------------------------------------------
// Prymitywy
// ---------------------------------------------------------------------------

TEST("fillRect: wypełnia dokładnie zadany obszar") {
    Canvas c;
    REQUIRE(c.fb.fillRect(Rect(2, 3, 4, 5), colors::white).has_value());

    CHECK_EQ(c.litCount(), 20);
    CHECK(c.isSet(2, 3));
    CHECK(c.isSet(5, 7));
    CHECK(!c.isSet(1, 3));
    CHECK(!c.isSet(6, 3));
    CHECK(!c.isSet(2, 8));
}

TEST("drawRect: rysuje sam obrys, bez wnętrza") {
    Canvas c;
    REQUIRE(c.fb.drawRect(Rect(1, 1, 5, 4), colors::white).has_value());

    // Obwód prostokąta 5×4 to 2*(5+4) - 4 naroża = 14 pikseli.
    CHECK_EQ(c.litCount(), 14);
    CHECK(c.isSet(1, 1));
    CHECK(c.isSet(5, 1));
    CHECK(c.isSet(1, 4));
    CHECK(c.isSet(5, 4));
    CHECK(!c.isSet(3, 2));  // wnętrze zostaje puste
}

TEST("Linie: poziome i pionowe trafiają w te same piksele co prostokąt") {
    Canvas c;
    REQUIRE(c.fb.hLine(2, 5, 6, colors::white).has_value());
    CHECK_EQ(c.litCount(), 6);
    CHECK(c.isSet(2, 5));
    CHECK(c.isSet(7, 5));
    CHECK(!c.isSet(8, 5));

    Canvas v;
    REQUIRE(v.fb.vLine(3, 2, 4, colors::white).has_value());
    CHECK_EQ(v.litCount(), 4);
    CHECK(v.isSet(3, 2));
    CHECK(v.isSet(3, 5));
}

TEST("Linie: ujemna długość rysuje w drugą stronę, nie znika") {
    Canvas c;
    REQUIRE(c.fb.hLine(10, 5, -4, colors::white).has_value());
    CHECK_EQ(c.litCount(), 4);
    CHECK(c.isSet(7, 5));
    CHECK(c.isSet(10, 5));
}

TEST("Linia ukośna: oba końce są narysowane") {
    Canvas c;
    REQUIRE(c.fb.line(0, 0, 7, 7, colors::white).has_value());

    CHECK(c.isSet(0, 0));
    CHECK(c.isSet(7, 7));
    CHECK(c.isSet(3, 3));
    CHECK_EQ(c.litCount(), 8);
}

TEST("Linia ukośna: kierunek rysowania nie zmienia zbioru pikseli") {
    Canvas a, b;
    REQUIRE(a.fb.line(1, 2, 9, 6, colors::white).has_value());
    REQUIRE(b.fb.line(9, 6, 1, 2, colors::white).has_value());

    // Odwrócenie końców to najczęstsze źródło rozjeżdżających się o piksel
    // krawędzi w interfejsie.
    CHECK_EQ(memcmp(a.buffer, b.buffer, sizeof(a.buffer)), 0);
}

TEST("Okrąg: obrys jest pusty w środku, wypełnienie nie") {
    Canvas outline;
    REQUIRE(outline.fb.drawCircle(8, 8, 5, colors::white).has_value());
    CHECK(!outline.isSet(8, 8));
    CHECK(outline.isSet(8, 3));   // punkt górny
    CHECK(outline.isSet(8, 13));  // dolny
    CHECK(outline.isSet(3, 8));   // lewy
    CHECK(outline.isSet(13, 8));  // prawy

    Canvas filled;
    REQUIRE(filled.fb.fillCircle(8, 8, 5, colors::white).has_value());
    CHECK(filled.isSet(8, 8));
    CHECK(filled.litCount() > outline.litCount());
    // Poza promieniem nadal czysto.
    CHECK(!filled.isSet(8, 2));
    CHECK(!filled.isSet(2, 8));
}

TEST("Okrąg o promieniu zero to pojedynczy piksel") {
    Canvas c;
    REQUIRE(c.fb.drawCircle(5, 5, 0, colors::white).has_value());
    CHECK_EQ(c.litCount(), 1);
    CHECK(c.isSet(5, 5));

    CHECK(c.fb.drawCircle(5, 5, -1, colors::white).error() == Err::BadArgument);
}

TEST("Prostokąt zaokrąglony: naroża są ścięte") {
    Canvas c;
    REQUIRE(c.fb.fillRoundRect(Rect(2, 2, 12, 10), 3, colors::white).has_value());

    CHECK(!c.isSet(2, 2));    // narożnik wycięty
    CHECK(!c.isSet(13, 11));
    CHECK(c.isSet(8, 2));     // środek górnej krawędzi
    CHECK(c.isSet(2, 7));     // środek lewej krawędzi
    CHECK(c.isSet(8, 7));     // wnętrze
}

TEST("Prostokąt zaokrąglony: zbyt duży promień jest przycinany") {
    Canvas big, clamped;
    // Promień większy niż połowa krótszego boku dałby nachodzące narożniki.
    REQUIRE(big.fb.fillRoundRect(Rect(2, 2, 10, 8), 100, colors::white).has_value());
    REQUIRE(clamped.fb.fillRoundRect(Rect(2, 2, 10, 8), 4, colors::white).has_value());
    CHECK_EQ(memcmp(big.buffer, clamped.buffer, sizeof(big.buffer)), 0);

    // Promień zerowy to zwykły prostokąt.
    Canvas sharp, plain;
    REQUIRE(sharp.fb.fillRoundRect(Rect(1, 1, 6, 6), 0, colors::white).has_value());
    REQUIRE(plain.fb.fillRect(Rect(1, 1, 6, 6), colors::white).has_value());
    CHECK_EQ(memcmp(sharp.buffer, plain.buffer, sizeof(sharp.buffer)), 0);
}

TEST("Trójkąt: wypełnienie pokrywa obrys") {
    Canvas outline, filled;
    const Point a(4, 2), b(12, 4), c(6, 12);

    REQUIRE(outline.fb.drawTriangle(a, b, c, colors::white).has_value());
    REQUIRE(filled.fb.fillTriangle(a, b, c, colors::white).has_value());

    CHECK(filled.litCount() > outline.litCount());
    // Każdy piksel obrysu musi znaleźć się w wypełnieniu — inaczej krawędź
    // wystawałaby poza figurę.
    for (i16 y = 0; y < Canvas::kH; ++y) {
        for (i16 x = 0; x < Canvas::kW; ++x) {
            if (outline.isSet(x, y)) CHECK(filled.isSet(x, y));
        }
    }
}

TEST("Trójkąt zdegenerowany do linii nadal się rysuje") {
    Canvas c;
    REQUIRE(c.fb.fillTriangle(Point(2, 5), Point(8, 5), Point(5, 5), colors::white)
                .has_value());
    CHECK_EQ(c.litCount(), 7);
    CHECK(c.isSet(2, 5));
    CHECK(c.isSet(8, 5));
}

// ---------------------------------------------------------------------------
// Bitmapy
// ---------------------------------------------------------------------------

TEST("Bitmapa jednobitowa: bit najstarszy to lewa kolumna") {
    Canvas c;
    // Dwa wiersze po 8 pikseli: 0b10000001 i 0b00011000.
    const u8 bitmap[] = {0x81, 0x18};
    REQUIRE(c.fb.drawBitmap1(2, 3, bitmap, 8, 2, colors::white).has_value());

    CHECK(c.isSet(2, 3));
    CHECK(c.isSet(9, 3));
    CHECK(!c.isSet(3, 3));
    CHECK(c.isSet(5, 4));
    CHECK(c.isSet(6, 4));
    CHECK_EQ(c.litCount(), 4);
}

TEST("Bitmapa jednobitowa z tłem maluje także zerowe bity") {
    Canvas c;
    const u8 bitmap[] = {0x80};
    REQUIRE(c.fb.drawBitmap1(0, 0, bitmap, 8, 1, colors::white, colors::blue).has_value());

    CHECK(c.at(0, 0) == colors::white);
    CHECK(c.at(1, 0) != colors::white);
    CHECK(c.at(1, 0) != colors::black);  // tło zostało narysowane
}

TEST("Bitmapa RGB565 zachowuje kolory") {
    Canvas c;
    const u16 pixels[] = {colors::red.rgb565(), colors::green.rgb565(),
                          colors::blue.rgb565(), colors::white.rgb565()};
    REQUIRE(c.fb.drawBitmapRgb565(1, 1, pixels, 2, 2).has_value());

    CHECK(c.at(1, 1).r > 200 && c.at(1, 1).g < 80);
    CHECK(c.at(2, 2).r > 200 && c.at(2, 2).b > 200);
}

TEST("Bitmapy: brak danych to błąd argumentu") {
    Canvas c;
    CHECK(c.fb.drawBitmap1(0, 0, nullptr, 8, 8, colors::white).error() == Err::BadArgument);
    CHECK(c.fb.drawBitmapRgb565(0, 0, nullptr, 2, 2).error() == Err::BadArgument);
}

// ---------------------------------------------------------------------------
// Tekst
// ---------------------------------------------------------------------------

TEST("Czcionka wbudowana: opis zgadza się z danymi") {
    const Font& f = font8x8();
    CHECK(f.valid());
    CHECK_EQ(static_cast<int>(f.width), 8);
    CHECK_EQ(static_cast<int>(f.height), 8);
    CHECK_EQ(static_cast<int>(f.bytesPerGlyph()), 8);
    CHECK(!f.msbFirst);  // tablica z MinisGfx zaczyna od bitu najmłodszego

    CHECK(f.covers('A'));
    CHECK(f.covers(' '));
    CHECK(!f.covers('\n'));
    CHECK(f.glyph('\n') == nullptr);
}

TEST("Czcionka wbudowana: kształt litery A jest poprawny") {
    const Font& f     = font8x8();
    const u8*   glyph = f.glyph('A');
    REQUIRE(glyph != nullptr);

    // Wiersz górny: dwa piksele wierzchołka, wyśrodkowane.
    CHECK(!f.pixel(glyph, 1, 0));
    CHECK(f.pixel(glyph, 2, 0));
    CHECK(f.pixel(glyph, 3, 0));
    CHECK(!f.pixel(glyph, 4, 0));

    // Wiersz z poprzeczką: pełne pięć pikseli wszerz.
    CHECK(f.pixel(glyph, 0, 4));
    CHECK(f.pixel(glyph, 5, 4));

    // Spacja jest pusta na całej powierzchni.
    const u8* space = f.glyph(' ');
    REQUIRE(space != nullptr);
    for (u8 row = 0; row < 8; ++row) {
        for (u8 col = 0; col < 8; ++col) CHECK(!f.pixel(space, col, row));
    }
}

TEST("Pomiary tekstu uwzględniają skalę i nowe wiersze") {
    const Font& f = font8x8();
    CHECK_EQ(static_cast<int>(textWidth(f, "abc")), 24);
    CHECK_EQ(static_cast<int>(textWidth(f, "abc", 2)), 48);
    CHECK_EQ(static_cast<int>(textHeight(f, 2)), 16);
    CHECK_EQ(static_cast<int>(textWidth(f, "")), 0);

    // Szerokość napisu wielowierszowego to szerokość najdłuższego wiersza.
    CHECK_EQ(static_cast<int>(textWidth(f, "ab\ncdef")), 32);
}

TEST("drawText: rysuje znaki i przesuwa się o odstęp") {
    Canvas c;
    REQUIRE(c.fb.drawText(0, 0, "A", colors::white, font8x8()).has_value());
    const int oneChar = c.litCount();
    CHECK(oneChar > 0);

    Canvas two;
    REQUIRE(two.fb.drawText(0, 0, "AA", colors::white, font8x8()).has_value());
    CHECK_EQ(two.litCount(), oneChar * 2);
    // Drugi znak zaczyna się o pełny odstęp dalej.
    CHECK(two.isSet(10, 0));
}

TEST("drawText: nowa linia przenosi kursor na początek wiersza") {
    Canvas c;
    REQUIRE(c.fb.drawText(0, 0, "A\nA", colors::white, font8x8()).has_value());

    CHECK(c.isSet(2, 0));  // wierzchołek pierwszej litery
    CHECK(c.isSet(2, 8));  // druga litera wiersz niżej, od tej samej kolumny
}

TEST("drawText: znak spoza czcionki jest pomijany bez błędu") {
    Canvas c;
    const char text[] = {'A', static_cast<char>(0x01), 'A', '\0'};
    REQUIRE(c.fb.drawText(0, 0, text, colors::white, font8x8()).has_value());

    // Znak sterujący nie rysuje się, ale zajmuje miejsce — inaczej tekst
    // przeskakiwałby i psuł wyrównanie kolumn.
    CHECK(c.isSet(2, 0));
    CHECK(c.isSet(18, 0));
}

TEST("drawText: skala powiększa każdy piksel glifu") {
    Canvas c;
    REQUIRE(c.fb.drawText(0, 0, "A", colors::white, font8x8(), 2).has_value());

    // Piksel (2,0) glifu zajmuje teraz kwadrat 2×2 od (4,0).
    CHECK(c.isSet(4, 0));
    CHECK(c.isSet(5, 0));
    CHECK(c.isSet(4, 1));
    CHECK(c.isSet(5, 1));
}

TEST("drawText z tłem maluje prostokąt pod napisem") {
    Canvas c;
    REQUIRE(c.fb.drawText(1, 1, "A", colors::white, colors::blue, font8x8()).has_value());

    // Tło pokrywa całą komórkę znaku, także tam, gdzie glif jest pusty.
    CHECK(c.at(1, 1) != colors::black);
    CHECK(c.at(8, 8) != colors::black);
    CHECK(c.at(9, 9) == colors::black);  // poza komórką już nie
}

TEST("Tekst: błędne argumenty są odrzucane") {
    Canvas c;
    CHECK(c.fb.drawText(0, 0, nullptr, colors::white, font8x8()).error() == Err::BadArgument);
    CHECK(c.fb.drawText(0, 0, "x", colors::white, font8x8(), 0).error() == Err::BadArgument);

    const Font empty;
    CHECK(c.fb.drawChar(0, 0, 'A', colors::white, empty).error() == Err::BadArgument);
}
