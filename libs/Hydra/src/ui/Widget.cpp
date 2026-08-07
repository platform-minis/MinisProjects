/** Hydra — implementacja widżetu, ekranu i stosu ekranów (rozdz. 6). */

#include "hydra/ui/Widget.hpp"

#if HYDRA_ENABLE_UI

namespace hydra {
namespace ui {

// ---------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------

void Widget::setBounds(Rect r) {
    if (r == bounds_) return;
    // Unieważniamy stare i nowe położenie: przesunięty widżet zostawiłby
    // po sobie ślad w miejscu, z którego zniknął.
    const Rect previous = bounds_;
    bounds_ = r;
    if (owner_) {
        owner_->invalidateArea(previous);
        owner_->invalidateArea(bounds_);
    }
}

void Widget::setVisible(bool visible) {
    if (visible == visible_) return;
    visible_ = visible;
    invalidate();
}

void Widget::invalidate() {
    if (owner_) owner_->invalidateArea(bounds_);
}

void Widget::setFocused(bool focused) {
    if (focused == focused_) return;
    focused_ = focused;
    invalidate();
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

Status Screen::add(Widget& widget) {
    // Sprawdzenie duplikatu musi wyprzedzić sprawdzenie pojemności: ponowne
    // dodanie widżetu, który już jest na ekranie, niczego nie zajmuje i nie ma
    // powodu, by zawodziło tylko dlatego, że lista jest pełna.
    for (u8 i = 0; i < count_; ++i) {
        if (widgets_[i] == &widget) return ok();
    }
    if (count_ >= kMaxWidgets) return fail(Err::OutOfMemory);
    widgets_[count_++] = &widget;
    widget.setOwner(this);
    invalidateArea(widget.bounds());
    return ok();
}

Widget* Screen::widget(u8 index) const { return index < count_ ? widgets_[index] : nullptr; }

void Screen::invalidateArea(Rect area) {
    if (area.empty()) return;
    dirty_ = dirty_.unite(area);
}

void Screen::invalidateAll() { fullDirty_ = true; }

Rect Screen::takeDirty() {
    const Rect result = dirty_;
    dirty_ = Rect();
    return result;
}

void Screen::draw(gfx::ISurface& surface, const Theme& theme, Rect area) {
    // Tło maluje ekran, nie widżety — inaczej każdy z nich musiałby wiedzieć,
    // co jest pod nim.
    surface.fillRect(area, theme.background);

    for (u8 i = 0; i < count_; ++i) {
        Widget* w = widgets_[i];
        if (!w || !w->visible()) continue;
        if (!w->bounds().intersects(area)) continue;

        // Przycięcie do prostokąta widżetu: widżet nie ma jak zamazać sąsiada,
        // nawet gdyby próbował.
        const Rect previousClip = surface.clip();
        surface.setClip(w->bounds().intersect(area));
        w->draw(surface, theme);
        surface.setClip(previousClip);
    }
}

bool Screen::dispatchPointer(const PointerEvent& event) {
    // Od góry: widżet dodany później leży wyżej i ma pierwszeństwo.
    for (i8 i = static_cast<i8>(count_) - 1; i >= 0; --i) {
        Widget* w = widgets_[i];
        if (!w || !w->visible()) continue;
        if (!w->bounds().contains(event.x, event.y)) continue;
        if (w->onPointer(event)) return true;
    }
    return false;
}

bool Screen::dispatchEncoder(const EncoderEvent& event) {
    Widget* target = focusedWidget();
    if (target && target->onEncoder(event)) return true;

    // Nieskonsumowany obrót przesuwa fokus — tak działa nawigacja
    // jednopokrętłowa, w której to samo pokrętło wybiera i zmienia.
    if (event.delta != 0 && !target) {
        focusNext();
        return true;
    }
    return false;
}

void Screen::focusNext() {
    if (count_ == 0) return;

    const i8 start = focusIdx_;
    for (u8 step = 1; step <= count_; ++step) {
        const i8 candidate = static_cast<i8>((start + step) % count_);
        Widget*  w         = widgets_[candidate];
        if (!w || !w->visible() || !w->focusable()) continue;

        if (start >= 0 && widgets_[start]) widgets_[start]->setFocused(false);
        w->setFocused(true);
        focusIdx_ = candidate;
        return;
    }
}

Widget* Screen::focusedWidget() const {
    return (focusIdx_ >= 0 && focusIdx_ < static_cast<i8>(count_)) ? widgets_[focusIdx_]
                                                                  : nullptr;
}

// ---------------------------------------------------------------------------
// ScreenStack
// ---------------------------------------------------------------------------

Status ScreenStack::push(Screen& screen) {
    if (depth_ >= kMaxDepth) return fail(Err::OutOfMemory);

    if (Screen* current = top()) current->onExit();
    stack_[depth_++] = &screen;
    screen.onEnter();
    switched_ = true;
    return ok();
}

Status ScreenStack::pop() {
    // Ekran domowy zostaje zawsze — bez niego nie byłoby co pokazać.
    if (depth_ <= 1) return fail(Err::NotSupported);

    stack_[depth_ - 1]->onExit();
    --depth_;
    if (Screen* current = top()) current->onEnter();
    switched_ = true;
    return ok();
}

Status ScreenStack::home() {
    if (depth_ == 0) return fail(Err::NotFound);
    while (depth_ > 1) {
        stack_[depth_ - 1]->onExit();
        --depth_;
    }
    stack_[0]->onEnter();
    switched_ = true;
    return ok();
}

Rect ScreenStack::takeDirty(Rect fullBounds) {
    if (switched_) {
        // Po zmianie ekranu nie ma czego odświeżać częściowo — poprzednia
        // zawartość nie ma nic wspólnego z nową.
        switched_ = false;
        if (Screen* current = top()) current->takeDirty();
        return fullBounds;
    }

    Screen* current = top();
    if (!current) return Rect();
    return current->takeDirty();
}

void ScreenStack::draw(gfx::ISurface& surface, const Theme& theme, Rect area) {
    if (Screen* current = top()) current->draw(surface, theme, area);
}

bool ScreenStack::dispatchPointer(const PointerEvent& event) {
    Screen* current = top();
    return current ? current->dispatchPointer(event) : false;
}

bool ScreenStack::dispatchEncoder(const EncoderEvent& event) {
    Screen* current = top();
    return current ? current->dispatchEncoder(event) : false;
}

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
