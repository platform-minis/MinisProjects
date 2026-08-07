#pragma once
/**
 * Hydra — widżet i ekran (rozdz. 6).
 *
 * Widżet zna swój prostokąt i umie się narysować. Nie wie, kiedy ma to zrobić —
 * o tym decyduje renderer, a widżet jedynie zgłasza, że jego zawartość się
 * zdezaktualizowała.
 *
 * Ta jednokierunkowość jest sednem warstwy deklaratywnej. Kod ekranu nie
 * zawiera logiki odświeżania: nie ma w nim pętli, nie ma porównywania starych
 * wartości z nowymi, nie ma wywołań „przerysuj teraz". Widżet dostaje nową
 * wartość, unieważnia swój obszar i wraca; reszta dzieje się sama.
 *
 * Widżety nie są właścicielami pamięci i nie są alokowane — ekran trzyma
 * wskaźniki na obiekty żyjące u aplikacji, zwykle statyczne (rozdz. 11).
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Expected.hpp"
#include "hydra/gfx/ISurface.hpp"
#include "hydra/ui/Theme.hpp"
#include "hydra/ui/UiTypes.hpp"

namespace hydra {
namespace ui {

class Screen;

class Widget {
public:
    virtual ~Widget() = default;

    void setBounds(Rect r);
    Rect bounds() const { return bounds_; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    /** Zgłasza, że zawartość widżetu wymaga przerysowania. */
    void invalidate();

    void setFocused(bool focused);
    bool focused() const { return focused_; }
    /** Czy widżet bierze udział w nawigacji enkoderem. */
    virtual bool focusable() const { return false; }

    /** Rysuje zawartość. Wołane wyłącznie z taska ui.render. */
    virtual void draw(gfx::ISurface& surface, const Theme& theme) = 0;

    /** Obsługa dotyku. true oznacza, że zdarzenie zostało skonsumowane. */
    virtual bool onPointer(const PointerEvent& event) {
        HYDRA_UNUSED(event);
        return false;
    }
    virtual bool onEncoder(const EncoderEvent& event) {
        HYDRA_UNUSED(event);
        return false;
    }

protected:
    friend class Screen;
    /** Ustawiane przez ekran przy dodaniu widżetu. */
    void setOwner(Screen* owner) { owner_ = owner; }

    Rect    bounds_{};
    Screen* owner_   = nullptr;
    bool    visible_ = true;
    bool    focused_ = false;
};

/**
 * Ekran: zbiór widżetów o wspólnym tle i wspólnej obsłudze wejścia.
 *
 * Zbiera unieważnienia od swoich widżetów i oddaje je rendererowi jako jeden
 * obszar. Dzięki temu zmiana trzech wskaźników w jednej klatce kosztuje jeden
 * transfer, a nie trzy.
 */
class Screen {
public:
    /** Maksymalna liczba widżetów na ekranie. */
    static constexpr u8 kMaxWidgets = 16;

    explicit Screen(const char* name = "screen") : name_(name) {}
    virtual ~Screen() = default;

    const char* name() const { return name_; }

    Status add(Widget& widget);
    u8     widgetCount() const { return count_; }
    Widget* widget(u8 index) const;

    /** Wołane przy wejściu na ekran i zejściu z niego. */
    virtual void onEnter() {}
    virtual void onExit() {}

    /** Rysuje tło i wszystkie widoczne widżety przecinające zadany obszar. */
    void draw(gfx::ISurface& surface, const Theme& theme, Rect area);

    /** Kieruje zdarzenie do widżetu pod wskazanym punktem. */
    bool dispatchPointer(const PointerEvent& event);
    /** Kieruje obrót do widżetu z fokusem; przycisk przesuwa fokus. */
    bool dispatchEncoder(const EncoderEvent& event);

    /** Przesuwa fokus na kolejny widżet przyjmujący fokus. */
    void focusNext();
    Widget* focusedWidget() const;

    /** Obszar zgłoszony przez widżety; zerowany przy odczycie. */
    Rect takeDirty();
    void invalidateArea(Rect area);
    /** Unieważnia cały ekran — np. po zmianie motywu. */
    void invalidateAll();

private:
    const char* name_;
    Widget*     widgets_[kMaxWidgets] = {};
    u8          count_   = 0;
    i8          focusIdx_ = -1;
    Rect        dirty_{};
    bool        fullDirty_ = false;
};

/**
 * Stos ekranów (rozdz. 6): push/pop i powrót do ekranu domowego.
 *
 * Stos, a nie lista, bo taka jest natura nawigacji w urządzeniu: wejście
 * w ustawienia, potem w podmenu, potem powrót — każdy powrót ma wracać
 * dokładnie tam, skąd przyszliśmy.
 */
class ScreenStack {
public:
    static constexpr u8 kMaxDepth = 6;

    Status push(Screen& screen);
    /** Zdejmuje bieżący ekran. Ekranu domowego nie da się zdjąć. */
    Status pop();
    /** Wraca do ekranu domowego, zdejmując wszystko powyżej. */
    Status home();

    Screen* top() const { return depth_ > 0 ? stack_[depth_ - 1] : nullptr; }
    u8      depth() const { return depth_; }

    /** Obszar do odświeżenia: pełny po zmianie ekranu, cząstkowy w trakcie. */
    Rect takeDirty(Rect fullBounds);

    void draw(gfx::ISurface& surface, const Theme& theme, Rect area);
    bool dispatchPointer(const PointerEvent& event);
    bool dispatchEncoder(const EncoderEvent& event);

private:
    Screen* stack_[kMaxDepth] = {};
    u8      depth_    = 0;
    bool    switched_ = false;
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
