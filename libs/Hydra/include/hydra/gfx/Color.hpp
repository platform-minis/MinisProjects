#pragma once
/**
 * Hydra — kolor i formaty pikseli warstwy graficznej.
 *
 * Kolor przenoszony jest zawsze jako RGBA8888 i konwertowany leniwie przez
 * backend: RGB565 na typowych TFT, luminancja z progiem na e-papierze
 * i monochromatycznych OLED-ach. Dzięki temu ten sam kod rysujący daje sensowny
 * wynik na panelu 16-bitowym i na wyświetlaczu jednobitowym, bez rozgałęzień
 * w kodzie aplikacji.
 *
 * Kształt tej warstwy wzięty jest z MinisGfx (libs/MinisLib) — sprawdzonego
 * rozwiązania tego samego problemu. Różnice wynikają z reguł Hydry: typy
 * o jawnej szerokości, brak zależności od standardowej biblioteki i operacje
 * możliwe do wykonania w czasie kompilacji.
 */

#include "hydra/core/Types.hpp"

namespace hydra {
namespace gfx {

/** Sposób upakowania piksela w pamięci bufora. */
enum class PixelFormat : u8 {
    Mono1,     ///< 1 bit na piksel, MSB pierwszy, wiersze wyrównane do bajtu
    Rgb565,    ///< 16 bitów, big-endian w buforze (kolejność bajtów paneli SPI)
    Rgb888,    ///< 24 bity, kolejność R, G, B
    Rgba8888,  ///< 32 bity, kolejność R, G, B, A
};

/** Liczba bitów na piksel danego formatu. */
constexpr u8 bitsPerPixel(PixelFormat f) {
    switch (f) {
        case PixelFormat::Mono1:    return 1;
        case PixelFormat::Rgb565:   return 16;
        case PixelFormat::Rgb888:   return 24;
        case PixelFormat::Rgba8888: return 32;
    }
    return 0;
}

constexpr const char* toString(PixelFormat f) {
    switch (f) {
        case PixelFormat::Mono1:    return "mono1";
        case PixelFormat::Rgb565:   return "rgb565";
        case PixelFormat::Rgb888:   return "rgb888";
        case PixelFormat::Rgba8888: return "rgba8888";
    }
    return "unknown";
}

/** Kolor RGBA8888. Wszystkie operacje wykonalne w czasie kompilacji. */
struct Color {
    u8 r = 0, g = 0, b = 0, a = 255;

    constexpr Color() = default;
    constexpr Color(u8 red, u8 green, u8 blue, u8 alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}

    static constexpr Color rgb(u8 r, u8 g, u8 b) { return Color(r, g, b); }
    static constexpr Color fromRgb888(u32 c) {
        return Color(static_cast<u8>((c >> 16) & 0xFF), static_cast<u8>((c >> 8) & 0xFF),
                     static_cast<u8>(c & 0xFF));
    }
    static constexpr Color fromRgb565(u16 c) {
        // Rozciągnięcie 5/6 bitów na pełne 8 z zaokrągleniem — samo przesunięcie
        // w lewo dałoby biel o wartości 248, czyli widocznie szarą.
        return Color(static_cast<u8>((((c >> 11) & 0x1F) * 255 + 15) / 31),
                     static_cast<u8>((((c >> 5) & 0x3F) * 255 + 31) / 63),
                     static_cast<u8>(((c & 0x1F) * 255 + 15) / 31));
    }

    constexpr u16 rgb565() const {
        return static_cast<u16>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    constexpr u32 rgb888() const {
        return (static_cast<u32>(r) << 16) | (static_cast<u32>(g) << 8) | b;
    }
    constexpr u32 rgba() const {
        return static_cast<u32>(r) | (static_cast<u32>(g) << 8) |
               (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
    }

    /** Jasność wg Rec.601 — podstawa progowania na panelach jednobitowych. */
    constexpr u8 luma() const {
        return static_cast<u8>((r * 77 + g * 150 + b * 29) >> 8);
    }
    /** Czy piksel zapala punkt na wyświetlaczu monochromatycznym. */
    constexpr bool mono() const { return luma() >= 128; }

    constexpr bool opaque() const { return a == 255; }
    constexpr bool invisible() const { return a == 0; }

    /** Nałożenie tego koloru na tło z uwzględnieniem kanału alfa. */
    constexpr Color over(Color background) const {
        return a == 255 ? *this
             : a == 0   ? background
                        : Color(mix(r, background.r, a), mix(g, background.g, a),
                                mix(b, background.b, a), 255);
    }

    constexpr Color withAlpha(u8 alpha) const { return Color(r, g, b, alpha); }

    /** Rozjaśnienie i przyciemnienie w procentach — do stanów aktywnych w UI. */
    constexpr Color lighter(u8 percent = 130) const {
        return Color(scale(r, percent), scale(g, percent), scale(b, percent), a);
    }
    constexpr Color darker(u8 percent = 130) const {
        return Color(static_cast<u8>(r * 100 / percent), static_cast<u8>(g * 100 / percent),
                     static_cast<u8>(b * 100 / percent), a);
    }

    constexpr bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }

private:
    static constexpr u8 mix(u8 src, u8 dst, u8 alpha) {
        return static_cast<u8>((src * alpha + dst * (255 - alpha)) / 255);
    }
    static constexpr u8 scale(u8 c, u8 percent) {
        return static_cast<u8>((c * percent / 100) > 255 ? 255 : (c * percent / 100));
    }
};

/** Paleta podstawowa. Nazwy w osobnej przestrzeni, by nie kolidować z makrami bibliotek. */
namespace colors {
constexpr Color black{0, 0, 0};
constexpr Color white{255, 255, 255};
constexpr Color red{220, 50, 50};
constexpr Color green{60, 180, 90};
constexpr Color blue{60, 120, 200};
constexpr Color yellow{235, 200, 40};
constexpr Color orange{240, 140, 40};
constexpr Color cyan{40, 200, 220};
constexpr Color magenta{210, 60, 180};
constexpr Color gray{128, 128, 128};
constexpr Color darkGray{64, 64, 64};
constexpr Color lightGray{200, 200, 200};
constexpr Color transparent{0, 0, 0, 0};
}  // namespace colors

}  // namespace gfx
}  // namespace hydra
