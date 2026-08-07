#pragma once
/**
 * Hydra — widżety wysokopoziomowe (rozdz. 6).
 *
 * Zestaw dobrany pod to, co naprawdę pokazuje urządzenie IoT albo robot:
 * stan zasilania, jakość łącza, przebieg wielkości mierzonej, sterowanie
 * ręczne i listę ustawień. Każdy widżet przyjmuje wartość w jednostce
 * fizycznej — procentach, dBm, jednostkach czujnika — i sam decyduje,
 * jak ją pokazać przy danym motywie i rozmiarze.
 *
 * Żaden nie alokuje. Bufory mają stały rozmiar, a listy operują na tablicach
 * dostarczonych przez aplikację (rozdz. 11).
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_UI

#include "hydra/core/Delegate.hpp"
#include "hydra/ui/Widget.hpp"

namespace hydra {
namespace ui {

/** Liczba próbek pamiętanych przez wykres przebiegu. */
#ifndef HYDRA_UI_SPARKLINE_POINTS
#  define HYDRA_UI_SPARKLINE_POINTS 64
#endif
/** Maksymalna długość tekstu w etykiecie i wierszu listy. */
#ifndef HYDRA_UI_TEXT_MAX
#  define HYDRA_UI_TEXT_MAX 32
#endif

/** Wyrównanie tekstu w obrębie widżetu. */
enum class Align : u8 { Left = 0, Center, Right };

// ---------------------------------------------------------------------------

/** Napis, opcjonalnie z wartością liczbową i jednostką. */
class Label : public Widget {
public:
    explicit Label(const char* text = "");

    void setText(const char* text);
    /** Ustawia wartość liczbową z zadaną liczbą miejsc po przecinku. */
    void setValue(float value, u8 decimals = 1, const char* unit = "");
    void setAlign(Align align);
    void setColor(gfx::Color color);
    /** Przywraca kolor z motywu. */
    void useThemeColor();

    const char* text() const { return text_; }

    void draw(gfx::ISurface& surface, const Theme& theme) override;

private:
    char       text_[HYDRA_UI_TEXT_MAX] = {};
    Align      align_    = Align::Left;
    gfx::Color color_{};
    bool       hasColor_ = false;
};

// ---------------------------------------------------------------------------

/** Wskaźnik naładowania baterii z opcjonalnym znacznikiem ładowania. */
class BatteryIndicator : public Widget {
public:
    void setPercent(u8 percent);
    void setCharging(bool charging);
    /** Poziomy, poniżej których wskaźnik zmienia wygląd. */
    void setThresholds(u8 warning, u8 danger);

    u8   percent() const { return percent_; }
    bool charging() const { return charging_; }

    void draw(gfx::ISurface& surface, const Theme& theme) override;

private:
    u8   percent_   = 100;
    bool charging_  = false;
    u8   warningAt_ = 30;
    u8   dangerAt_  = 15;
};

// ---------------------------------------------------------------------------

/** Siła sygnału jako cztery słupki rosnącej wysokości. */
class SignalBars : public Widget {
public:
    static constexpr u8 kBars = 4;

    /** Siła sygnału w dBm; wartość 0 oznacza brak łącza. */
    void setRssi(i8 dbm);
    void setConnected(bool connected);

    i8 rssi() const { return rssi_; }
    /** Liczba zapalonych słupków, 0–4. */
    u8 level() const;

    void draw(gfx::ISurface& surface, const Theme& theme) override;

private:
    i8   rssi_      = 0;
    bool connected_ = false;
};

// ---------------------------------------------------------------------------

/**
 * Wykres przebiegu wielkości mierzonej.
 *
 * Skala pionowa dobiera się sama do zakresu widocznych próbek, chyba że
 * ustawiono ją jawnie. Automatyczne skalowanie jest tu domyślne, bo przy
 * telemetrii rzadko wiadomo z góry, w jakim zakresie będzie się poruszać
 * temperatura czy prąd — a wykres przyklejony do krawędzi nie niesie
 * żadnej informacji.
 */
class Sparkline : public Widget {
public:
    void push(float value);
    void clear();

    /** Wymusza stały zakres pionowy. */
    void setRange(float min, float max);
    /** Przywraca dobieranie zakresu do danych. */
    void setAutoRange();

    void setShowFrame(bool show) { frame_ = show; }

    u16   count() const { return count_; }
    float latest() const;
    float minimum() const;
    float maximum() const;

    void draw(gfx::ISurface& surface, const Theme& theme) override;

private:
    float samples_[HYDRA_UI_SPARKLINE_POINTS] = {};
    u16   count_ = 0;
    u16   head_  = 0;
    float min_   = 0.0f;
    float max_   = 0.0f;
    bool  autoRange_ = true;
    bool  frame_     = true;
};

// ---------------------------------------------------------------------------

/** Przycisk ekranowy z etykietą. Reaguje na dotyk i na enkoder z fokusem. */
class Button : public Widget {
public:
    using Handler = Delegate<void()>;

    explicit Button(const char* label = "");

    void setLabel(const char* label);
    void setHandler(Handler handler) { handler_ = handler; }
    void setEnabled(bool enabled);

    bool enabled() const { return enabled_; }
    bool pressed() const { return pressed_; }
    bool focusable() const override { return enabled_; }

    void draw(gfx::ISurface& surface, const Theme& theme) override;
    bool onPointer(const PointerEvent& event) override;
    bool onEncoder(const EncoderEvent& event) override;

private:
    void fire();

    char    label_[HYDRA_UI_TEXT_MAX] = {};
    Handler handler_{};
    bool    enabled_ = true;
    bool    pressed_ = false;
};

// ---------------------------------------------------------------------------

/**
 * Lista ustawień budowana z tablicy pozycji dostarczonej przez aplikację.
 *
 * Wiersz to etykieta i wartość — dokładnie kształt, w jakim opisuje się
 * konfigurację urządzenia. Widżet nie kopiuje ani nie posiada tych danych.
 */
class ListView : public Widget {
public:
    struct Item {
        const char* label = "";
        const char* value = "";
    };

    using SelectHandler = Delegate<void(u8)>;

    /** Podpina tablicę pozycji. Musi przeżyć widżet. */
    void setItems(const Item* items, u8 count);
    void setSelected(u8 index);
    void setSelectHandler(SelectHandler handler) { onSelect_ = handler; }

    u8 selected() const { return selected_; }
    u8 count() const { return count_; }
    /** Numer pierwszej widocznej pozycji przy bieżącym przewinięciu. */
    u8 firstVisible() const { return top_; }
    /** Ile wierszy mieści się w widżecie przy danym motywie. */
    u8 visibleRows(const Theme& theme) const;

    bool focusable() const override { return count_ > 0; }

    void draw(gfx::ISurface& surface, const Theme& theme) override;
    bool onPointer(const PointerEvent& event) override;
    bool onEncoder(const EncoderEvent& event) override;

private:
    void scrollToSelected(u8 rows);

    const Item*   items_ = nullptr;
    u8            count_ = 0;
    u8            selected_ = 0;
    u8            top_      = 0;
    u8            lastRows_ = 1;
    SelectHandler onSelect_{};
};

// ---------------------------------------------------------------------------

/**
 * Joystick ekranowy: obszar dotykowy zwracający wychylenie w promilach.
 *
 * Wartości w zakresie ±1000, tak jak wypełnienie PWM w HAL — dzięki temu
 * przekazanie wychylenia do modułu ruchu nie wymaga skalowania po drodze.
 */
class Joystick : public Widget {
public:
    using MoveHandler = Delegate<void(i16, i16)>;

    void setHandler(MoveHandler handler) { onMove_ = handler; }
    /** Czy uchwyt wraca na środek po oderwaniu palca. */
    void setSelfCentering(bool on) { selfCentering_ = on; }

    i16  x() const { return x_; }
    i16  y() const { return y_; }
    bool active() const { return active_; }
    void center();

    void draw(gfx::ISurface& surface, const Theme& theme) override;
    bool onPointer(const PointerEvent& event) override;

private:
    void updateFrom(i16 px, i16 py);

    i16         x_ = 0;
    i16         y_ = 0;
    bool        active_        = false;
    bool        selfCentering_ = true;
    MoveHandler onMove_{};
};

}  // namespace ui
}  // namespace hydra

#endif  // HYDRA_ENABLE_UI
