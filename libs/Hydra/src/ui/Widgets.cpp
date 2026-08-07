/** Hydra — implementacja widżetów wysokopoziomowych (rozdz. 6). */

#include "hydra/ui/Widgets.hpp"

#if HYDRA_ENABLE_UI

#include <stdio.h>
#include <string.h>

namespace hydra {
namespace ui {
namespace {

using gfx::Color;

i16 imin(i16 a, i16 b) { return a < b ? a : b; }
i16 imax(i16 a, i16 b) { return a > b ? a : b; }

/** Położenie tekstu w prostokącie zgodnie z wyrównaniem. */
i16 alignedX(Rect area, i16 textW, Align align, i16 padding) {
    switch (align) {
        case Align::Center: return static_cast<i16>(area.x + (area.w - textW) / 2);
        case Align::Right:  return static_cast<i16>(area.right() - padding - textW + 1);
        case Align::Left:   break;
    }
    return static_cast<i16>(area.x + padding);
}

/** Wyśrodkowanie w pionie — tekst zawsze siedzi w osi swojego wiersza. */
i16 centeredY(Rect area, i16 textH) {
    return static_cast<i16>(area.y + (area.h - textH) / 2);
}

void copyText(char* dst, size_t cap, const char* src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

}  // namespace

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

Label::Label(const char* text) { copyText(text_, sizeof(text_), text); }

void Label::setText(const char* text) {
    // Bez porównania każda aktualizacja telemetrii unieważniałaby obszar,
    // nawet gdy wyświetlana wartość się nie zmieniła.
    if (text && strncmp(text_, text, sizeof(text_) - 1) == 0) return;
    copyText(text_, sizeof(text_), text);
    invalidate();
}

void Label::setValue(float value, u8 decimals, const char* unit) {
    char buffer[HYDRA_UI_TEXT_MAX];
    if (decimals > 6) decimals = 6;
    snprintf(buffer, sizeof(buffer), "%.*f%s%s", static_cast<int>(decimals),
             static_cast<double>(value), (unit && unit[0]) ? " " : "", unit ? unit : "");
    setText(buffer);
}

void Label::setAlign(Align align) {
    if (align == align_) return;
    align_ = align;
    invalidate();
}

void Label::setColor(gfx::Color color) {
    color_    = color;
    hasColor_ = true;
    invalidate();
}

void Label::useThemeColor() {
    hasColor_ = false;
    invalidate();
}

void Label::draw(gfx::ISurface& surface, const Theme& theme) {
    if (!theme.font || text_[0] == '\0') return;

    const i16   w = theme.textWidth(text_);
    const i16   h = theme.lineHeight();
    const Color c = hasColor_ ? color_ : theme.text;

    surface.drawText(alignedX(bounds_, w, align_, theme.padding), centeredY(bounds_, h),
                     text_, c, *theme.font, theme.scale);
}

// ---------------------------------------------------------------------------
// BatteryIndicator
// ---------------------------------------------------------------------------

void BatteryIndicator::setPercent(u8 percent) {
    if (percent > 100) percent = 100;
    if (percent == percent_) return;
    percent_ = percent;
    invalidate();
}

void BatteryIndicator::setCharging(bool charging) {
    if (charging == charging_) return;
    charging_ = charging;
    invalidate();
}

void BatteryIndicator::setThresholds(u8 warning, u8 danger) {
    warningAt_ = warning;
    dangerAt_  = danger;
    invalidate();
}

void BatteryIndicator::draw(gfx::ISurface& surface, const Theme& theme) {
    if (bounds_.w < 8 || bounds_.h < 5) return;

    // Styk baterii to zawsze mały fragment po prawej — bez niego kształt
    // nie czyta się jako bateria.
    const i16 tipW = imax(2, static_cast<i16>(bounds_.w / 10));
    const Rect body(bounds_.x, bounds_.y, static_cast<i16>(bounds_.w - tipW), bounds_.h);
    const Rect tip(static_cast<i16>(body.right() + 1),
                   static_cast<i16>(bounds_.y + bounds_.h / 4), tipW,
                   imax(1, static_cast<i16>(bounds_.h / 2)));

    surface.drawRect(body, theme.border);
    surface.fillRect(tip, theme.border);

    const Rect inner = body.inflated(-2);
    if (inner.empty()) return;

    const i16 fillW = static_cast<i16>(static_cast<i32>(inner.w) * percent_ / 100);
    if (fillW > 0) {
        Color fill = theme.ok;
        if (percent_ <= dangerAt_)       fill = theme.danger;
        else if (percent_ <= warningAt_) fill = theme.warning;
        surface.fillRect(Rect(inner.x, inner.y, fillW, inner.h), fill);
    }

    if (!charging_) return;

    // Na panelu jednobitowym kolor nie odróżni ładowania od rozładowywania,
    // więc znacznik jest kształtem: pionowa kreska pośrodku.
    const i16 cx = static_cast<i16>(inner.x + inner.w / 2);
    surface.vLine(cx, inner.y, inner.h,
                  theme.monochrome ? theme.background : theme.text);
}

// ---------------------------------------------------------------------------
// SignalBars
// ---------------------------------------------------------------------------

void SignalBars::setRssi(i8 dbm) {
    if (dbm == rssi_) return;
    rssi_ = dbm;
    invalidate();
}

void SignalBars::setConnected(bool connected) {
    if (connected == connected_) return;
    connected_ = connected;
    invalidate();
}

u8 SignalBars::level() const {
    if (!connected_ || rssi_ == 0) return 0;
    // Progi z praktyki Wi-Fi: powyżej -55 dBm łącze jest bardzo dobre,
    // poniżej -80 dBm zaczyna gubić pakiety.
    if (rssi_ >= -55) return 4;
    if (rssi_ >= -65) return 3;
    if (rssi_ >= -75) return 2;
    if (rssi_ >= -85) return 1;
    return 0;
}

void SignalBars::draw(gfx::ISurface& surface, const Theme& theme) {
    if (bounds_.w < kBars * 2 || bounds_.h < 4) return;

    const u8  lit    = level();
    const i16 gap    = imax(1, static_cast<i16>(theme.scale));
    const i16 barW   = static_cast<i16>((bounds_.w - gap * (kBars - 1)) / kBars);
    if (barW <= 0) return;

    for (u8 i = 0; i < kBars; ++i) {
        // Każdy kolejny słupek jest wyższy — poziom czyta się z kształtu,
        // nie tylko z liczby zapalonych elementów.
        const i16 barH = static_cast<i16>(bounds_.h * (i + 1) / kBars);
        const Rect bar(static_cast<i16>(bounds_.x + i * (barW + gap)),
                       static_cast<i16>(bounds_.bottom() - barH + 1), barW, barH);

        if (i < lit) {
            surface.fillRect(bar, theme.accent);
        } else {
            // Słupek nieaktywny zostaje obrysem: widać, ile poziomów brakuje.
            surface.drawRect(bar, theme.textMuted);
        }
    }
}

// ---------------------------------------------------------------------------
// Sparkline
// ---------------------------------------------------------------------------

void Sparkline::push(float value) {
    samples_[head_] = value;
    head_ = static_cast<u16>((head_ + 1) % HYDRA_UI_SPARKLINE_POINTS);
    if (count_ < HYDRA_UI_SPARKLINE_POINTS) ++count_;
    invalidate();
}

void Sparkline::clear() {
    count_ = 0;
    head_  = 0;
    invalidate();
}

void Sparkline::setRange(float min, float max) {
    min_       = min;
    max_       = max;
    autoRange_ = false;
    invalidate();
}

void Sparkline::setAutoRange() {
    autoRange_ = true;
    invalidate();
}

float Sparkline::latest() const {
    if (count_ == 0) return 0.0f;
    const u16 last = static_cast<u16>((head_ + HYDRA_UI_SPARKLINE_POINTS - 1) %
                                      HYDRA_UI_SPARKLINE_POINTS);
    return samples_[last];
}

float Sparkline::minimum() const {
    if (count_ == 0) return 0.0f;
    float m = samples_[0];
    for (u16 i = 1; i < count_; ++i) {
        if (samples_[i] < m) m = samples_[i];
    }
    return m;
}

float Sparkline::maximum() const {
    if (count_ == 0) return 0.0f;
    float m = samples_[0];
    for (u16 i = 1; i < count_; ++i) {
        if (samples_[i] > m) m = samples_[i];
    }
    return m;
}

void Sparkline::draw(gfx::ISurface& surface, const Theme& theme) {
    if (frame_) surface.drawRect(bounds_, theme.border);
    if (count_ < 2) return;

    const Rect plot = frame_ ? bounds_.inflated(-1) : bounds_;
    if (plot.empty()) return;

    float lo = min_;
    float hi = max_;
    if (autoRange_) {
        lo = minimum();
        hi = maximum();
    }
    // Zakres zerowy oznaczałby dzielenie przez zero; rozsuwamy go symetrycznie,
    // żeby stała wartość rysowała się w połowie wysokości, a nie przy krawędzi.
    if (hi <= lo) {
        const float mid = lo;
        lo = mid - 1.0f;
        hi = mid + 1.0f;
    }

    const u16 points  = imin(static_cast<i16>(count_), plot.w);
    const u16 startAt = static_cast<u16>(count_ - points);

    i16 previousX = 0;
    i16 previousY = 0;

    for (u16 i = 0; i < points; ++i) {
        // Bufor jest pierścieniowy: najstarsza widoczna próbka leży
        // `count_` pozycji przed głową.
        const u16 index = static_cast<u16>(
            (head_ + HYDRA_UI_SPARKLINE_POINTS - count_ + startAt + i) %
            HYDRA_UI_SPARKLINE_POINTS);
        const float value = samples_[index];

        const i16 x = points > 1
                          ? static_cast<i16>(plot.x + static_cast<i32>(plot.w - 1) * i /
                                                          (points - 1))
                          : plot.x;
        const float norm = (value - lo) / (hi - lo);
        const i16   y    = static_cast<i16>(plot.bottom() -
                                            static_cast<i32>((plot.h - 1) * norm));

        if (i > 0) surface.line(previousX, previousY, x, y, theme.accent);
        previousX = x;
        previousY = y;
    }
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

Button::Button(const char* label) { copyText(label_, sizeof(label_), label); }

void Button::setLabel(const char* label) {
    if (label && strncmp(label_, label, sizeof(label_) - 1) == 0) return;
    copyText(label_, sizeof(label_), label);
    invalidate();
}

void Button::setEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    if (!enabled_) pressed_ = false;
    invalidate();
}

void Button::draw(gfx::ISurface& surface, const Theme& theme) {
    const Color fill = pressed_ ? theme.accent : theme.surface;
    surface.fillRoundRect(bounds_, theme.radius, fill);

    // Fokus i wciśnięcie muszą być rozróżnialne także bez koloru: fokus
    // pogrubia obrys, wciśnięcie wypełnia wnętrze.
    surface.drawRoundRect(bounds_, theme.radius, focused_ ? theme.accent : theme.border);
    if (focused_) {
        surface.drawRoundRect(bounds_.inflated(-1), theme.radius, theme.accent);
    }

    if (!theme.font || label_[0] == '\0') return;

    Color textColor = enabled_ ? theme.text : theme.textMuted;
    if (pressed_ && !theme.monochrome) textColor = theme.background;

    const i16 w = theme.textWidth(label_);
    surface.drawText(static_cast<i16>(bounds_.x + (bounds_.w - w) / 2),
                     centeredY(bounds_, theme.lineHeight()), label_, textColor,
                     *theme.font, theme.scale);
}

void Button::fire() {
    if (handler_) handler_();
}

bool Button::onPointer(const PointerEvent& event) {
    if (!enabled_) return false;

    switch (event.action) {
        case PointerAction::Down:
            pressed_ = true;
            invalidate();
            return true;

        case PointerAction::Up:
            if (!pressed_) return false;
            pressed_ = false;
            invalidate();
            // Wywołanie dopiero przy oderwaniu palca: użytkownik może
            // zsunąć palec z przycisku i wycofać się z naciśnięcia.
            if (bounds_.contains(event.x, event.y)) fire();
            return true;

        case PointerAction::Move:
            return pressed_;
    }
    return false;
}

bool Button::onEncoder(const EncoderEvent& event) {
    if (!enabled_ || !focused_) return false;
    if (!event.pressed) return false;
    fire();
    return true;
}

// ---------------------------------------------------------------------------
// ListView
// ---------------------------------------------------------------------------

void ListView::setItems(const Item* items, u8 count) {
    items_ = items;
    count_ = items ? count : 0;
    if (selected_ >= count_) selected_ = count_ > 0 ? static_cast<u8>(count_ - 1) : 0;
    top_ = 0;
    invalidate();
}

void ListView::setSelected(u8 index) {
    if (count_ == 0 || index >= count_ || index == selected_) return;
    selected_ = index;
    scrollToSelected(lastRows_);
    invalidate();
}

u8 ListView::visibleRows(const Theme& theme) const {
    const i16 row = theme.rowHeightOrDefault();
    if (row <= 0) return 0;
    const i16 rows = static_cast<i16>(bounds_.h / row);
    return rows > 0 ? static_cast<u8>(rows) : 0;
}

void ListView::scrollToSelected(u8 rows) {
    if (rows == 0) return;
    // Przewijamy o tyle, ile trzeba, żeby zaznaczenie było widoczne — nigdy
    // więcej. Skok o pół listy przy każdym ruchu byłby nieczytelny.
    if (selected_ < top_) {
        top_ = selected_;
    } else if (selected_ >= top_ + rows) {
        top_ = static_cast<u8>(selected_ - rows + 1);
    }
}

void ListView::draw(gfx::ISurface& surface, const Theme& theme) {
    if (!items_ || count_ == 0 || !theme.font) return;

    const i16 rowH = theme.rowHeightOrDefault();
    const u8  rows = visibleRows(theme);
    if (rows == 0) return;
    lastRows_ = rows;

    for (u8 i = 0; i < rows && top_ + i < count_; ++i) {
        const u8   index = static_cast<u8>(top_ + i);
        const Rect row(bounds_.x, static_cast<i16>(bounds_.y + i * rowH), bounds_.w, rowH);
        const bool isSelected = (index == selected_);

        if (isSelected) {
            surface.fillRect(row, focused_ ? theme.accent : theme.surface);
        }

        gfx::Color labelColor = theme.text;
        if (isSelected && focused_ && !theme.monochrome) labelColor = theme.background;

        const i16 textY = centeredY(row, theme.lineHeight());
        surface.drawText(static_cast<i16>(row.x + theme.padding), textY,
                         items_[index].label, labelColor, *theme.font, theme.scale);

        if (items_[index].value && items_[index].value[0]) {
            const i16 valueW = theme.textWidth(items_[index].value);
            surface.drawText(static_cast<i16>(row.right() - theme.padding - valueW + 1),
                             textY, items_[index].value,
                             isSelected && focused_ && !theme.monochrome ? labelColor
                                                                        : theme.textMuted,
                             *theme.font, theme.scale);
        }

        if (isSelected && theme.monochrome) {
            // Bez koloru zaznaczenie musi być kształtem.
            surface.drawRect(row, theme.text);
        }
    }
}

bool ListView::onPointer(const PointerEvent& event) {
    if (!items_ || count_ == 0 || event.action != PointerAction::Up) return false;

    const i16 rowH = lastRows_ > 0 && bounds_.h > 0
                         ? static_cast<i16>(bounds_.h / lastRows_)
                         : 0;
    if (rowH <= 0) return false;

    const i16 offset = static_cast<i16>(event.y - bounds_.y);
    if (offset < 0) return false;

    const u8 index = static_cast<u8>(top_ + offset / rowH);
    if (index >= count_) return false;

    selected_ = index;
    invalidate();
    if (onSelect_) onSelect_(selected_);
    return true;
}

bool ListView::onEncoder(const EncoderEvent& event) {
    if (!items_ || count_ == 0 || !focused_) return false;

    if (event.delta != 0) {
        const i32 target = static_cast<i32>(selected_) + event.delta;
        // Zatrzymanie na krańcach zamiast zawijania: przy liście ustawień
        // przeskok z ostatniej pozycji na pierwszą myli użytkownika.
        const u8 clamped = target < 0 ? 0
                         : target >= count_ ? static_cast<u8>(count_ - 1)
                                            : static_cast<u8>(target);
        if (clamped != selected_) {
            selected_ = clamped;
            scrollToSelected(lastRows_);
            invalidate();
        }
        return true;
    }

    if (event.pressed) {
        if (onSelect_) onSelect_(selected_);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Joystick
// ---------------------------------------------------------------------------

void Joystick::center() {
    if (x_ == 0 && y_ == 0) return;
    x_ = 0;
    y_ = 0;
    invalidate();
    if (onMove_) onMove_(0, 0);
}

void Joystick::updateFrom(i16 px, i16 py) {
    const i16 cx = static_cast<i16>(bounds_.x + bounds_.w / 2);
    const i16 cy = static_cast<i16>(bounds_.y + bounds_.h / 2);
    const i16 halfW = imax(1, static_cast<i16>(bounds_.w / 2));
    const i16 halfH = imax(1, static_cast<i16>(bounds_.h / 2));

    // Wychylenie w promilach — ta sama jednostka, w której HAL przyjmuje
    // wypełnienie PWM, więc droga do modułu ruchu nie wymaga skalowania.
    i32 nx = static_cast<i32>(px - cx) * 1000 / halfW;
    i32 ny = static_cast<i32>(py - cy) * 1000 / halfH;

    if (nx > 1000) nx = 1000;
    if (nx < -1000) nx = -1000;
    if (ny > 1000) ny = 1000;
    if (ny < -1000) ny = -1000;

    const i16 newX = static_cast<i16>(nx);
    const i16 newY = static_cast<i16>(ny);
    if (newX == x_ && newY == y_) return;

    x_ = newX;
    y_ = newY;
    invalidate();
    if (onMove_) onMove_(x_, y_);
}

bool Joystick::onPointer(const PointerEvent& event) {
    switch (event.action) {
        case PointerAction::Down:
            active_ = true;
            updateFrom(event.x, event.y);
            return true;

        case PointerAction::Move:
            if (!active_) return false;
            updateFrom(event.x, event.y);
            return true;

        case PointerAction::Up:
            if (!active_) return false;
            active_ = false;
            // Powrót na środek po oderwaniu palca to zabezpieczenie: robot
            // nie może jechać dalej dlatego, że ktoś puścił ekran.
            if (selfCentering_) center();
            else invalidate();
            return true;
    }
    return false;
}

void Joystick::draw(gfx::ISurface& surface, const Theme& theme) {
    const i16 cx = static_cast<i16>(bounds_.x + bounds_.w / 2);
    const i16 cy = static_cast<i16>(bounds_.y + bounds_.h / 2);
    const i16 radius = static_cast<i16>(imin(bounds_.w, bounds_.h) / 2 - 1);
    if (radius <= 2) return;

    surface.drawCircle(cx, cy, radius, theme.border);
    // Krzyż osi — bez niego trudno ocenić położenie środka na małym ekranie.
    surface.hLine(static_cast<i16>(cx - radius / 4), cy,
                  static_cast<i16>(radius / 2), theme.textMuted);
    surface.vLine(cx, static_cast<i16>(cy - radius / 4), static_cast<i16>(radius / 2),
                  theme.textMuted);

    const i16 knobR = imax(2, static_cast<i16>(radius / 3));
    const i16 knobX = static_cast<i16>(cx + static_cast<i32>(x_) * (radius - knobR) / 1000);
    const i16 knobY = static_cast<i16>(cy + static_cast<i32>(y_) * (radius - knobR) / 1000);

    surface.fillCircle(knobX, knobY, knobR, active_ ? theme.accent : theme.surface);
    surface.drawCircle(knobX, knobY, knobR, theme.border);
}

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
