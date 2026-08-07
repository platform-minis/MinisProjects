#pragma once
/**
 * Hydra — czcionki bitmapowe warstwy graficznej.
 *
 * Czcionka to opis tablicy glifów, a nie kod: dzięki temu font wbudowany,
 * font wygenerowany z pliku TTF i font dostarczony przez aplikację
 * obsługiwane są tą samą ścieżką rysowania. Glify są monochromatyczne,
 * bit po bicie, wiersz po wierszu — najtańszy format, jaki daje czytelny
 * tekst na wyświetlaczu wbudowanym.
 *
 * Układ danych glifu: kolejne bajty to kolejne wiersze; glif szerszy niż
 * 8 pikseli zajmuje tyle bajtów na wiersz, ile trzeba (zaokrąglone w górę).
 *
 * Kolejność bitów w wierszu jest polem struktury, a nie założeniem. Generatory
 * czcionek dzielą się tu na dwa obozy — Adafruit i GxEPD zaczynają od bitu
 * najstarszego, klasyczne tablice 8×8 (w tym wbudowana) od najmłodszego.
 * Wpisanie tego w dane pozwala trzymać obie konwencje obok siebie i eliminuje
 * najczęstszy objaw pomyłki: tekst odbity w poziomie.
 */

#include "hydra/core/Types.hpp"

namespace hydra {
namespace gfx {

struct Font {
    /** Dane glifów, ułożone kolejno od znaku `first` do `last`. */
    const u8* glyphs = nullptr;
    u8  first   = 0;   ///< kod pierwszego zawartego znaku
    u8  last    = 0;   ///< kod ostatniego zawartego znaku
    u8  width   = 0;   ///< szerokość komórki glifu w pikselach
    u8  height  = 0;   ///< wysokość komórki glifu w pikselach
    u8  advance = 0;   ///< odstęp między początkami kolejnych znaków
    /** true: bit najstarszy to lewa kolumna (Adafruit, GxEPD). */
    bool msbFirst = true;
    /** Nazwa do diagnostyki i wyboru czcionki po nazwie. */
    const char* name = "";

    constexpr bool valid() const { return glyphs != nullptr && width > 0 && height > 0; }
    constexpr u8   bytesPerRow() const { return static_cast<u8>((width + 7) / 8); }
    constexpr u16  bytesPerGlyph() const { return static_cast<u16>(bytesPerRow() * height); }
    constexpr bool covers(char c) const {
        return static_cast<u8>(c) >= first && static_cast<u8>(c) <= last;
    }

    /** Wskaźnik na dane glifu albo nullptr, gdy znak jest poza zakresem. */
    const u8* glyph(char c) const {
        if (!valid() || !covers(c)) return nullptr;
        return glyphs + static_cast<u16>(static_cast<u8>(c) - first) * bytesPerGlyph();
    }

    /** Czy piksel (col, row) glifu jest zapalony. */
    bool pixel(const u8* glyphData, u8 col, u8 row) const {
        if (!glyphData || col >= width || row >= height) return false;
        const u8 byte = glyphData[row * bytesPerRow() + (col / 8)];
        const u8 bit  = static_cast<u8>(col % 8);
        return msbFirst ? (byte & (0x80 >> bit)) != 0 : (byte & (1 << bit)) != 0;
    }
};

/**
 * Czcionka wbudowana 8×8, znaki drukowalne ASCII (0x20–0x7E).
 * Jedyna, na którą framework może liczyć zawsze — używana m.in. przez ekran
 * awaryjny, gdy konfiguracja UI jeszcze nie wystartowała.
 */
const Font& font8x8();

/** Szerokość napisu w pikselach przy zadanej skali. */
i16 textWidth(const Font& font, const char* text, u8 scale = 1);
/** Wysokość wiersza tekstu przy zadanej skali. */
i16 textHeight(const Font& font, u8 scale = 1);

}  // namespace gfx
}  // namespace hydra
