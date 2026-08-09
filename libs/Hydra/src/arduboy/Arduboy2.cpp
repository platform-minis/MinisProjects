/**
 * Rdzeń warstwy zgodności z Arduboy2.
 *
 * Prymitywy rysujące są przepisane, a nie skopiowane, ale **wynik** ma być
 * ten sam co do piksela: gry na Arduboya bywają zbudowane wokół tego, że
 * `drawCircle(10, 10, 5)` zapala dokładnie te, a nie inne punkty — kolizje
 * liczone na podstawie rysunku, wzory na ekranie tytułowym, animacje krok po
 * kroku. Dlatego wszędzie, gdzie oryginał używa algorytmu Bresenhama albo
 * midpointu, używamy tego samego, z tym samym zaokrąglaniem.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include "hydra/arduboy/Arduboy2.hpp"

#include <stdio.h>
#include <string.h>

#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace arduboy {

namespace {

/** Instancja, na której bufor rysują statyczne metody `Sprites`. */
Arduboy2Base* gActive = nullptr;

inline void swapI16(i16& a, i16& b) {
    const i16 t = a;
    a = b;
    b = t;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  Kolizje
// ═══════════════════════════════════════════════════════════════════════════

bool collide(Point p, Rect r) {
    return p.x >= r.x && p.x < r.x + r.width &&
           p.y >= r.y && p.y < r.y + r.height;
}

bool collide(Rect a, Rect b) {
    // Styk krawędziami nie jest kolizją: obiekty stojące obok siebie na
    // siatce mają wtedy „wolne" sąsiedztwo, a to jest założenie, na którym
    // opiera się ruch po kratce w połowie gier platformowych.
    return !(a.x            >= b.x + b.width  ||
             a.x + a.width  <= b.x            ||
             a.y            >= b.y + b.height ||
             a.y + a.height <= b.y);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Audio
// ═══════════════════════════════════════════════════════════════════════════

void Audio::saveOnOff() {
    // Bez pamięci nieulotnej ustawienie żyje do wyłączenia zasilania.
    // Świadomie nie jest to błąd: gra woła to po każdej zmianie w menu
    // i przerwanie jej działania z powodu braku EEPROM-u byłoby gorsze
    // niż ciche zapomnienie ustawienia.
}

void Audio::begin() {
    enabled_ = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Print
// ═══════════════════════════════════════════════════════════════════════════

size_t Print::write(const char* s) {
    if (s == nullptr) return 0;
    size_t n = 0;
    while (*s != '\0') n += write(static_cast<u8>(*s++));
    return n;
}

size_t Print::write(const u8* buffer, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; ++i) n += write(buffer[i]);
    return n;
}

size_t Print::printUnsigned(unsigned long value, u8 base) {
    if (base < 2) base = 10;

    // 64 bity w systemie dwójkowym to 64 znaki plus zero kończące.
    char buf[65];
    char* p = buf + sizeof(buf) - 1;
    *p = '\0';

    do {
        const unsigned long digit = value % base;
        *--p = static_cast<char>(digit < 10 ? '0' + digit : 'A' + digit - 10);
        value /= base;
    } while (value != 0);

    return write(p);
}

size_t Print::printSigned(long value, u8 base) {
    if (base == 10 && value < 0) {
        // Wartość skrajnie ujemna nie ma odpowiednika dodatniego w typie ze
        // znakiem, więc negujemy dopiero po przejściu na typ bez znaku.
        const size_t n = write(static_cast<u8>('-'));
        return n + printUnsigned(0UL - static_cast<unsigned long>(value), base);
    }
    return printUnsigned(static_cast<unsigned long>(value), base);
}

size_t Print::print(double value, u8 digits) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(digits), value);
    return write(buf);
}

size_t Print::println() {
    return write(static_cast<u8>('\n'));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Arduboy2Base — cykl życia
// ═══════════════════════════════════════════════════════════════════════════

Arduboy2Base* activeInstance() {
    return gActive;
}

Arduboy2Base::Arduboy2Base() {
    memset(sBuffer, 0, sizeof(sBuffer));
    memset(mono_, 0, sizeof(mono_));

    // Rejestracja już w konstruktorze, nie w `begin()`.
    //
    // Gra deklaruje `Arduboy2 arduboy;` globalnie, a runtime musi podłączyć
    // ekran **przed** wywołaniem `setup()` — bo `setup()` zwykle od razu woła
    // `begin()` i rysuje. Gdyby instancja zgłaszała się dopiero w `begin()`,
    // pierwsza klatka poszłaby w próżnię.
    //
    // Przy kilku instancjach wygrywa ostatnia utworzona; oryginał miał ten sam
    // problem i to samo rozwiązanie, bo `Sprites` sięgał po globalny bufor.
    gActive = this;
}

void Arduboy2Base::begin() {
    boot();
    bootLogo();
    audio.begin();
    systemButtons();
}

void Arduboy2Base::boot() {
    gActive = this;
    clear();
    nextFrameStart_ = static_cast<u32>(rtos::nowMs());
    initRandomSeed();
}

void Arduboy2Base::bootLogo() {
    // Oryginał przesuwa logo z góry ekranu przez około sekundę. Robimy to
    // samo pustym ekranem: gra po `begin()` ma prawo założyć, że bufor jest
    // czysty, a nie że został po logo.
    clear();
    display();
}

void Arduboy2Base::waitNoButtons() {
    // Zapobiega „przeklikaniu" ekranu tytułowego przyciskiem trzymanym
    // jeszcze z poprzedniego ekranu.
    while (buttonsState() != 0) rtos::delayMs(5);
}

void Arduboy2Base::flashlight() {
    if (!pressed(kUpButton)) return;

    allPixelsOn(true);
    while (buttonsState() != 0) rtos::delayMs(10);
    allPixelsOn(false);
}

void Arduboy2Base::systemButtons() {
    while (pressed(kBButton)) {
        if (pressed(kUpButton))   audio.on();
        if (pressed(kDownButton)) audio.off();
        rtos::delayMs(10);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Takt klatek
// ═══════════════════════════════════════════════════════════════════════════

void Arduboy2Base::setFrameRate(u8 rate) {
    // Zero dałoby w oryginale dzielenie przez zero. Zostaje poprzednia wartość:
    // gra z popsutą liczbą klatek ma chodzić wolno, a nie stanąć.
    if (rate == 0) return;
    frameDurationMs_ = static_cast<u16>(1000 / rate);
    if (frameDurationMs_ == 0) frameDurationMs_ = 1;
}

void Arduboy2Base::setFrameDuration(u8 durationMs) {
    if (durationMs == 0) return;
    frameDurationMs_ = durationMs;
}

bool Arduboy2Base::nextFrame() {
    const u32 now = static_cast<u32>(rtos::nowMs());

    // Odejmowanie, nie porównanie — licznik milisekund przewija się co 49 dni
    // i `now < nextFrameStart_` zatrzymałoby wtedy grę na całą dobę.
    if (static_cast<i32>(now - nextFrameStart_) < 0) return false;

    if (frameInProgress_) {
        const u32 spent = now - frameBeganAt_;
        cpuLoad_ = frameDurationMs_ > 0
                       ? static_cast<int>(spent * 100 / frameDurationMs_)
                       : 0;
    }

    // Kolejny termin liczony od poprzedniego terminu, nie od teraz: inaczej
    // każde opóźnienie klatki dodawałoby się do następnych i gra stopniowo
    // zwalniałaby bez powodu.
    nextFrameStart_ += frameDurationMs_;

    // Gdy zaległość urosła ponad całą klatkę — bo panel długo się odświeżał
    // albo procesor zajął się czymś innym — zaczynamy odliczać od nowa.
    // Bez tego wracalibyśmy tu serią `true` pod rząd, „nadrabiając" czas,
    // a gracz zobaczyłby, jak obiekty przeskakują kilka kroków naraz.
    if (static_cast<i32>(now - nextFrameStart_) > 0) nextFrameStart_ = now + frameDurationMs_;

    ++frameCount;
    frameBeganAt_    = now;
    frameInProgress_ = true;
    return true;
}

bool Arduboy2Base::nextFrameDEV() {
    const bool go = nextFrame();
    // Oryginał zapala diodę czerwoną przy przekroczeniu budżetu. Bez diody
    // zostawiamy sam pomiar — widać go w `cpuLoad()`.
    return go;
}

bool Arduboy2Base::everyXFrames(u8 n) const {
    if (n == 0) return false;
    return frameCount % n == 0;
}

void Arduboy2Base::idle() {
    const u32 now = static_cast<u32>(rtos::nowMs());
    const i32 left = static_cast<i32>(nextFrameStart_ - now);
    if (left > 0) rtos::delayMs(static_cast<u32>(left));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Ekran
// ═══════════════════════════════════════════════════════════════════════════

void Arduboy2Base::clear() {
    memset(sBuffer, 0, sizeof(sBuffer));
}

void Arduboy2Base::fillScreen(u8 color) {
    memset(sBuffer, color == kBlack ? 0x00 : 0xFF, sizeof(sBuffer));
}

void Arduboy2Base::display() {
    present();
}

void Arduboy2Base::display(bool clearBuffer) {
    present();
    if (clearBuffer) clear();
}

/**
 * Przekłada bufor stronicowy na wierszowy.
 *
 * Pętla idzie po stronach, nie po pikselach: dla każdej z ośmiu stron i każdej
 * z ośmiu linii w stronie zbieramy osiem sąsiednich pikseli w poziomie w jeden
 * bajt wyjściowy. To 1024 iteracje wewnętrzne na klatkę zamiast 8192 testów
 * pojedynczych pikseli — różnica widoczna na ATmega, niewidoczna na ESP32,
 * ale kosztująca tyle samo linijek.
 */
void Arduboy2Base::present() {
    constexpr u32 stride = kWidth / 8;   // 16 bajtów na wiersz

    for (u8 page = 0; page < kHeight / 8; ++page) {
        const u8* src = sBuffer + static_cast<size_t>(page) * kWidth;

        for (u8 bit = 0; bit < 8; ++bit) {
            const u8 mask = static_cast<u8>(1u << bit);
            u8* dst = mono_ + (static_cast<size_t>(page) * 8 + bit) * stride;

            for (u32 byte = 0; byte < stride; ++byte) {
                const u8* column = src + byte * 8;
                u8 out = 0;
                // Bit najstarszy to lewa kolumna — układ Mono1 w gfx.
                if (column[0] & mask) out |= 0x80;
                if (column[1] & mask) out |= 0x40;
                if (column[2] & mask) out |= 0x20;
                if (column[3] & mask) out |= 0x10;
                if (column[4] & mask) out |= 0x08;
                if (column[5] & mask) out |= 0x04;
                if (column[6] & mask) out |= 0x02;
                if (column[7] & mask) out |= 0x01;
                dst[byte] = out;
            }
        }
    }

    if (inverted_) {
        for (size_t i = 0; i < sizeof(mono_); ++i) mono_[i] = static_cast<u8>(~mono_[i]);
    }
    if (allOn_) {
        memset(mono_, 0xFF, sizeof(mono_));
    }

    if (flush_) (void)flush_();
}

void Arduboy2Base::drawPixel(i16 x, i16 y, u8 color) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;

    const size_t index = static_cast<size_t>(x) +
                         (static_cast<size_t>(y) / 8) * kWidth;
    const u8 mask = static_cast<u8>(1u << (y & 7));

    switch (color) {
        case kBlack:  sBuffer[index] = static_cast<u8>(sBuffer[index] & ~mask); break;
        case kInvert: sBuffer[index] = static_cast<u8>(sBuffer[index] ^ mask);  break;
        default:      sBuffer[index] = static_cast<u8>(sBuffer[index] | mask);  break;
    }
}

bool Arduboy2Base::getPixel(u8 x, u8 y) const {
    if (x >= kWidth || y >= kHeight) return false;
    const size_t index = static_cast<size_t>(x) + (static_cast<size_t>(y) / 8) * kWidth;
    return (sBuffer[index] & (1u << (y & 7))) != 0;
}

void Arduboy2Base::drawFastVLine(i16 x, i16 y, u8 h, u8 color) {
    const i16 end = static_cast<i16>(y + h);
    for (i16 py = y; py < end; ++py) drawPixel(x, py, color);
}

void Arduboy2Base::drawFastHLine(i16 x, i16 y, u8 w, u8 color) {
    const i16 end = static_cast<i16>(x + w);
    for (i16 px = x; px < end; ++px) drawPixel(px, y, color);
}

void Arduboy2Base::drawLine(i16 x0, i16 y0, i16 x1, i16 y1, u8 color) {
    // Bresenham w wersji z zamianą osi — ta sama, którą ma oryginał,
    // więc linie ukośne mają identyczny „schodek".
    const bool steep = (y1 > y0 ? y1 - y0 : y0 - y1) > (x1 > x0 ? x1 - x0 : x0 - x1);
    if (steep) {
        swapI16(x0, y0);
        swapI16(x1, y1);
    }
    if (x0 > x1) {
        swapI16(x0, x1);
        swapI16(y0, y1);
    }

    const i16 dx = static_cast<i16>(x1 - x0);
    const i16 dy = static_cast<i16>(y1 > y0 ? y1 - y0 : y0 - y1);
    const i16 ystep = y0 < y1 ? 1 : -1;

    i16 err = static_cast<i16>(dx / 2);
    i16 y = y0;

    for (i16 x = x0; x <= x1; ++x) {
        if (steep) drawPixel(y, x, color);
        else       drawPixel(x, y, color);

        err = static_cast<i16>(err - dy);
        if (err < 0) {
            y = static_cast<i16>(y + ystep);
            err = static_cast<i16>(err + dx);
        }
    }
}

void Arduboy2Base::drawRect(i16 x, i16 y, u8 w, u8 h, u8 color) {
    if (w == 0 || h == 0) return;
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, static_cast<i16>(y + h - 1), w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(static_cast<i16>(x + w - 1), y, h, color);
}

void Arduboy2Base::fillRect(i16 x, i16 y, u8 w, u8 h, u8 color) {
    for (u8 i = 0; i < w; ++i) drawFastVLine(static_cast<i16>(x + i), y, h, color);
}

void Arduboy2Base::drawCircle(i16 x0, i16 y0, u8 r, u8 color) {
    // Midpoint — jak w oryginale.
    i16 f     = static_cast<i16>(1 - r);
    i16 ddF_x = 1;
    i16 ddF_y = static_cast<i16>(-2 * r);
    i16 x     = 0;
    i16 y     = r;

    drawPixel(x0, static_cast<i16>(y0 + r), color);
    drawPixel(x0, static_cast<i16>(y0 - r), color);
    drawPixel(static_cast<i16>(x0 + r), y0, color);
    drawPixel(static_cast<i16>(x0 - r), y0, color);

    while (x < y) {
        if (f >= 0) {
            --y;
            ddF_y = static_cast<i16>(ddF_y + 2);
            f = static_cast<i16>(f + ddF_y);
        }
        ++x;
        ddF_x = static_cast<i16>(ddF_x + 2);
        f = static_cast<i16>(f + ddF_x);

        drawPixel(static_cast<i16>(x0 + x), static_cast<i16>(y0 + y), color);
        drawPixel(static_cast<i16>(x0 - x), static_cast<i16>(y0 + y), color);
        drawPixel(static_cast<i16>(x0 + x), static_cast<i16>(y0 - y), color);
        drawPixel(static_cast<i16>(x0 - x), static_cast<i16>(y0 - y), color);
        drawPixel(static_cast<i16>(x0 + y), static_cast<i16>(y0 + x), color);
        drawPixel(static_cast<i16>(x0 - y), static_cast<i16>(y0 + x), color);
        drawPixel(static_cast<i16>(x0 + y), static_cast<i16>(y0 - x), color);
        drawPixel(static_cast<i16>(x0 - y), static_cast<i16>(y0 - x), color);
    }
}

void Arduboy2Base::drawCircleHelper(i16 x0, i16 y0, u8 r, u8 corners, u8 color) {
    i16 f     = static_cast<i16>(1 - r);
    i16 ddF_x = 1;
    i16 ddF_y = static_cast<i16>(-2 * r);
    i16 x     = 0;
    i16 y     = r;

    while (x < y) {
        if (f >= 0) {
            --y;
            ddF_y = static_cast<i16>(ddF_y + 2);
            f = static_cast<i16>(f + ddF_y);
        }
        ++x;
        ddF_x = static_cast<i16>(ddF_x + 2);
        f = static_cast<i16>(f + ddF_x);

        if (corners & 0x4) {   // prawy dolny
            drawPixel(static_cast<i16>(x0 + x), static_cast<i16>(y0 + y), color);
            drawPixel(static_cast<i16>(x0 + y), static_cast<i16>(y0 + x), color);
        }
        if (corners & 0x2) {   // prawy górny
            drawPixel(static_cast<i16>(x0 + x), static_cast<i16>(y0 - y), color);
            drawPixel(static_cast<i16>(x0 + y), static_cast<i16>(y0 - x), color);
        }
        if (corners & 0x8) {   // lewy dolny
            drawPixel(static_cast<i16>(x0 - y), static_cast<i16>(y0 + x), color);
            drawPixel(static_cast<i16>(x0 - x), static_cast<i16>(y0 + y), color);
        }
        if (corners & 0x1) {   // lewy górny
            drawPixel(static_cast<i16>(x0 - y), static_cast<i16>(y0 - x), color);
            drawPixel(static_cast<i16>(x0 - x), static_cast<i16>(y0 - y), color);
        }
    }
}

void Arduboy2Base::fillCircleHelper(i16 x0, i16 y0, u8 r, u8 sides, i16 delta, u8 color) {
    i16 f     = static_cast<i16>(1 - r);
    i16 ddF_x = 1;
    i16 ddF_y = static_cast<i16>(-2 * r);
    i16 x     = 0;
    i16 y     = r;

    while (x < y) {
        if (f >= 0) {
            --y;
            ddF_y = static_cast<i16>(ddF_y + 2);
            f = static_cast<i16>(f + ddF_y);
        }
        ++x;
        ddF_x = static_cast<i16>(ddF_x + 2);
        f = static_cast<i16>(f + ddF_x);

        if (sides & 0x1) {   // prawa połowa
            drawFastVLine(static_cast<i16>(x0 + x), static_cast<i16>(y0 - y),
                          static_cast<u8>(2 * y + 1 + delta), color);
            drawFastVLine(static_cast<i16>(x0 + y), static_cast<i16>(y0 - x),
                          static_cast<u8>(2 * x + 1 + delta), color);
        }
        if (sides & 0x2) {   // lewa połowa
            drawFastVLine(static_cast<i16>(x0 - x), static_cast<i16>(y0 - y),
                          static_cast<u8>(2 * y + 1 + delta), color);
            drawFastVLine(static_cast<i16>(x0 - y), static_cast<i16>(y0 - x),
                          static_cast<u8>(2 * x + 1 + delta), color);
        }
    }
}

void Arduboy2Base::fillCircle(i16 x0, i16 y0, u8 r, u8 color) {
    drawFastVLine(x0, static_cast<i16>(y0 - r), static_cast<u8>(2 * r + 1), color);
    fillCircleHelper(x0, y0, r, 3, 0, color);
}

void Arduboy2Base::drawRoundRect(i16 x, i16 y, u8 w, u8 h, u8 r, u8 color) {
    drawFastHLine(static_cast<i16>(x + r), y, static_cast<u8>(w - 2 * r), color);
    drawFastHLine(static_cast<i16>(x + r), static_cast<i16>(y + h - 1),
                  static_cast<u8>(w - 2 * r), color);
    drawFastVLine(x, static_cast<i16>(y + r), static_cast<u8>(h - 2 * r), color);
    drawFastVLine(static_cast<i16>(x + w - 1), static_cast<i16>(y + r),
                  static_cast<u8>(h - 2 * r), color);

    drawCircleHelper(static_cast<i16>(x + r), static_cast<i16>(y + r), r, 1, color);
    drawCircleHelper(static_cast<i16>(x + w - r - 1), static_cast<i16>(y + r), r, 2, color);
    drawCircleHelper(static_cast<i16>(x + w - r - 1), static_cast<i16>(y + h - r - 1), r, 4, color);
    drawCircleHelper(static_cast<i16>(x + r), static_cast<i16>(y + h - r - 1), r, 8, color);
}

void Arduboy2Base::fillRoundRect(i16 x, i16 y, u8 w, u8 h, u8 r, u8 color) {
    fillRect(static_cast<i16>(x + r), y, static_cast<u8>(w - 2 * r), h, color);
    fillCircleHelper(static_cast<i16>(x + w - r - 1), static_cast<i16>(y + r), r, 1,
                     static_cast<i16>(h - 2 * r - 1), color);
    fillCircleHelper(static_cast<i16>(x + r), static_cast<i16>(y + r), r, 2,
                     static_cast<i16>(h - 2 * r - 1), color);
}

void Arduboy2Base::drawTriangle(i16 x0, i16 y0, i16 x1, i16 y1, i16 x2, i16 y2, u8 color) {
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

void Arduboy2Base::fillTriangle(i16 x0, i16 y0, i16 x1, i16 y1, i16 x2, i16 y2, u8 color) {
    // Sortowanie po y, potem dwa przebiegi: górna i dolna połowa.
    if (y0 > y1) { swapI16(y0, y1); swapI16(x0, x1); }
    if (y1 > y2) { swapI16(y2, y1); swapI16(x2, x1); }
    if (y0 > y1) { swapI16(y0, y1); swapI16(x0, x1); }

    if (y0 == y2) {
        // Trójkąt zdegenerowany do odcinka — bez tego przypadku niżej
        // dzielilibyśmy przez zero.
        i16 a = x0;
        i16 b = x0;
        if (x1 < a) a = x1; else if (x1 > b) b = x1;
        if (x2 < a) a = x2; else if (x2 > b) b = x2;
        drawFastHLine(a, y0, static_cast<u8>(b - a + 1), color);
        return;
    }

    const i16 dx01 = static_cast<i16>(x1 - x0), dy01 = static_cast<i16>(y1 - y0);
    const i16 dx02 = static_cast<i16>(x2 - x0), dy02 = static_cast<i16>(y2 - y0);
    const i16 dx12 = static_cast<i16>(x2 - x1), dy12 = static_cast<i16>(y2 - y1);

    i32 sa = 0;
    i32 sb = 0;

    // Gdy dolna krawędź jest pozioma, ostatni wiersz górnej połowy rysujemy
    // tutaj; inaczej byłby narysowany dwa razy.
    const i16 last = (y1 == y2) ? y1 : static_cast<i16>(y1 - 1);

    i16 y = y0;
    for (; y <= last; ++y) {
        i16 a = static_cast<i16>(x0 + sa / dy01);
        i16 b = static_cast<i16>(x0 + sb / dy02);
        sa += dx01;
        sb += dx02;
        if (a > b) swapI16(a, b);
        drawFastHLine(a, y, static_cast<u8>(b - a + 1), color);
    }

    sa = static_cast<i32>(dx12) * (y - y1);
    sb = static_cast<i32>(dx02) * (y - y0);
    for (; y <= y2; ++y) {
        i16 a = static_cast<i16>(x1 + sa / dy12);
        i16 b = static_cast<i16>(x0 + sb / dy02);
        sa += dx12;
        sb += dx02;
        if (a > b) swapI16(a, b);
        drawFastHLine(a, y, static_cast<u8>(b - a + 1), color);
    }
}

void Arduboy2Base::drawBitmap(i16 x, i16 y, const u8* bitmap, u8 w, u8 h, u8 color) {
    if (bitmap == nullptr) return;
    if (x + w < 0 || x >= kWidth || y + h < 0 || y >= kHeight) return;

    const u8 rows = static_cast<u8>((h + 7) / 8);
    for (u8 page = 0; page < rows; ++page) {
        for (u8 col = 0; col < w; ++col) {
            const u8 bits = bitmap[static_cast<size_t>(page) * w + col];
            if (bits == 0 && color != kBlack) continue;

            for (u8 bit = 0; bit < 8; ++bit) {
                const i16 py = static_cast<i16>(y + page * 8 + bit);
                if (py >= y + h) break;
                if (bits & (1u << bit)) drawPixel(static_cast<i16>(x + col), py, color);
            }
        }
    }
}

void Arduboy2Base::drawSlowXYBitmap(i16 x, i16 y, const u8* bitmap, u8 w, u8 h, u8 color) {
    if (bitmap == nullptr) return;

    const u8 stride = static_cast<u8>((w + 7) / 8);
    for (u8 row = 0; row < h; ++row) {
        for (u8 col = 0; col < w; ++col) {
            const u8 bits = bitmap[static_cast<size_t>(row) * stride + col / 8];
            if (bits & (0x80 >> (col & 7))) {
                drawPixel(static_cast<i16>(x + col), static_cast<i16>(y + row), color);
            }
        }
    }
}

/**
 * Bitmapa spakowana algorytmem `Cabi`.
 *
 * Format: 3 bity szerokości-1 i 3 bity wysokości-1 zapisane jako dwie liczby
 * siedmiobitowe, potem strumień par (barwa, długość) kodowanych zmienną
 * liczbą bitów. Rozpakowujemy w locie, bez bufora pośredniego — bitmapa
 * mieszcząca się w pamięci programu nie musi się mieścić w RAM-ie.
 */
void Arduboy2Base::drawCompressedBitmap(i16 sx, i16 sy, const u8* bitmap, u8 color) {
    if (bitmap == nullptr) return;

    size_t byteIndex = 0;
    u8     bitIndex  = 0;

    auto readBit = [&]() -> u8 {
        const u8 bit = static_cast<u8>((bitmap[byteIndex] >> bitIndex) & 1);
        if (++bitIndex > 7) {
            bitIndex = 0;
            ++byteIndex;
        }
        return bit;
    };

    auto readBits = [&](u8 count) -> u16 {
        u16 value = 0;
        for (u8 i = 0; i < count; ++i) value = static_cast<u16>(value | (readBit() << i));
        return value;
    };

    const u8 width  = static_cast<u8>(readBits(8) + 1);
    const u8 height = static_cast<u8>(readBits(8) + 1);

    u8 spanColor = readBit();   // barwa pierwszego ciągu
    i16 x = 0;
    i16 y = 0;

    while (y < height) {
        // Długość ciągu: jedynki prefiksu mówią, ile bitów ma sama liczba.
        u8 lengthBits = 0;
        while (readBit() != 0) ++lengthBits;

        u16 length = 1;
        if (lengthBits > 0) {
            length = static_cast<u16>((1u << lengthBits) + readBits(lengthBits));
        }

        for (u16 i = 0; i < length && y < height; ++i) {
            if (spanColor != 0) {
                drawPixel(static_cast<i16>(sx + x), static_cast<i16>(sy + y), color);
            }
            if (++x >= width) {
                x = 0;
                ++y;
            }
        }

        spanColor = static_cast<u8>(spanColor ^ 1);
    }
}

void Arduboy2Base::invert(bool inverse)        { inverted_ = inverse; }
void Arduboy2Base::allPixelsOn(bool on)        { allOn_    = on; }
void Arduboy2Base::flipVertical(bool flipped)  { flipV_    = flipped; }
void Arduboy2Base::flipHorizontal(bool flipped){ flipH_    = flipped; }

// ═══════════════════════════════════════════════════════════════════════════
//  Przyciski
// ═══════════════════════════════════════════════════════════════════════════

u8 Arduboy2Base::buttonsState() const {
    return buttons_ ? buttons_() : 0;
}

bool Arduboy2Base::pressed(u8 buttons) const {
    return (buttonsState() & buttons) == buttons;
}

bool Arduboy2Base::notPressed(u8 buttons) const {
    return (buttonsState() & buttons) == 0;
}

bool Arduboy2Base::anyPressed(u8 buttons) const {
    return (buttonsState() & buttons) != 0;
}

void Arduboy2Base::pollButtons() {
    previousButtons_ = currentButtons_;
    currentButtons_  = buttonsState();
}

bool Arduboy2Base::justPressed(u8 button) const {
    return (previousButtons_ & button) == 0 && (currentButtons_ & button) != 0;
}

bool Arduboy2Base::justReleased(u8 button) const {
    return (previousButtons_ & button) != 0 && (currentButtons_ & button) == 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Reszta
// ═══════════════════════════════════════════════════════════════════════════

void Arduboy2Base::setRGBled(u8 red, u8 green, u8 blue) {
    rgb_[0] = red;
    rgb_[1] = green;
    rgb_[2] = blue;
}

void Arduboy2Base::setRGBled(u8 color, u8 value) {
    if (color < 3) rgb_[color] = value;
}

void Arduboy2Base::digitalWriteRGB(u8 red, u8 green, u8 blue) {
    setRGBled(static_cast<u8>(red ? 255 : 0),
              static_cast<u8>(green ? 255 : 0),
              static_cast<u8>(blue ? 255 : 0));
}

void Arduboy2Base::digitalWriteRGB(u8 color, u8 value) {
    setRGBled(color, static_cast<u8>(value ? 255 : 0));
}

u32 Arduboy2Base::generateRandomSeed() const {
    // Oryginał czyta szum z niepodłączonego wejścia analogowego. Tu bierzemy
    // czas do pierwszego wywołania — na urządzeniu włączanym przyciskiem jest
    // to wielkość na tyle nieprzewidywalna, na ile trzeba do losowania wroga.
    return static_cast<u32>(rtos::nowMs()) * 2654435761u;
}

void Arduboy2Base::initRandomSeed() {
    // Ziarno ustawia gra przez `randomSeed()`; tu tylko je wyliczamy.
}

// ═══════════════════════════════════════════════════════════════════════════
//  Arduboy2 — tekst
// ═══════════════════════════════════════════════════════════════════════════

void Arduboy2::clear() {
    Arduboy2Base::clear();
    cursorX_ = 0;
    cursorY_ = 0;
}

void Arduboy2::setTextSize(u8 size) {
    // Zero dałoby znaki o zerowej wielkości i kursor stojący w miejscu —
    // czyli pętlę wypisującą bez końca w to samo miejsce.
    textSize_ = size > 0 ? size : 1;
}

void Arduboy2::drawChar(i16 x, i16 y, u8 c, u8 color, u8 bg, u8 size) {
    if (x >= kWidth || y >= kHeight) return;
    if (x + 5 * size < 0 || y + 8 * size < 0) return;

    // Znaki spoza zakresu czcionki rysujemy jako spację — oryginał sięgnąłby
    // poza tablicę i wypisał śmieci z sąsiedniej pamięci.
    const u8 index = (c >= 0x20 && c < 0x80) ? static_cast<u8>(c - 0x20) : 0;
    const u8* glyph = kFont5x7 + static_cast<size_t>(index) * 5;

    for (u8 col = 0; col < 6; ++col) {
        // Szósta kolumna to odstęp — zawsze pusta.
        const u8 bits = (col < 5) ? glyph[col] : 0;

        for (u8 row = 0; row < 8; ++row) {
            const bool on = (bits & (1u << row)) != 0;
            if (!on && bg == color) continue;   // przezroczyste tło

            const u8 pixelColor = on ? color : bg;
            if (size == 1) {
                drawPixel(static_cast<i16>(x + col), static_cast<i16>(y + row), pixelColor);
            } else {
                fillRect(static_cast<i16>(x + col * size), static_cast<i16>(y + row * size),
                         size, size, pixelColor);
            }
        }
    }
}

size_t Arduboy2::write(u8 c) {
    if (c == '\n') {
        cursorX_ = 0;
        cursorY_ = static_cast<i16>(cursorY_ + textSize_ * 8);
        return 1;
    }
    if (c == '\r') {
        return 1;   // pomijane, jak w oryginale
    }

    drawChar(cursorX_, cursorY_, c, textColor_, textBackground_, textSize_);
    cursorX_ = static_cast<i16>(cursorX_ + textSize_ * 6);

    if (textWrap_ && cursorX_ > kWidth - textSize_ * 6) {
        cursorX_ = 0;
        cursorY_ = static_cast<i16>(cursorY_ + textSize_ * 8);
    }
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Sprites
// ═══════════════════════════════════════════════════════════════════════════

void Sprites::bind(Arduboy2Base* owner) {
    gActive = owner;
}

namespace {

/** Wspólny rdzeń wszystkich wariantów rysowania duszka. */
enum class SpriteMode { Overwrite, Erase, SelfMasked, ExternalMask, PlusMask };

void drawSprite(i16 x, i16 y, const u8* bitmap, const u8* mask,
                u8 frame, u8 maskFrame, SpriteMode mode) {
    if (gActive == nullptr || bitmap == nullptr) return;

    const u8 width  = bitmap[0];
    const u8 height = bitmap[1];
    if (width == 0 || height == 0) return;

    const u8 pages = static_cast<u8>((height + 7) / 8);
    const size_t frameBytes = static_cast<size_t>(width) * pages;

    // Klatka animacji to przesunięcie w tej samej tablicy. Przy `PlusMask`
    // każda kolumna zajmuje dwa bajty, więc i klatka jest dwa razy dłuższa.
    const size_t stride = (mode == SpriteMode::PlusMask) ? frameBytes * 2 : frameBytes;
    const u8* image = bitmap + 2 + static_cast<size_t>(frame) * stride;
    const u8* maskData = nullptr;
    if (mode == SpriteMode::ExternalMask && mask != nullptr) {
        maskData = mask + 2 + static_cast<size_t>(maskFrame) * frameBytes;
    }

    for (u8 page = 0; page < pages; ++page) {
        for (u8 col = 0; col < width; ++col) {
            u8 bits = 0;
            u8 maskBits = 0xFF;

            if (mode == SpriteMode::PlusMask) {
                const size_t at = (static_cast<size_t>(page) * width + col) * 2;
                bits     = image[at];
                maskBits = image[at + 1];
            } else {
                bits = image[static_cast<size_t>(page) * width + col];
                if (maskData != nullptr) {
                    maskBits = maskData[static_cast<size_t>(page) * width + col];
                }
            }

            for (u8 bit = 0; bit < 8; ++bit) {
                const i16 py = static_cast<i16>(y + page * 8 + bit);
                if (py >= y + height) break;

                const i16 px = static_cast<i16>(x + col);
                const bool set  = (bits & (1u << bit)) != 0;
                const bool keep = (maskBits & (1u << bit)) != 0;

                switch (mode) {
                    case SpriteMode::Overwrite:
                        // Prostokąt duszka nadpisuje tło w całości.
                        gActive->drawPixel(px, py, set ? kWhite : kBlack);
                        break;
                    case SpriteMode::Erase:
                        if (set) gActive->drawPixel(px, py, kBlack);
                        break;
                    case SpriteMode::SelfMasked:
                        if (set) gActive->drawPixel(px, py, kWhite);
                        break;
                    case SpriteMode::ExternalMask:
                    case SpriteMode::PlusMask:
                        // Maska decyduje, gdzie w ogóle dotykamy tła.
                        if (keep) gActive->drawPixel(px, py, set ? kWhite : kBlack);
                        break;
                }
            }
        }
    }
}

}  // namespace

void Sprites::drawOverwrite(i16 x, i16 y, const u8* bitmap, u8 frame) {
    drawSprite(x, y, bitmap, nullptr, frame, 0, SpriteMode::Overwrite);
}

void Sprites::drawErase(i16 x, i16 y, const u8* bitmap, u8 frame) {
    drawSprite(x, y, bitmap, nullptr, frame, 0, SpriteMode::Erase);
}

void Sprites::drawSelfMasked(i16 x, i16 y, const u8* bitmap, u8 frame) {
    drawSprite(x, y, bitmap, nullptr, frame, 0, SpriteMode::SelfMasked);
}

void Sprites::drawExternalMask(i16 x, i16 y, const u8* bitmap, const u8* mask,
                               u8 frame, u8 maskFrame) {
    drawSprite(x, y, bitmap, mask, frame, maskFrame, SpriteMode::ExternalMask);
}

void Sprites::drawPlusMask(i16 x, i16 y, const u8* bitmap, u8 frame) {
    drawSprite(x, y, bitmap, nullptr, frame, 0, SpriteMode::PlusMask);
}

}  // namespace arduboy
}  // namespace hydra

#endif  // HYDRA_ENABLE_ARDUBOY
