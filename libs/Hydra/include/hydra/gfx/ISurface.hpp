#pragma once
/**
 * Hydra — uniwersalna powierzchnia rysowania (podstawa modułu UI, rozdz. 6).
 *
 * Jedna metoda jest wymagana — writePixel(). Wszystkie pozostałe prymitywy mają
 * implementacje programowe zbudowane na niej, a backend nadpisuje tylko te,
 * które jego biblioteka robi sprzętowo. Ten kształt sprawdził się w MinisGfx
 * i jest tu zachowany: napisanie adaptera nad nową biblioteką graficzną kosztuje
 * kilkanaście linii, a nie kilkaset.
 *
 * Trzy rzeczy, których MinisGfx nie miał, a które są tu w kontrakcie:
 *
 * 1. **Przycinanie.** Rysowanie częściowo poza ekranem albo poza obszarem
 *    widżetu jest normalne, nie błędne. Prostokąt przycinania obowiązuje
 *    wszystkie prymitywy, więc widżet nie może zabazgrać sąsiada.
 * 2. **Kody błędów.** Panele na SPI potrafią nie odpowiedzieć. Operacje zwracają
 *    Status, żeby warstwa wyżej mogła to zauważyć, zamiast rysować w próżnię.
 * 3. **Śledzenie zmian.** Powierzchnia zapamiętuje obszar naruszony od ostatniego
 *    flush(). Na e-papierze i wolnym SPI to różnica między przerysowaniem
 *    całego ekranu a jednego napisu.
 *
 * Adaptery bibliotek producentów są szablonami (katalog adapters/) — dzięki temu
 * Hydra nie włącza ani jednego nagłówka biblioteki graficznej, a reguła
 * zależności z rozdz. 3 pozostaje nienaruszona.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/gfx/Color.hpp"
#include "hydra/gfx/Font.hpp"
#include "hydra/gfx/Geometry.hpp"

namespace hydra {
namespace gfx {

/** Sposób ułożenia obrazu względem panelu. */
enum class Rotation : u8 { None = 0, Cw90, Cw180, Cw270 };

class ISurface {
public:
    virtual ~ISurface() = default;

    // --- opis powierzchni ---------------------------------------------------

    virtual Size        size() const = 0;
    i16                 width() const { return size().w; }
    i16                 height() const { return size().h; }
    virtual PixelFormat pixelFormat() const = 0;
    /** Prostokąt obejmujący całą powierzchnię. */
    Rect bounds() const { return Rect(0, 0, size().w, size().h); }

    // --- cykl życia ---------------------------------------------------------

    /** Inicjalizacja panelu. Domyślnie nic nie robi. */
    virtual Status begin() { return ok(); }
    /**
     * Wypycha narysowaną zawartość na panel. Dla powierzchni rysujących
     * bezpośrednio (TFT na SPI) jest to operacja pusta; dla buforowanych
     * (OLED, e-papier, framebuffer) — właściwy transfer.
     */
    virtual Status flush() { clearDirty(); return ok(); }

    /** Otwarcie i zamknięcie serii zapisów; backendy SPI trzymają w niej CS. */
    virtual void beginBatch() {}
    virtual void endBatch() {}

    // --- przycinanie --------------------------------------------------------

    /** Ogranicza rysowanie do części wspólnej z powierzchnią. */
    void setClip(Rect r) {
        clip_    = r.intersect(bounds());
        clipSet_ = true;
    }
    void resetClip() { clipSet_ = false; }
    /** Domyślnie cała powierzchnia — dopóki nikt nie zawęzi obszaru. */
    Rect clip() const { return clipSet_ ? clip_ : bounds(); }

    // --- obszar zmieniony ---------------------------------------------------

    /** Obszar naruszony od ostatniego flush(). Pusty = nic się nie zmieniło. */
    Rect dirty() const { return dirty_; }
    void clearDirty() { dirty_ = Rect(); }
    /** Ręczne oznaczenie obszaru jako zmienionego — dla backendów rysujących same. */
    void markDirty(Rect r) { dirty_ = dirty_.unite(r.intersect(bounds())); }

    // --- prymitywy ----------------------------------------------------------

    /** Pojedynczy piksel z uwzględnieniem przycinania i kanału alfa. */
    Status drawPixel(i16 x, i16 y, Color c);

    virtual Status fill(Color c);
    Status clear(Color c = colors::black) { return fill(c); }

    virtual Status hLine(i16 x, i16 y, i16 w, Color c);
    virtual Status vLine(i16 x, i16 y, i16 h, Color c);
    virtual Status line(i16 x0, i16 y0, i16 x1, i16 y1, Color c);

    virtual Status drawRect(Rect r, Color c);
    virtual Status fillRect(Rect r, Color c);
    virtual Status drawRoundRect(Rect r, i16 radius, Color c);
    virtual Status fillRoundRect(Rect r, i16 radius, Color c);

    virtual Status drawCircle(i16 cx, i16 cy, i16 radius, Color c);
    virtual Status fillCircle(i16 cx, i16 cy, i16 radius, Color c);

    virtual Status drawTriangle(Point a, Point b, Point c, Color color);
    virtual Status fillTriangle(Point a, Point b, Point c, Color color);

    /**
     * Bitmapa jednobitowa, bit najstarszy z lewej — układ Adafruit i GxEPD.
     * Kolor tła pominięty oznacza, że zerowe bity nie są rysowane.
     */
    virtual Status drawBitmap1(i16 x, i16 y, const u8* bitmap, i16 w, i16 h, Color fg);
    virtual Status drawBitmap1(i16 x, i16 y, const u8* bitmap, i16 w, i16 h, Color fg,
                               Color bg);
    /** Obraz RGB565 ułożony wierszami. */
    virtual Status drawBitmapRgb565(i16 x, i16 y, const u16* pixels, i16 w, i16 h);

    // --- tekst --------------------------------------------------------------

    virtual Status drawChar(i16 x, i16 y, char ch, Color fg, const Font& font,
                            u8 scale = 1);
    /** Rysuje napis; znak nowej linii przenosi kursor do początku wiersza. */
    virtual Status drawText(i16 x, i16 y, const char* text, Color fg, const Font& font,
                            u8 scale = 1);
    /** Wariant z tłem — potrzebny tam, gdzie nie da się wyczyścić obszaru. */
    virtual Status drawText(i16 x, i16 y, const char* text, Color fg, Color bg,
                            const Font& font, u8 scale = 1);

protected:
    /**
     * Jedyna metoda wymagana od backendu. Współrzędne są już przycięte
     * i mieszczą się w powierzchni, a kolor jest nieprzezroczysty.
     */
    virtual Status writePixel(i16 x, i16 y, Color c) = 0;

    /**
     * Odczyt piksela — potrzebny wyłącznie do mieszania z kanałem alfa.
     * Panele bez odczytu zwracają Err::NotSupported; warstwa rysująca traktuje
     * wtedy kolor półprzezroczysty jak nieprzezroczysty, zamiast go gubić.
     */
    virtual Result<Color> readPixel(i16 x, i16 y) const {
        HYDRA_UNUSED(x);
        HYDRA_UNUSED(y);
        return unexpected(Err::NotSupported);
    }

private:
    Rect clip_{};
    Rect dirty_{};
    bool clipSet_ = false;
};

}  // namespace gfx
}  // namespace hydra
