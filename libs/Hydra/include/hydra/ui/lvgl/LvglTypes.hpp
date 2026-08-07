#pragma once
/**
 * Hydra — typy pomostu do LVGL (rozdz. 6).
 *
 * LVGL jest biblioteką w C z globalnymi funkcjami, więc nie da się jej owinąć
 * szablonem po typie urządzenia, jak zrobiliśmy z bibliotekami graficznymi.
 * Zamiast tego most rozmawia z **typem cech** — strukturą statycznych metod,
 * którą wypełnia albo prawdziwe API LVGL, albo atrapa w testach.
 *
 * Efekt jest ten sam, co poprzednio: Hydra nie włącza `lvgl.h`, reguła
 * zależności z rozdz. 3 pozostaje nienaruszona, a logika pomostu — przeliczanie
 * obszarów, konwersja formatów pikseli, karmienie urządzeń wejściowych —
 * jest w całości sprawdzalna bez LVGL.
 *
 * Te typy celowo odwzorowują kształt danych LVGL, a nie jego nazwy: dzięki
 * temu zmiana wersji biblioteki dotyka wyłącznie pliku wiążącego.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Types.hpp"
#include "hydra/ui/UiTypes.hpp"

namespace hydra {
namespace ui {
namespace lvgl {

/**
 * Format pikseli bufora rysowania LVGL.
 *
 * Wariant z zamienionymi bajtami nie jest fanaberią: LVGL na ESP32 często
 * pracuje z RGB565 w kolejności odwrotnej niż wymaga panel, żeby transfer
 * DMA szedł bez przestawiania bajtów po drodze. Pomost musi o tym wiedzieć,
 * bo inaczej obraz wychodzi w fałszywych barwach.
 */
enum class ColorFormat : u8 {
    Rgb565 = 0,
    Rgb565Swapped,
    Rgb888,
    Argb8888,
    Mono1,
};

constexpr u8 bytesPerPixel(ColorFormat f) {
    switch (f) {
        case ColorFormat::Rgb565:
        case ColorFormat::Rgb565Swapped: return 2;
        case ColorFormat::Rgb888:        return 3;
        case ColorFormat::Argb8888:      return 4;
        case ColorFormat::Mono1:         return 0;  // liczone bitami, nie bajtami
    }
    return 0;
}

constexpr const char* toString(ColorFormat f) {
    switch (f) {
        case ColorFormat::Rgb565:        return "rgb565";
        case ColorFormat::Rgb565Swapped: return "rgb565-swapped";
        case ColorFormat::Rgb888:        return "rgb888";
        case ColorFormat::Argb8888:      return "argb8888";
        case ColorFormat::Mono1:         return "mono1";
    }
    return "unknown";
}

/** Dane wskaźnika przekazywane do LVGL. */
struct PointerFeed {
    i16  x       = 0;
    i16  y       = 0;
    bool pressed = false;
};

/** Dane enkodera przekazywane do LVGL: różnica i stan przycisku. */
struct EncoderFeed {
    i16  diff    = 0;
    bool pressed = false;
};

}  // namespace lvgl
}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
