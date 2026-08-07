#pragma once
/**
 * Hydra — motyw interfejsu (rozdz. 6).
 *
 * Motyw niesie kolory, czcionkę i metryki układu. Widżety nigdy nie zapisują
 * konkretnych barw ani odstępów — biorą je stąd. Dzięki temu przełączenie
 * jasny/ciemny albo zmiana skali dla innej rozdzielczości nie wymaga tknięcia
 * kodu widżetów.
 *
 * Motyw monochromatyczny nie jest ozdobnikiem. Na wyświetlaczu jednobitowym
 * paleta kolorów sprowadza się do dwóch wartości, więc odróżnienie stanu
 * ostrzegawczego od zwykłego musi opierać się na kształcie i wypełnieniu,
 * a nie na barwie. Widżet czytający kolory z motywu dostaje to za darmo.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/gfx/Color.hpp"
#include "hydra/gfx/Font.hpp"
#include "hydra/ui/UiTypes.hpp"

namespace hydra {
namespace ui {

struct Theme {
    // --- kolory ---
    gfx::Color background{};   ///< tło ekranu
    gfx::Color surface{};      ///< tło elementów wypukłych (karty, listy)
    gfx::Color text{};
    gfx::Color textMuted{};    ///< podpisy, jednostki, stan nieaktywny
    gfx::Color accent{};       ///< element wyróżniony, fokus
    gfx::Color ok{};
    gfx::Color warning{};
    gfx::Color danger{};
    gfx::Color border{};

    // --- metryki ---
    const gfx::Font* font = nullptr;
    /** Skala czcionki i grubości: 1× dla małych paneli, 2× dla dużych. */
    u8  scale   = 1;
    i16 padding = 4;
    i16 radius  = 4;
    /** Wysokość wiersza listy; zero oznacza wyliczenie z czcionki. */
    i16 rowHeight = 0;

    /** Czy motyw przewiduje wyłącznie dwie barwy. */
    bool monochrome = false;

    // --- gotowe motywy ---
    static Theme dark(u8 scale = 1);
    static Theme light(u8 scale = 1);
    /** Dla paneli jednobitowych: OLED, e-papier. */
    static Theme mono(u8 scale = 1);

    /** Wysokość wiersza tekstu przy skali motywu. */
    i16 lineHeight() const;
    /** Wysokość wiersza listy — z metryki albo wyliczona z czcionki. */
    i16 rowHeightOrDefault() const;
    /** Szerokość napisu przy skali motywu. */
    i16 textWidth(const char* text) const;
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
