/**
 * @file Arduboy2.hpp
 * @brief Warstwa zgodności z biblioteką Arduboy2 — gry działają bez zmian.
 *
 * Moduł nie jest „inspirowany" Arduboyem. Jest jego **implementacją zastępczą**:
 * gra napisana na Arduboya ma się skompilować i zadziałać po podmianie
 * biblioteki, bez dotykania kodu gry. Wszystko poniżej wynika z tego założenia.
 *
 * ## Dlaczego własny bufor obrazu, a nie `gfx::Framebuffer`
 *
 * Hydra trzyma obrazy jednobitowe **wierszami**: bajt zawiera osiem sąsiednich
 * pikseli w poziomie. Arduboy trzyma je **stronami**, jak pamięć SSD1306:
 *
 *     sBuffer[x + (y / 8) * 128], bit (y & 7)
 *
 * czyli bajt to osiem pikseli w **pionie**. Różnica nie jest kosmetyczna:
 *
 *  - połowa gier rysuje wprost do `sBuffer`, bo tak jest szybciej na ATmega,
 *  - wszystkie bitmapy i duszki Arduboya są zapisane stronicowo,
 *  - `drawBitmap()` przy zgodnym `y` kopiuje całe bajty zamiast pikseli.
 *
 * Trzymamy więc bufor w układzie Arduboya, bajt w bajt, a przekładamy go na
 * powierzchnię Hydry dopiero w `display()`. Koszt to jeden przebieg po 1 KB na
 * klatkę; cena za rozjechanie się z formatem duszków byłaby nieporównanie
 * wyższa, bo płaciłaby ją każda gra z osobna.
 *
 * ## Co zostało celowo inaczej
 *
 *  - `boot()` nie mruga diodą RGB ani nie czyta EEPROM-u przy starcie,
 *  - `flashlight()` nie zwiera zasilania, tylko zapala ekran,
 *  - `cpuLoad()` mierzy prawdziwe zajęcie klatki, nie stałą.
 *
 * Wszystkie te funkcje **istnieją** i dają sensowny wynik, bo gry je wołają.
 *
 * ## Wejście
 *
 * Na układzie przyciski wnosi projekt przez `setButtonSource()`. Na celu
 * natywnym runtime podpina klawiaturę sam — strzałki, A/S, Enter, Backspace.
 *
 * @see hydra/arduboy/Runtime.hpp — pętla gry i okno na celu natywnym
 */
#pragma once

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/gfx/Framebuffer.hpp"

namespace hydra {
namespace arduboy {

// ═══════════════════════════════════════════════════════════════════════════
//  Stałe sprzętowe oryginału
// ═══════════════════════════════════════════════════════════════════════════

constexpr i16 kWidth  = 128;
constexpr i16 kHeight = 64;

/** Rozmiar bufora obrazu w bajtach — 128 × 64 / 8. */
constexpr size_t kBufferBytes = static_cast<size_t>(kWidth) * kHeight / 8;

/** Barwy. `INVERT` odwraca piksel zamiast go ustawiać. */
constexpr u8 kBlack  = 0;
constexpr u8 kWhite  = 1;
constexpr u8 kInvert = 2;

/**
 * Maski przycisków — wartości oryginału.
 *
 * Nie są kolejne, bo na ATmega32u4 odpowiadały pozycjom nóżek w rejestrach
 * portów. Zachowujemy je, bo gry bywają zapisują stan przycisków w EEPROM-ie
 * albo składają maski ręcznie, a wtedy własna numeracja cicho zmieniłaby
 * znaczenie zapisanych danych.
 */
constexpr u8 kLeftButton  = 0x20;
constexpr u8 kRightButton = 0x40;
constexpr u8 kUpButton    = 0x80;
constexpr u8 kDownButton  = 0x10;
constexpr u8 kAButton     = 0x08;
constexpr u8 kBButton     = 0x04;

/** Wszystkie kierunki — częsty test „czy gracz w ogóle rusza". */
constexpr u8 kDirectionButtons = kLeftButton | kRightButton | kUpButton | kDownButton;

// ═══════════════════════════════════════════════════════════════════════════
//  Typy pomocnicze oryginału
// ═══════════════════════════════════════════════════════════════════════════

struct Point {
    i16 x = 0;
    i16 y = 0;
};

struct Rect {
    i16 x      = 0;
    i16 y      = 0;
    u8  width  = 0;
    u8  height = 0;
};

/** Czy punkt leży w prostokącie. */
bool collide(Point p, Rect r);
/** Czy prostokąty zachodzą na siebie. Styk krawędziami **nie** jest kolizją. */
bool collide(Rect a, Rect b);

/** Dane czcionki 5×7; pięć bajtów kolumnowych na znak, od 0x20. */
extern const u8 kFont5x7[96 * 5];

// ═══════════════════════════════════════════════════════════════════════════
//  Dźwięk
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Włącznik dźwięku — odpowiednik `Arduboy2Audio`.
 *
 * Sam nie generuje żadnego przebiegu; trzyma tylko decyzję gracza i udostępnia
 * ją grze przez `enabled()`. Gry pytają o to przed każdym efektem, więc bez
 * tej klasy nie skompilują się w ogóle.
 *
 * Zapis stanu (`saveOnOff()`) trafia do pamięci nieulotnej, jeśli projekt ją
 * podpiął — inaczej ustawienie żyje do wyłączenia zasilania.
 */
class Audio {
public:
    /**
     * Metody są **statyczne**, i to nie jest szczegół implementacji.
     *
     * Gry pisze się tak:
     *
     *     ArduboyTones sound(arduboy.audio.enabled);
     *
     * czyli przekazują `enabled` jako wskaźnik na funkcję `bool(*)()`. Na
     * metodzie niestatycznej takie wyrażenie się nie kompiluje — nie ma z czego
     * zrobić wskaźnika bez obiektu. Oryginał trzyma więc stan w składowej
     * statycznej i my musimy tak samo, bo inaczej ten jeden wiersz wywraca
     * budowę każdej gry z dźwiękiem.
     */
    static void on()     { enabled_ = true; }
    static void off()    { enabled_ = false; }
    static void toggle() { enabled_ = !enabled_; }
    static bool enabled() { return enabled_; }

    /** Utrwala stan. Bez pamięci nieulotnej — bez efektu, ale i bez błędu. */
    static void saveOnOff();
    /** Odczytuje utrwalony stan; brak zapisu oznacza „dźwięk włączony". */
    static void begin();

private:
    inline static bool enabled_ = true;
};

// ═══════════════════════════════════════════════════════════════════════════
//  Wypisywanie tekstu
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Minimalny odpowiednik arduinowego `Print`.
 *
 * Własny, a nie dziedziczony z `Arduino.h`, i to jest świadoma decyzja: gdyby
 * na układzie tekst szedł przez arduinowy `Print`, a na hoście przez atrapę,
 * to formatowanie liczb — a więc wygląd każdego licznika punktów — zależałoby
 * od celu budowy. Jedna implementacja znaczy jeden wygląd wszędzie.
 */
class Print {
public:
    virtual ~Print() = default;

    /** Jedyna metoda, którą musi dostarczyć klasa pochodna. */
    virtual size_t write(u8 c) = 0;

    size_t write(const char* s);
    size_t write(const u8* buffer, size_t size);

    // Przeciążenia opisane typami **wbudowanymi**, nie skrótami `i32`/`u32`.
    //
    // Powód jest przenośnościowy: `int32_t` bywa `int`, a bywa `long`,
    // zależnie od celu. Przy skrótach dwa przeciążenia zlewałyby się w jedno
    // na jednej architekturze, a na innej zostawiałyby lukę — i to samo
    // wywołanie `print(1234L)` raz się kompilowało, raz nie.
    size_t print(char c)          { return write(static_cast<u8>(c)); }
    size_t print(const char* s)   { return write(s); }

    size_t print(unsigned char v, u8 base = 10) { return printUnsigned(v, base); }
    size_t print(short v, u8 base = 10)         { return printSigned(v, base); }
    size_t print(unsigned short v, u8 base = 10){ return printUnsigned(v, base); }
    size_t print(int v, u8 base = 10)           { return printSigned(v, base); }
    size_t print(unsigned int v, u8 base = 10)  { return printUnsigned(v, base); }
    size_t print(long v, u8 base = 10)          { return printSigned(v, base); }
    size_t print(unsigned long v, u8 base = 10) { return printUnsigned(v, base); }
    size_t print(double v, u8 digits = 2);
    size_t print(float v, u8 digits = 2) { return print(static_cast<double>(v), digits); }

    size_t println();
    size_t println(char c)        { return print(c) + println(); }
    size_t println(const char* s) { return print(s) + println(); }

    size_t println(unsigned char v, u8 base = 10) { return print(v, base) + println(); }
    size_t println(short v, u8 base = 10)         { return print(v, base) + println(); }
    size_t println(unsigned short v, u8 base = 10){ return print(v, base) + println(); }
    size_t println(int v, u8 base = 10)           { return print(v, base) + println(); }
    size_t println(unsigned int v, u8 base = 10)  { return print(v, base) + println(); }
    size_t println(long v, u8 base = 10)          { return print(v, base) + println(); }
    size_t println(unsigned long v, u8 base = 10) { return print(v, base) + println(); }
    size_t println(double v, u8 digits = 2) { return print(v, digits) + println(); }
    size_t println(float v, u8 digits = 2)  { return print(v, digits) + println(); }

private:
    size_t printSigned(long value, u8 base);
    size_t printUnsigned(unsigned long value, u8 base);
};

// ═══════════════════════════════════════════════════════════════════════════
//  Rdzeń
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Rysowanie, przyciski i takt klatek — odpowiednik `Arduboy2Base`.
 *
 * Gry używające tekstu chcą klasy `Arduboy2` niżej; ta jest dla tych, które
 * rysują wyłącznie grafiką i nie chcą płacić za czcionkę.
 */
class Arduboy2Base {
public:
    /** Źródło stanu przycisków: maska `kLeftButton | …` bitów wciśniętych. */
    using ButtonSource = Delegate<u8()>;

    Arduboy2Base();

    // ── Cykl życia ─────────────────────────────────────────────────────────

    /**
     * Uruchamia ekran i wejście.
     *
     * W oryginale pokazuje też logo i obsługuje skróty systemowe. Tutaj robi
     * to samo, o ile projekt tego nie wyłączył — patrz `setBootFlags()`.
     */
    void begin();

    /** Sama inicjalizacja sprzętu, bez logo i skrótów systemowych. */
    void boot();

    /** Zapala cały ekran, dopóki trzymany jest przycisk góra. */
    void flashlight();

    /** Skróty systemowe: B + góra/dół steruje dźwiękiem. */
    void systemButtons();

    /** Ekran powitalny. */
    void bootLogo();
    void bootLogoSpritesSelfMasked() { bootLogo(); }
    void bootLogoText()              { bootLogo(); }
    void waitNoButtons();

    // ── Takt klatek ────────────────────────────────────────────────────────

    /**
     * Ustawia liczbę klatek na sekundę.
     *
     * Wartość 0 jest odrzucana — w oryginale dawała dzielenie przez zero
     * i zawieszenie, tutaj zostaje poprzednia liczba.
     */
    void setFrameRate(u8 rate);
    /** Czas klatki w milisekundach; alternatywa dla `setFrameRate()`. */
    void setFrameDuration(u8 durationMs);

    /**
     * Czy nadszedł czas na nową klatkę.
     *
     * Kręgosłup każdej gry na Arduboya:
     *
     *     if (!arduboy.nextFrame()) return;
     *
     * Zwraca `true` dokładnie raz na okres klatki. Gdy poprzednia klatka
     * przekroczyła budżet, **nie** nadrabia zaległości serią `true` pod rząd —
     * gra zwolniłaby, ale nie przeskoczyła, a to jest wybór między spadkiem
     * płynności a teleportacją obiektów.
     */
    bool nextFrame();

    /** Jak `nextFrame()`, lecz sygnalizuje przekroczenie budżetu klatki. */
    bool nextFrameDEV();

    /** Czy numer bieżącej klatki dzieli się przez `n` — do wolniejszych animacji. */
    bool everyXFrames(u8 n) const;

    /** Zajęcie budżetu klatki w procentach, mierzone naprawdę. */
    int cpuLoad() const { return cpuLoad_; }

    /** Oddaje procesor do końca klatki. */
    void idle();

    // ── Ekran ──────────────────────────────────────────────────────────────

    /** Wysyła bufor na ekran. */
    void display();
    /** Wysyła bufor i od razu go czyści — oszczędza jeden przebieg po pamięci. */
    void display(bool clearBuffer);

    /** Zeruje bufor. */
    void clear();
    /** Wypełnia bufor barwą. */
    void fillScreen(u8 color = kWhite);

    void drawPixel(i16 x, i16 y, u8 color = kWhite);
    bool getPixel(u8 x, u8 y) const;

    void drawFastVLine(i16 x, i16 y, u8 h, u8 color = kWhite);
    void drawFastHLine(i16 x, i16 y, u8 w, u8 color = kWhite);
    void drawLine(i16 x0, i16 y0, i16 x1, i16 y1, u8 color = kWhite);

    void drawRect(i16 x, i16 y, u8 w, u8 h, u8 color = kWhite);
    void fillRect(i16 x, i16 y, u8 w, u8 h, u8 color = kWhite);
    void drawRoundRect(i16 x, i16 y, u8 w, u8 h, u8 r, u8 color = kWhite);
    void fillRoundRect(i16 x, i16 y, u8 w, u8 h, u8 r, u8 color = kWhite);

    void drawCircle(i16 x0, i16 y0, u8 r, u8 color = kWhite);
    void fillCircle(i16 x0, i16 y0, u8 r, u8 color = kWhite);
    void drawCircleHelper(i16 x0, i16 y0, u8 r, u8 corners, u8 color = kWhite);
    void fillCircleHelper(i16 x0, i16 y0, u8 r, u8 sides, i16 delta, u8 color = kWhite);

    void drawTriangle(i16 x0, i16 y0, i16 x1, i16 y1, i16 x2, i16 y2, u8 color = kWhite);
    void fillTriangle(i16 x0, i16 y0, i16 x1, i16 y1, i16 x2, i16 y2, u8 color = kWhite);

    /** Bitmapa w układzie stronicowym — format `Sprites` i większości narzędzi. */
    void drawBitmap(i16 x, i16 y, const u8* bitmap, u8 w, u8 h, u8 color = kWhite);
    /** Bitmapa wierszami, bit na piksel — format Adafruit GFX. */
    void drawSlowXYBitmap(i16 x, i16 y, const u8* bitmap, u8 w, u8 h, u8 color = kWhite);
    /** Bitmapa spakowana algorytmem `Cabi`. */
    void drawCompressedBitmap(i16 sx, i16 sy, const u8* bitmap, u8 color = kWhite);

    // ── Ekran: tryby całościowe ────────────────────────────────────────────

    void invert(bool inverse);
    void allPixelsOn(bool on);
    void flipVertical(bool flipped);
    void flipHorizontal(bool flipped);
    void setFrameRateHint(u8) {}

    // ── Przyciski ──────────────────────────────────────────────────────────

    /** Podpina źródło przycisków. Na celu natywnym robi to runtime. */
    void setButtonSource(ButtonSource src) { buttons_ = src; }

    /** Surowa maska wciśniętych przycisków — stan chwilowy, bez zatrzasku. */
    u8 buttonsState() const;

    /** Czy **wszystkie** przyciski z maski są wciśnięte. */
    bool pressed(u8 buttons) const;
    /** Czy żaden z przycisków z maski nie jest wciśnięty. */
    bool notPressed(u8 buttons) const;
    /** Czy którykolwiek z maski jest wciśnięty. */
    bool anyPressed(u8 buttons) const;

    /**
     * Zatrzaskuje stan przycisków na potrzeby `justPressed()`.
     *
     * Musi być wołane raz na klatkę. Bez tego `justPressed()` zawsze zwraca
     * `false` — najczęstsza przyczyna „gra nie reaguje na przyciski".
     */
    void pollButtons();

    /** Czy przycisk został wciśnięty **w tej** klatce. Wymaga `pollButtons()`. */
    bool justPressed(u8 button) const;
    /** Czy przycisk został puszczony w tej klatce. */
    bool justReleased(u8 button) const;

    // ── Reszta sprzętu ─────────────────────────────────────────────────────

    /** Dioda RGB. Bez diody w projekcie — zapamiętuje barwę i tyle. */
    void setRGBled(u8 red, u8 green, u8 blue);
    void setRGBled(u8 color, u8 value);
    void digitalWriteRGB(u8 red, u8 green, u8 blue);
    void digitalWriteRGB(u8 color, u8 value);
    void freeRGBled() {}

    /** Ziarno generatora z szumu sprzętowego. */
    void initRandomSeed();
    u32  generateRandomSeed() const;

    /** Napięcie zasilania w miliwoltach; 0, gdy projekt nie mierzy. */
    u16 rawADC(u8) const { return 0; }

    // ── Dostęp do wnętrza ──────────────────────────────────────────────────

    /**
     * Bufor obrazu w układzie Arduboya: `sBuffer[x + (y / 8) * 128]`.
     *
     * Publiczny, bo gry pisane pod ATmega rysują tu wprost i to jest
     * legalny, udokumentowany sposób użycia oryginalnej biblioteki.
     */
    u8 sBuffer[kBufferBytes];

    /** Numer bieżącej klatki; przewija się co 256. */
    u8 frameCount = 0;

    Audio audio;

    // ── Podłączenie do wyświetlacza ────────────────────────────────────────

    /** Odświeżenie panelu; wołane przez `display()` po przełożeniu bufora. */
    using FlushFn = Delegate<Status()>;

    /**
     * Obraz w układzie **wierszowym** `PixelFormat::Mono1` — tym, którego
     * oczekuje `gfx`.
     *
     * Zwracany bufor jest przeznaczony do podpięcia pod powierzchnię Hydry:
     *
     *     display.begin(arduboy.monoBuffer(), cfg);
     *     arduboy.setFlush([&] { return display.framebuffer().flush(); });
     *
     * Dzięki temu `display()` nie kopiuje obrazu drugi raz — przekłada
     * `sBuffer` wprost do pamięci, z której czyta panel.
     */
    ByteSpan monoBuffer() { return ByteSpan{mono_, sizeof(mono_)}; }

    /** Ustawia funkcję odświeżającą panel. Bez niej `display()` tylko przelicza. */
    void setFlush(FlushFn fn) { flush_ = fn; }

protected:
    /** Przekłada `sBuffer` (stronicowy) na `mono_` (wierszowy) i odświeża. */
    void present();

    /** Obraz w układzie oczekiwanym przez `gfx`; patrz `monoBuffer()`. */
    u8           mono_[kBufferBytes];
    FlushFn      flush_{};
    ButtonSource buttons_{};

    u8  currentButtons_  = 0;
    u8  previousButtons_ = 0;

    u16 frameDurationMs_ = 1000 / 60;
    u32 nextFrameStart_  = 0;
    u32 frameBeganAt_    = 0;
    int cpuLoad_         = 0;
    bool frameInProgress_ = false;

    bool inverted_ = false;
    bool allOn_    = false;
    bool flipV_    = false;
    bool flipH_    = false;
    u8   rgb_[3]   = {0, 0, 0};
};

// ═══════════════════════════════════════════════════════════════════════════
//  Rdzeń z tekstem
// ═══════════════════════════════════════════════════════════════════════════

/**
 * `Arduboy2Base` powiększony o wypisywanie tekstu — to jego używa 95% gier.
 *
 * Kursor liczy się w pikselach, nie w znakach: `setCursor(0, 8)` to początek
 * drugiego wiersza. Znak zajmuje 6 pikseli w poziomie i 8 w pionie, razem
 * z odstępem — czyli 21 znaków na wiersz i 8 wierszy na ekran.
 */
class Arduboy2 : public Arduboy2Base, public Print {
public:
    Arduboy2() = default;

    size_t write(u8 c) override;
    using Print::write;

    /** Rysuje pojedynczy znak w podanym miejscu, bez ruszania kursora. */
    void drawChar(i16 x, i16 y, u8 c, u8 color, u8 bg, u8 size);

    void setCursor(i16 x, i16 y) { cursorX_ = x; cursorY_ = y; }
    void setCursorX(i16 x)       { cursorX_ = x; }
    void setCursorY(i16 y)       { cursorY_ = y; }
    i16  getCursorX() const      { return cursorX_; }
    i16  getCursorY() const      { return cursorY_; }

    void setTextColor(u8 color)      { textColor_ = color; }
    void setTextBackground(u8 bg)    { textBackground_ = bg; }
    u8   getTextColor() const        { return textColor_; }
    u8   getTextBackground() const   { return textBackground_; }

    /** Skala całkowita. 0 jest odrzucane — dałoby tekst o zerowym rozmiarze. */
    void setTextSize(u8 size);
    u8   getTextSize() const { return textSize_; }

    /** Czy zawijać wiersze na krawędzi ekranu. */
    void setTextWrap(bool w) { textWrap_ = w; }
    bool getTextWrap() const { return textWrap_; }

    /** Kursor na (0,0) i czyszczenie bufora. */
    void clear();

private:
    i16 cursorX_        = 0;
    i16 cursorY_        = 0;
    u8  textColor_      = kWhite;
    u8  textBackground_ = kBlack;
    u8  textSize_       = 1;
    bool textWrap_      = false;
};

// ═══════════════════════════════════════════════════════════════════════════
//  Duszki
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Rysowanie duszków — odpowiednik klasy `Sprites`.
 *
 * Metody są statyczne i operują na buforze **aktywnej** instancji Arduboya,
 * dokładnie jak w oryginale, gdzie `Sprites` sięgał po globalny `sBuffer`.
 * Aktywną instancję ustawia `Arduboy2Base::begin()`.
 *
 * Format danych: dwa bajty nagłówka (szerokość, wysokość), potem kolumny
 * stronicowo. Warianty z maską przeplatają bajt obrazu z bajtem maski.
 */
class Sprites {
public:
    /** Nadpisuje tło — najszybszy wariant, duszek jest prostokątem. */
    static void drawOverwrite(i16 x, i16 y, const u8* bitmap, u8 frame = 0);
    /** Gasi piksele tam, gdzie duszek ma zapalone. */
    static void drawErase(i16 x, i16 y, const u8* bitmap, u8 frame = 0);
    /** Zapala piksele duszka, tła nie rusza — duszek bez prostokątnego obrysu. */
    static void drawSelfMasked(i16 x, i16 y, const u8* bitmap, u8 frame = 0);
    /** Maska w osobnej tablicy. */
    static void drawExternalMask(i16 x, i16 y, const u8* bitmap, const u8* mask,
                                 u8 frame = 0, u8 maskFrame = 0);
    /** Maska przeplatana z obrazem — jedna tablica, dwa bajty na kolumnę. */
    static void drawPlusMask(i16 x, i16 y, const u8* bitmap, u8 frame = 0);

    /** Ustawia instancję, na której bufor rysują powyższe metody. */
    static void bind(Arduboy2Base* owner);
};

/** `SpritesB` w oryginale to ten sam interfejs, mniejszy kosztem szybkości. */
using SpritesB = Sprites;

}  // namespace arduboy
}  // namespace hydra

#endif  // HYDRA_ENABLE_ARDUBOY
