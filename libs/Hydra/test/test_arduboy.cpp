/**
 * Testy warstwy zgodności z Arduboy2.
 *
 * Sprawdzamy przede wszystkim to, co odróżnia „skompilowało się" od „gra
 * działa tak samo": układ bufora, metryki czcionki, semantykę kolizji
 * i zachowanie taktu klatek na krawędziach. Rzeczy, których niezgodność
 * objawiłaby się dopiero jako dziwnie wyglądająca gra.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include "hydra/arduboy/Arduboy2.hpp"
#include "hydra/arduboy/Eeprom.hpp"
#include "hydra/arduboy/Tones.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra_test.hpp"

using namespace hydra;
using namespace hydra::arduboy;

namespace {

/** Zlicza zapalone piksele — tańsze niż porównywanie całych buforów. */
u16 litPixels(const Arduboy2Base& game) {
    u16 count = 0;
    for (i16 y = 0; y < kHeight; ++y) {
        for (i16 x = 0; x < kWidth; ++x) {
            if (game.getPixel(static_cast<u8>(x), static_cast<u8>(y))) ++count;
        }
    }
    return count;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  Układ bufora
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: bufor jest stronicowy jak w oryginale") {
    Arduboy2Base game;
    game.clear();

    // To jest kontrakt, na którym opiera się połowa gier: piksel (3, 9) siedzi
    // w bajcie `sBuffer[3 + 1 * 128]`, na bicie 1. Gra rysująca wprost do
    // bufora liczy dokładnie tak.
    game.drawPixel(3, 9, kWhite);

    CHECK_EQ(game.sBuffer[3 + 1 * kWidth], 0x02);
    CHECK(game.getPixel(3, 9));
    CHECK_EQ(litPixels(game), 1u);
}

TEST("arduboy: BLACK gasi piksel, INVERT go przełącza") {
    Arduboy2Base game;
    game.clear();

    game.drawPixel(10, 10, kWhite);
    game.drawPixel(10, 10, kBlack);
    CHECK(!(game.getPixel(10, 10)));

    game.drawPixel(10, 10, kInvert);
    CHECK(game.getPixel(10, 10));
    game.drawPixel(10, 10, kInvert);
    CHECK(!(game.getPixel(10, 10)));
}

TEST("arduboy: rysowanie poza ekranem nie tyka pamięci") {
    Arduboy2Base game;
    game.clear();

    // Poza ekranem: żaden z tych zapisów nie może dotknąć pamięci.
    game.drawPixel(-1, 10, kWhite);
    game.drawPixel(kWidth, 10, kWhite);
    game.drawPixel(10, -1, kWhite);
    game.drawPixel(10, kHeight, kWhite);

    CHECK_EQ(litPixels(game), 0u);
}

TEST("arduboy: display() przekłada strony na wiersze gfx") {
    Arduboy2Base game;
    game.clear();

    // Piksel (0, 0) w układzie stronicowym trafia na najstarszy bit
    // pierwszego bajtu układu wierszowego — tego, którego oczekuje gfx.
    game.drawPixel(0, 0, kWhite);
    // Piksel (8, 1) to drugi bajt drugiego wiersza, znowu bit najstarszy.
    game.drawPixel(8, 1, kWhite);

    bool flushed = false;
    game.setFlush([&flushed] { flushed = true; return ok(); });
    game.display();

    CHECK(flushed);

    const u8* mono = game.monoBuffer().data();
    constexpr u32 stride = kWidth / 8;
    CHECK_EQ(mono[0], 0x80);
    CHECK_EQ(mono[stride + 1], 0x80);
}

TEST("arduboy: fillScreen obejmuje cały ekran") {
    Arduboy2Base game;
    game.fillScreen(kWhite);
    CHECK_EQ(litPixels(game), static_cast<u16>(kWidth * kHeight));

    game.fillScreen(kBlack);
    CHECK_EQ(litPixels(game), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Prymitywy
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: drawRect rysuje sam obwód") {
    Arduboy2Base game;
    game.clear();
    game.drawRect(0, 0, 10, 10, kWhite);

    // Obwód prostokąta 10×10 to 36 pikseli, nie 40: narożniki należą
    // jednocześnie do boku poziomego i pionowego.
    CHECK_EQ(litPixels(game), 36u);
    CHECK(game.getPixel(0, 0));
    CHECK(!(game.getPixel(5, 5)));
}

TEST("arduboy: fillRect wypełnia dokładny prostokąt") {
    Arduboy2Base game;
    game.clear();
    game.fillRect(2, 3, 10, 5, kWhite);

    CHECK_EQ(litPixels(game), 50u);
    CHECK(game.getPixel(2, 3));
    CHECK(game.getPixel(11, 7));
    CHECK(!(game.getPixel(12, 7)));
}

TEST("arduboy: drawLine obejmuje oba końce") {
    Arduboy2Base game;
    game.clear();
    game.drawLine(0, 5, 9, 5, kWhite);

    CHECK_EQ(litPixels(game), 10u);
    CHECK(game.getPixel(0, 5));
    CHECK(game.getPixel(9, 5));
}

TEST("arduboy: fillTriangle pokrywa obrys") {
    Arduboy2Base game;

    game.clear();
    game.drawTriangle(0, 0, 10, 0, 5, 8, kWhite);
    const u16 outline = litPixels(game);

    game.clear();
    game.fillTriangle(0, 0, 10, 0, 5, 8, kWhite);
    const u16 filled = litPixels(game);

    // Wypełnienie musi objąć obrys: gra rysująca pocisk wypełnionym trójkątem
    // i sprawdzająca kolizję po obrysie zobaczyłaby inaczej dziury.
    CHECK(filled > outline);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Kolizje
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: kolizja punktu z prostokątem") {
    const Rect box{10, 10, 20, 20};

    CHECK(collide(Point{10, 10}, box));
    CHECK(collide(Point{29, 29}, box));
    // Krawędź prawa i dolna są poza — prostokąt 20 szeroki od x=10 kończy
    // się na x=29, nie na x=30.
    CHECK(!(collide(Point{30, 20}, box)));
    CHECK(!(collide(Point{9, 10}, box)));
}

TEST("arduboy: styk krawędziami to nie kolizja") {
    const Rect a{0, 0, 10, 10};
    const Rect touching{10, 0, 10, 10};
    const Rect overlapping{9, 0, 10, 10};

    // Styk krawędziami to nie kolizja — na tym opiera się ruch po kratce.
    CHECK(!(collide(a, touching)));
    CHECK(collide(a, overlapping));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Tekst
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: znak zajmuje sześć pikseli") {
    Arduboy2 game;
    game.clear();
    game.setCursor(0, 0);
    game.print("AB");

    // Sześć pikseli na znak i osiem na wiersz to metryki oryginału. Gra
    // ustawiająca kursor na (0, 8) trafia w drugi wiersz tylko wtedy, gdy
    // te liczby się zgadzają.
    CHECK_EQ(game.getCursorX(), 12);
    CHECK_EQ(game.getCursorY(), 0);
}

TEST("arduboy: nowy wiersz wraca na lewy margines") {
    Arduboy2 game;
    game.setCursor(30, 0);
    game.println("x");

    CHECK_EQ(game.getCursorX(), 0);
    CHECK_EQ(game.getCursorY(), 8);
}

TEST("arduboy: skala tekstu mnoży metryki, zero jest odrzucane") {
    Arduboy2 game;
    game.setTextSize(2);
    game.setCursor(0, 0);
    game.print("A");

    CHECK_EQ(game.getCursorX(), 12);

    // Zero jest odrzucane: dałoby kursor stojący w miejscu.
    game.setTextSize(0);
    CHECK_EQ(game.getTextSize(), 1);
}

TEST("arduboy: glif faktycznie zapala piksele") {
    Arduboy2 game;
    game.clear();
    game.setCursor(0, 0);
    game.print("A");

    const u16 lit = litPixels(game);
    // Litera „A" ma zapalone piksele, ale nie wypełnia całej komórki 6×8.
    CHECK(lit > 0);
    CHECK(lit < 48);
}

TEST("arduboy: czcionka pokrywa cały drukowalny ASCII") {
    Arduboy2 game;

    // Każdy znak drukowalny musi coś narysować. Pusty glif w środku zakresu
    // oznaczałby dziurę w napisie i wyszedłby dopiero na ekranie gry.
    for (u8 c = 0x21; c < 0x7F; ++c) {
        game.clear();
        game.setCursor(0, 0);
        game.write(c);
        CHECK(litPixels(game) > 0);
    }

    // Spacja jest jedynym znakiem, który ma nic nie rysować.
    game.clear();
    game.setCursor(0, 0);
    game.write(static_cast<u8>(' '));
    CHECK_EQ(litPixels(game), 0u);
}

TEST("arduboy: print formatuje liczby jak Arduino") {
    /** Ujście tekstu do bufora znakowego zamiast na ekran. */
    struct Capture : Print {
        char buffer[64] = {};
        size_t length   = 0;

        size_t write(u8 c) override {
            if (length + 1 < sizeof(buffer)) buffer[length++] = static_cast<char>(c);
            return 1;
        }
    };

    Capture out;
    out.print(-42);
    CHECK_STR(out.buffer, "-42");

    Capture hex;
    hex.print(255u, 16);
    CHECK_STR(hex.buffer, "FF");

    // Wartość skrajnie ujemna: negowanie przed przejściem na typ bez znaku
    // dałoby tu przepełnienie.
    Capture edge;
    edge.print(static_cast<long>(-2147483647L - 1L));
    CHECK_STR(edge.buffer, "-2147483648");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Przyciski
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: justPressed wymaga pollButtons") {
    Arduboy2Base game;
    u8 state = 0;
    game.setButtonSource([&state] { return state; });

    state = kAButton;
    // Bez `pollButtons()` zatrzask nie istnieje — to najczęstsza przyczyna
    // „gra nie reaguje na przyciski".
    CHECK(!(game.justPressed(kAButton)));

    game.pollButtons();
    CHECK(game.justPressed(kAButton));

    // Drugi obieg z tym samym stanem to już nie „właśnie wciśnięty".
    game.pollButtons();
    CHECK(!(game.justPressed(kAButton)));
    CHECK(game.pressed(kAButton));

    state = 0;
    game.pollButtons();
    CHECK(game.justReleased(kAButton));
}

TEST("arduboy: pressed żąda wszystkich bitów maski") {
    Arduboy2Base game;
    u8 state = kAButton;
    game.setButtonSource([&state] { return state; });

    CHECK(!(game.pressed(kAButton | kBButton)));
    CHECK(game.anyPressed(kAButton | kBButton));
    CHECK(!(game.notPressed(kAButton)));

    state = kAButton | kBButton;
    CHECK(game.pressed(kAButton | kBButton));
}

TEST("arduboy: brak źródła przycisków to zero, nie awaria") {
    Arduboy2Base game;
    // Brak podłączonego źródła nie może się skończyć skokiem pod pusty
    // wskaźnik — gra na płytce bez przycisków ma po prostu stać.
    CHECK_EQ(game.buttonsState(), 0);
    CHECK(!(game.pressed(kAButton)));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Takt klatek
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: setFrameRate(0) jest odrzucane") {
    Arduboy2Base game;
    game.setFrameRate(60);
    game.setFrameRate(0);   // dzielenie przez zero w oryginale

    // Zostaje poprzednia wartość — gra ma zwolnić, nie stanąć.
    game.boot();
    CHECK(game.nextFrame());
}

TEST("arduboy: nextFrame odmierza okres klatki") {
    Arduboy2Base game;
    game.setFrameRate(100);   // 10 ms na klatkę
    game.boot();

    CHECK(game.nextFrame());
    // Natychmiast po klatce następnej jeszcze nie ma.
    CHECK(!(game.nextFrame()));

    rtos::delayMs(15);
    CHECK(game.nextFrame());
}

TEST("arduboy: everyXFrames dzieli licznik klatek") {
    Arduboy2Base game;
    game.setFrameRate(250);   // 4 ms
    game.boot();

    u16 everySecond = 0;
    for (u16 i = 0; i < 10; ++i) {
        while (!game.nextFrame()) rtos::delayMs(1);
        if (game.everyXFrames(2)) ++everySecond;
    }

    // Dziesięć klatek, co druga trafiona — dopuszczamy jedną różnicy,
    // bo licznik startuje z wartością zależną od poprzednich testów.
    CHECK(everySecond >= 4 && everySecond <= 6);
    // Dzielnik zero nie może dzielić.
    CHECK(!(game.everyXFrames(0)));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Duszki
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: drawSelfMasked zostawia tło") {
    Arduboy2Base game;
    Sprites::bind(&game);

    // Duszek 8×8: dwa bajty nagłówka, potem kolumna na bajt.
    static const u8 sprite[] = {
        8, 8,
        0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00
    };

    game.clear();
    game.drawPixel(1, 3, kWhite);          // tło w miejscu pustej kolumny duszka
    Sprites::drawSelfMasked(0, 0, sprite);

    // Kolumny nieparzyste duszka są puste, więc tło musi przetrwać.
    CHECK(game.getPixel(1, 3));
    CHECK(game.getPixel(0, 0));
    CHECK_EQ(litPixels(game), static_cast<u16>(4 * 8 + 1));
}

TEST("arduboy: drawOverwrite czyści prostokąt duszka") {
    Arduboy2Base game;
    Sprites::bind(&game);

    static const u8 sprite[] = {
        8, 8,
        0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00
    };

    game.clear();
    game.drawPixel(1, 3, kWhite);
    Sprites::drawOverwrite(0, 0, sprite);

    // Wariant nadpisujący czyści cały prostokąt duszka razem z tłem.
    CHECK(!(game.getPixel(1, 3)));
    CHECK_EQ(litPixels(game), static_cast<u16>(4 * 8));
}

TEST("arduboy: drawPlusMask czyta przeplecioną maskę") {
    Arduboy2Base game;
    Sprites::bind(&game);

    // Maska przeplatana: para bajtów na kolumnę — obraz, potem maska.
    static const u8 sprite[] = {
        2, 8,
        0xFF, 0xFF,   // kolumna 0: pełna, cała pod maską
        0x00, 0x00    // kolumna 1: maska pusta — tła nie dotykamy
    };

    game.clear();
    game.drawPixel(1, 3, kWhite);
    Sprites::drawPlusMask(0, 0, sprite);

    CHECK(game.getPixel(0, 0));
    CHECK(game.getPixel(1, 3));   // przetrwało: maska tam nie sięga
    CHECK_EQ(litPixels(game), 9u);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Melodie
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: sekwencer przechodzi po nutach") {
    static const u16 melody[] = {440, 200, 880, 200, kTonesEnd};

    Tones tones;
    u16 lastFreq = 0xFFFF;
    u16 changes  = 0;
    tones.setSink([&](u16 hz, bool) { lastFreq = hz; ++changes; });

    tones.tones(melody);
    CHECK(tones.playing());
    CHECK_EQ(tones.currentFrequency(), 440);

    // Czas podajemy wprost, zamiast usypiać wątek. Poprzednia wersja usypiała
    // na 30 ms przy nutach po 20 ms, więc każde zawahanie planisty dłuższe niż
    // nuta przeskakiwało dwie naraz — i test przewracał się losowo, najczęściej
    // pod sanitizerami, które spowalniają wykonanie. Sterowanie zegarem znosi
    // zależność od obciążenia maszyny i przy okazji skraca test do zera.
    const Millis t0 = rtos::nowMs();

    tones.update(t0);
    CHECK_EQ(tones.currentFrequency(), 440);

    tones.update(t0 + 300);
    CHECK_EQ(tones.currentFrequency(), 880);

    tones.update(t0 + 600);
    CHECK(!(tones.playing()));
    CHECK_EQ(lastFreq, 0);
    CHECK(changes >= 3);
}

TEST("arduboy: wyciszenie nie zatrzymuje melodii") {
    static const u16 melody[] = {440, 50, kTonesEnd};

    // Gry przekazują tu `arduboy.audio.enabled` — statyczną metodę.
    Tones tones([] { return false; });
    tones.tones(melody);

    // Melodia biegnie — gra czeka na `playing()` — ale nic nie brzmi.
    CHECK(tones.playing());
    CHECK_EQ(tones.currentFrequency(), 0);
}

TEST("arduboy: TONES_REPEAT zapętla bez końca") {
    static const u16 loop[] = {440, 100, kTonesRepeat};

    Tones tones;
    tones.tones(loop);

    const Millis t0 = rtos::nowMs();
    tones.update(t0);

    for (u8 i = 1; i <= 5; ++i) {
        tones.update(t0 + static_cast<Millis>(i) * 150);
        // Powtórka nie może się nigdy skończyć ani zawiesić na pustej nucie.
        CHECK(tones.playing());
        CHECK_EQ(tones.currentFrequency(), 440);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pamięć nieulotna
// ═══════════════════════════════════════════════════════════════════════════

TEST("arduboy: EEPROM zapisuje i oddaje bajt") {
    EepromClass mem;

    mem.write(10, 0x5A);
    CHECK_EQ(mem.read(10), 0x5A);

    // Adresy poza zakresem nie mogą tknąć pamięci ani wywrócić programu.
    mem.write(-1, 0x11);
    mem.write(static_cast<int>(kEepromBytes), 0x11);
    CHECK_EQ(mem.read(-1), 0xFF);
    CHECK_EQ(mem.read(static_cast<int>(kEepromBytes)), 0xFF);
}

TEST("arduboy: get/put przenosi strukturę w obie strony") {
    EepromClass mem;

    struct Score {
        u16 points;
        u8  level;
    };

    const Score written{4321, 7};
    mem.put(64, written);

    Score read{};
    mem.get(64, read);
    CHECK_EQ(read.points, 4321);
    CHECK_EQ(read.level, 7);

    // Zapis tuż przy końcu pamięci musi zostać odrzucony w całości, a nie
    // przyciąć strukturę — obcięty rekord odczytałby się jako poprawny.
    Score tail{1, 1};
    mem.put(static_cast<int>(kEepromBytes) - 1, tail);
    CHECK_EQ(mem.read(static_cast<int>(kEepromBytes) - 1), 0xFF);
}

TEST("arduboy: rozmiar EEPROM zgadza się z oryginałem") {
    // Gry adresują rekordy względem tej liczby; różnica zmieniłaby ich rachunki.
    CHECK_EQ(EepromClass::length(), 1024u);
}

#endif  // HYDRA_ENABLE_ARDUBOY
