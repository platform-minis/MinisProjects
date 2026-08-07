/**
 * Testy warstwy deklaratywnej interfejsu (etap 4c).
 *
 * Sprawdzają to, co decyduje o użyteczności warstwy: czy widżet unieważnia
 * swój obszar tylko wtedy, gdy naprawdę się zmienił, czy ekran chroni sąsiadów
 * przycinaniem i czy wiązanie danych naprawdę przenosi aktualizację do taska
 * interfejsu, zamiast dotykać widżetu z obcego kontekstu.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/ui/Binding.hpp"
#include "hydra/ui/Mock.hpp"
#include "hydra/ui/Widgets.hpp"

using namespace hydra;
using namespace hydra::ui;
using namespace hydra::gfx;

namespace {

void resetWidgets() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

/** Płótno 64×32 RGB565 do oglądania wyniku rysowania. */
struct Canvas {
    static constexpr i16 kW = 64;
    static constexpr i16 kH = 32;

    u8          buffer[Framebuffer::bytesNeeded(kW, kH, PixelFormat::Rgb565)] = {};
    Framebuffer fb;

    Canvas() { fb.attach(ByteSpan{buffer, sizeof(buffer)}, kW, kH, PixelFormat::Rgb565); }

    Color at(i16 x, i16 y) { return fb.pixelAt(x, y).value_or(colors::transparent); }

    /**
     * Kolor po zapisie do bufora RGB565 i odczycie z powrotem. Bufor ma
     * 16 bitów na piksel, więc odczytana barwa nie jest bitowo identyczna
     * z zapisaną — porównywanie z oryginałem dałoby fałszywe niepowodzenie.
     */
    static Color quantized(Color c) { return Color::fromRgb565(c.rgb565()); }
    int   litCount() {
        int n = 0;
        for (i16 y = 0; y < kH; ++y) {
            for (i16 x = 0; x < kW; ++x) {
                if (at(x, y) != colors::black) ++n;
            }
        }
        return n;
    }
};

/** Zdarzenie testowe do sprawdzenia wiązania danych. */
struct Reading {
    float value;
};

}  // namespace

HYDRA_DECLARE_EVENT(Reading, "test/ui-reading")

// ---------------------------------------------------------------------------
// Motyw
// ---------------------------------------------------------------------------

TEST("Motyw: warianty różnią się barwą, ale nie kształtem metryk") {
    const Theme dark  = Theme::dark();
    const Theme light = Theme::light();
    const Theme mono  = Theme::mono();

    CHECK(dark.background != light.background);
    CHECK(dark.text != light.text);
    CHECK(dark.font != nullptr);

    // Motyw monochromatyczny sprowadza wszystko do dwóch barw — widżet musi
    // odróżniać stany kształtem, nie kolorem.
    CHECK(mono.monochrome);
    CHECK(mono.ok == mono.danger);
    CHECK(mono.text != mono.background);
    CHECK(!dark.monochrome);
}

TEST("Motyw: skala mnoży metryki i wysokość wiersza") {
    const Theme one = Theme::dark(1);
    const Theme two = Theme::dark(2);

    CHECK_EQ(static_cast<int>(two.lineHeight()), static_cast<int>(one.lineHeight()) * 2);
    CHECK_EQ(static_cast<int>(two.padding), static_cast<int>(one.padding) * 2);
    CHECK_EQ(static_cast<int>(two.textWidth("abc")),
             static_cast<int>(one.textWidth("abc")) * 2);
    // Wysokość wiersza listy wynika z czcionki, gdy nie podano jej wprost.
    CHECK(two.rowHeightOrDefault() > two.lineHeight());
}

// ---------------------------------------------------------------------------
// Widget i ekran
// ---------------------------------------------------------------------------

TEST("Widget: unieważnia obszar tylko przy realnej zmianie") {
    Screen screen;
    Label  label("start");
    label.setBounds(Rect(0, 0, 40, 10));
    REQUIRE(screen.add(label).has_value());
    screen.takeDirty();

    // Ta sama treść nie może unieważniać obszaru — inaczej telemetria
    // przychodząca co 100 ms przerysowywałaby ekran bez powodu.
    label.setText("start");
    CHECK(screen.takeDirty().empty());

    label.setText("inna");
    CHECK(screen.takeDirty() == Rect(0, 0, 40, 10));
}

TEST("Widget: przesunięcie unieważnia stare i nowe położenie") {
    Screen screen;
    Label  label("x");
    label.setBounds(Rect(0, 0, 10, 10));
    REQUIRE(screen.add(label).has_value());
    screen.takeDirty();

    label.setBounds(Rect(30, 0, 10, 10));
    // Bez starego położenia widżet zostawiłby po sobie ślad tam, skąd zniknął.
    CHECK(screen.takeDirty() == Rect(0, 0, 40, 10));
}

TEST("Ekran: przycinanie chroni sąsiadów") {
    resetWidgets();
    Canvas canvas;
    Screen screen;

    // Widżet celowo rysujący poza swoim prostokątem.
    struct Greedy : Widget {
        void draw(gfx::ISurface& s, const Theme&) override {
            s.fillRect(Rect(0, 0, 64, 32), colors::red);
        }
    } greedy;
    greedy.setBounds(Rect(0, 0, 10, 10));

    Label neighbour("x");
    neighbour.setBounds(Rect(40, 0, 20, 10));

    REQUIRE(screen.add(greedy).has_value());
    REQUIRE(screen.add(neighbour).has_value());

    const Theme theme = Theme::dark();
    screen.draw(canvas.fb, theme, canvas.fb.bounds());

    // Zachłanny widżet nie miał jak wyjść poza własny prostokąt.
    CHECK(canvas.at(5, 5) != Canvas::quantized(theme.background));
    CHECK(canvas.at(30, 5) == Canvas::quantized(theme.background));
}

TEST("Ekran: tło maluje ekran, nie widżety") {
    resetWidgets();
    Canvas canvas;
    Screen screen;

    const Theme theme = Theme::light();
    screen.draw(canvas.fb, theme, canvas.fb.bounds());

    // Każdy piksel ma kolor tła — widżet nie musi wiedzieć, co jest pod nim.
    CHECK(canvas.at(0, 0) != colors::black);
    CHECK(canvas.at(63, 31) == canvas.at(0, 0));
}

TEST("Ekran: dotyk trafia w widżet leżący wyżej") {
    Screen screen;
    Button lower("dolny");
    Button upper("górny");
    lower.setBounds(Rect(0, 0, 40, 20));
    upper.setBounds(Rect(10, 5, 20, 10));

    int lowerHits = 0, upperHits = 0;
    lower.setHandler([&] { ++lowerHits; });
    upper.setHandler([&] { ++upperHits; });

    REQUIRE(screen.add(lower).has_value());
    REQUIRE(screen.add(upper).has_value());  // dodany później, więc wyżej

    screen.dispatchPointer(PointerEvent{15, 8, PointerAction::Down, 0, 0});
    screen.dispatchPointer(PointerEvent{15, 8, PointerAction::Up, 0, 0});

    CHECK_EQ(upperHits, 1);
    CHECK_EQ(lowerHits, 0);
}

TEST("Ekran: limit widżetów i idempotentne dodawanie") {
    Screen screen;
    Label  labels[Screen::kMaxWidgets];

    for (auto& l : labels) REQUIRE(screen.add(l).has_value());
    CHECK_EQ(static_cast<int>(screen.widgetCount()), Screen::kMaxWidgets);

    // Ponowne dodanie tego samego widżetu nie zajmuje kolejnego miejsca.
    REQUIRE(screen.add(labels[0]).has_value());
    CHECK_EQ(static_cast<int>(screen.widgetCount()), Screen::kMaxWidgets);

    Label extra;
    CHECK(screen.add(extra).error() == Err::OutOfMemory);
}

TEST("Ekran: fokus obchodzi wyłącznie widżety, które go przyjmują") {
    Screen screen;
    Label  label("opis");
    Button first("a");
    Button second("b");
    label.setBounds(Rect(0, 0, 10, 10));
    first.setBounds(Rect(0, 10, 10, 10));
    second.setBounds(Rect(0, 20, 10, 10));

    REQUIRE(screen.add(label).has_value());
    REQUIRE(screen.add(first).has_value());
    REQUIRE(screen.add(second).has_value());

    CHECK(screen.focusedWidget() == nullptr);
    screen.focusNext();
    CHECK(screen.focusedWidget() == &first);
    screen.focusNext();
    CHECK(screen.focusedWidget() == &second);
    // Etykieta jest pomijana, a obieg wraca na początek.
    screen.focusNext();
    CHECK(screen.focusedWidget() == &first);
    CHECK(first.focused());
    CHECK(!second.focused());
}

// ---------------------------------------------------------------------------
// Stos ekranów
// ---------------------------------------------------------------------------

TEST("Stos ekranów: push i pop wołają onEnter i onExit") {
    struct Tracking : Screen {
        using Screen::Screen;
        int enters = 0, exits = 0;
        void onEnter() override { ++enters; }
        void onExit() override { ++exits; }
    };

    Tracking home("home");
    Tracking settings("settings");
    ScreenStack stack;

    REQUIRE(stack.push(home).has_value());
    CHECK_EQ(home.enters, 1);
    CHECK(stack.top() == &home);

    REQUIRE(stack.push(settings).has_value());
    CHECK_EQ(home.exits, 1);
    CHECK_EQ(settings.enters, 1);
    CHECK_EQ(static_cast<int>(stack.depth()), 2);

    REQUIRE(stack.pop().has_value());
    CHECK_EQ(settings.exits, 1);
    CHECK_EQ(home.enters, 2);
    CHECK(stack.top() == &home);
}

TEST("Stos ekranów: ekranu domowego nie da się zdjąć") {
    Screen      home("home");
    ScreenStack stack;
    REQUIRE(stack.push(home).has_value());

    // Bez ekranu domowego nie byłoby co pokazać.
    CHECK(stack.pop().error() == Err::NotSupported);
    CHECK_EQ(static_cast<int>(stack.depth()), 1);
}

TEST("Stos ekranów: home zdejmuje wszystko powyżej") {
    Screen home("home"), a("a"), b("b");
    ScreenStack stack;

    REQUIRE(stack.push(home).has_value());
    REQUIRE(stack.push(a).has_value());
    REQUIRE(stack.push(b).has_value());
    CHECK_EQ(static_cast<int>(stack.depth()), 3);

    REQUIRE(stack.home().has_value());
    CHECK_EQ(static_cast<int>(stack.depth()), 1);
    CHECK(stack.top() == &home);
}

TEST("Stos ekranów: zmiana ekranu wymusza pełne odświeżenie") {
    Screen home("home"), other("other");
    ScreenStack stack;
    const Rect full(0, 0, 64, 32);

    REQUIRE(stack.push(home).has_value());
    // Poprzednia zawartość nie ma nic wspólnego z nową — odświeżanie
    // częściowe zostawiłoby na ekranie fragmenty starego ekranu.
    CHECK(stack.takeDirty(full) == full);
    CHECK(stack.takeDirty(full).empty());

    REQUIRE(stack.push(other).has_value());
    CHECK(stack.takeDirty(full) == full);
}

TEST("Stos ekranów: limit głębokości") {
    Screen screens[ScreenStack::kMaxDepth];
    ScreenStack stack;
    for (auto& s : screens) REQUIRE(stack.push(s).has_value());

    Screen extra;
    CHECK(stack.push(extra).error() == Err::OutOfMemory);
}

// ---------------------------------------------------------------------------
// Widżety
// ---------------------------------------------------------------------------

TEST("Label: formatowanie wartości z jednostką") {
    Label label;
    label.setValue(21.53f, 1, "degC");
    CHECK_STR(label.text(), "21.5 degC");

    label.setValue(-3.0f, 0);
    CHECK_STR(label.text(), "-3");

    label.setValue(1.0f, 2, "V");
    CHECK_STR(label.text(), "1.00 V");
}

TEST("Label: wyrównanie zmienia położenie tekstu") {
    resetWidgets();
    const Theme theme = Theme::dark();

    Canvas left, right;
    Label  a("X"), b("X");
    a.setBounds(Rect(0, 0, 60, 10));
    b.setBounds(Rect(0, 0, 60, 10));
    b.setAlign(Align::Right);

    a.draw(left.fb, theme);
    b.draw(right.fb, theme);

    // Ten sam napis, dwa różne położenia — i oba coś narysowały.
    CHECK(left.litCount() > 0);
    CHECK_EQ(left.litCount(), right.litCount());
    CHECK(memcmp(left.buffer, right.buffer, sizeof(left.buffer)) != 0);
}

TEST("Bateria: poziom zmienia szerokość i barwę wypełnienia") {
    resetWidgets();
    const Theme theme = Theme::dark();

    Canvas full, low;
    BatteryIndicator a, b;
    a.setBounds(Rect(0, 0, 40, 16));
    b.setBounds(Rect(0, 0, 40, 16));

    a.setPercent(100);
    b.setPercent(10);
    a.draw(full.fb, theme);
    b.draw(low.fb, theme);

    CHECK(full.litCount() > low.litCount());

    // Poniżej progu ostrzeżenia wypełnienie zmienia barwę na alarmową.
    bool foundDanger = false;
    for (i16 y = 0; y < Canvas::kH && !foundDanger; ++y) {
        for (i16 x = 0; x < Canvas::kW; ++x) {
            if (low.at(x, y) == Canvas::quantized(theme.danger)) {
                foundDanger = true;
                break;
            }
        }
    }
    CHECK(foundDanger);
}

TEST("Bateria: wartość ponad zakres jest przycinana") {
    BatteryIndicator battery;
    battery.setPercent(200);
    CHECK_EQ(static_cast<int>(battery.percent()), 100);
}

TEST("Sygnał: liczba słupków wynika z siły sygnału") {
    SignalBars bars;
    bars.setConnected(true);

    bars.setRssi(-40);
    CHECK_EQ(static_cast<int>(bars.level()), 4);
    bars.setRssi(-60);
    CHECK_EQ(static_cast<int>(bars.level()), 3);
    bars.setRssi(-70);
    CHECK_EQ(static_cast<int>(bars.level()), 2);
    bars.setRssi(-80);
    CHECK_EQ(static_cast<int>(bars.level()), 1);
    bars.setRssi(-95);
    CHECK_EQ(static_cast<int>(bars.level()), 0);

    // Brak łącza to zero słupków niezależnie od ostatniego pomiaru.
    bars.setRssi(-40);
    bars.setConnected(false);
    CHECK_EQ(static_cast<int>(bars.level()), 0);
}

TEST("Wykres: pamięta ograniczoną liczbę próbek") {
    Sparkline chart;

    for (int i = 0; i < HYDRA_UI_SPARKLINE_POINTS + 10; ++i) {
        chart.push(static_cast<float>(i));
    }
    CHECK_EQ(static_cast<int>(chart.count()), HYDRA_UI_SPARKLINE_POINTS);

    // Najstarsze próbki wypadły z bufora pierścieniowego.
    CHECK(chart.latest() > static_cast<float>(HYDRA_UI_SPARKLINE_POINTS));
    CHECK(chart.minimum() >= 10.0f);
}

TEST("Wykres: zakres dobiera się do danych, o ile nie ustawiono go wprost") {
    Sparkline chart;
    chart.push(10.0f);
    chart.push(20.0f);
    chart.push(15.0f);

    CHECK(chart.minimum() == 10.0f);
    CHECK(chart.maximum() == 20.0f);
    CHECK(chart.latest() == 15.0f);

    chart.clear();
    CHECK_EQ(static_cast<int>(chart.count()), 0);
}

TEST("Wykres: stała wartość nie wywraca skalowania") {
    resetWidgets();
    Canvas canvas;
    const Theme theme = Theme::dark();

    Sparkline chart;
    chart.setBounds(Rect(0, 0, 40, 20));
    for (int i = 0; i < 10; ++i) chart.push(5.0f);

    // Zakres zerowy oznaczałby dzielenie przez zero; przebieg ma się narysować
    // w połowie wysokości, a nie przykleić do krawędzi.
    chart.draw(canvas.fb, theme);
    CHECK(canvas.litCount() > 0);

    bool middleRowLit = false;
    for (i16 x = 0; x < 40; ++x) {
        if (canvas.at(x, 10) != colors::black) middleRowLit = true;
    }
    CHECK(middleRowLit);
}

TEST("Przycisk: wywołanie dopiero przy oderwaniu palca w obszarze") {
    Button button("OK");
    button.setBounds(Rect(0, 0, 40, 20));

    int fired = 0;
    button.setHandler([&] { ++fired; });

    CHECK(button.onPointer(PointerEvent{5, 5, PointerAction::Down, 0, 0}));
    CHECK(button.pressed());
    CHECK_EQ(fired, 0);  // samo naciśnięcie jeszcze nic nie robi

    CHECK(button.onPointer(PointerEvent{5, 5, PointerAction::Up, 0, 0}));
    CHECK(!button.pressed());
    CHECK_EQ(fired, 1);
}

TEST("Przycisk: zsunięcie palca poza obszar wycofuje naciśnięcie") {
    Button button("OK");
    button.setBounds(Rect(0, 0, 40, 20));

    int fired = 0;
    button.setHandler([&] { ++fired; });

    button.onPointer(PointerEvent{5, 5, PointerAction::Down, 0, 0});
    // Użytkownik zmienił zdanie i zsunął palec poza przycisk.
    button.onPointer(PointerEvent{100, 100, PointerAction::Up, 0, 0});
    CHECK_EQ(fired, 0);
    CHECK(!button.pressed());
}

TEST("Przycisk: wyłączony nie reaguje i nie przyjmuje fokusu") {
    Button button("OK");
    button.setBounds(Rect(0, 0, 40, 20));

    int fired = 0;
    button.setHandler([&] { ++fired; });
    button.setEnabled(false);

    CHECK(!button.focusable());
    CHECK(!button.onPointer(PointerEvent{5, 5, PointerAction::Down, 0, 0}));
    CHECK_EQ(fired, 0);
}

TEST("Przycisk: naciśnięcie enkodera z fokusem uruchamia akcję") {
    Button button("OK");
    int    fired = 0;
    button.setHandler([&] { ++fired; });

    // Bez fokusu enkoder nie ma jak trafić w ten przycisk.
    CHECK(!button.onEncoder(EncoderEvent{0, true}));
    CHECK_EQ(fired, 0);

    button.setFocused(true);
    CHECK(button.onEncoder(EncoderEvent{0, true}));
    CHECK_EQ(fired, 1);
}

TEST("Lista: enkoder przesuwa zaznaczenie i zatrzymuje się na krańcach") {
    const ListView::Item items[] = {{"Wi-Fi", "dom"}, {"Jasność", "80%"}, {"Język", "pl"}};
    ListView list;
    list.setBounds(Rect(0, 0, 64, 32));
    list.setItems(items, 3);
    list.setFocused(true);

    CHECK_EQ(static_cast<int>(list.selected()), 0);

    CHECK(list.onEncoder(EncoderEvent{1, false}));
    CHECK_EQ(static_cast<int>(list.selected()), 1);

    list.onEncoder(EncoderEvent{5, false});
    // Zatrzymanie zamiast zawijania: przeskok z końca na początek myli.
    CHECK_EQ(static_cast<int>(list.selected()), 2);

    list.onEncoder(EncoderEvent{-10, false});
    CHECK_EQ(static_cast<int>(list.selected()), 0);
}

TEST("Lista: naciśnięcie enkodera zgłasza wybór") {
    const ListView::Item items[] = {{"a", ""}, {"b", ""}};
    ListView list;
    list.setItems(items, 2);
    list.setFocused(true);

    int selected = -1;
    list.setSelectHandler([&](u8 index) { selected = index; });

    list.onEncoder(EncoderEvent{1, false});
    CHECK(list.onEncoder(EncoderEvent{0, true}));
    CHECK_EQ(selected, 1);
}

TEST("Lista: przewijanie pokazuje zaznaczoną pozycję") {
    resetWidgets();
    const Theme theme = Theme::mono();

    ListView::Item items[10];
    for (auto& item : items) item = ListView::Item{"pozycja", ""};

    Canvas   canvas;
    ListView list;
    list.setBounds(Rect(0, 0, 64, 32));
    list.setItems(items, 10);
    list.setFocused(true);

    // Rysowanie ustala, ile wierszy się mieści.
    list.draw(canvas.fb, theme);
    const u8 rows = list.visibleRows(theme);
    CHECK(rows > 0);
    CHECK(rows < 10);
    CHECK_EQ(static_cast<int>(list.firstVisible()), 0);

    list.onEncoder(EncoderEvent{9, false});
    CHECK_EQ(static_cast<int>(list.selected()), 9);
    // Lista przewinęła się dokładnie tyle, ile trzeba, by pokazać zaznaczenie.
    CHECK_EQ(static_cast<int>(list.firstVisible()), 10 - rows);
}

TEST("Joystick: wychylenie w promilach i powrót na środek") {
    Joystick stick;
    stick.setBounds(Rect(0, 0, 40, 40));

    i16 lastX = 0, lastY = 0;
    int moves = 0;
    stick.setHandler([&](i16 x, i16 y) {
        lastX = x;
        lastY = y;
        ++moves;
    });

    // Prawa krawędź to pełne wychylenie w tę samą jednostkę, w której HAL
    // przyjmuje wypełnienie PWM.
    stick.onPointer(PointerEvent{40, 20, PointerAction::Down, 0, 0});
    CHECK(stick.active());
    CHECK_EQ(static_cast<int>(lastX), 1000);
    CHECK_EQ(static_cast<int>(lastY), 0);

    stick.onPointer(PointerEvent{0, 0, PointerAction::Move, 0, 0});
    CHECK_EQ(static_cast<int>(lastX), -1000);
    CHECK_EQ(static_cast<int>(lastY), -1000);

    // Oderwanie palca zeruje wychylenie: robot nie może jechać dalej dlatego,
    // że ktoś puścił ekran.
    stick.onPointer(PointerEvent{0, 0, PointerAction::Up, 0, 0});
    CHECK(!stick.active());
    CHECK_EQ(static_cast<int>(stick.x()), 0);
    CHECK_EQ(static_cast<int>(stick.y()), 0);
}

TEST("Joystick: bez samocentrowania wychylenie zostaje") {
    Joystick stick;
    stick.setBounds(Rect(0, 0, 40, 40));
    stick.setSelfCentering(false);

    stick.onPointer(PointerEvent{40, 20, PointerAction::Down, 0, 0});
    stick.onPointer(PointerEvent{40, 20, PointerAction::Up, 0, 0});
    CHECK_EQ(static_cast<int>(stick.x()), 1000);
}

// ---------------------------------------------------------------------------
// Wiązanie danych
// ---------------------------------------------------------------------------

TEST("Wiązanie: zdarzenie z magistrali trafia na widżet przez kolejkę") {
    resetWidgets();

    RenderQueue queue;
    BindingHub  bindings(queue);
    Label       label("brak");

    REQUIRE(bindings
                .bind<Label, Reading>(label,
                                      [](Label& l, const Reading& r) {
                                          l.setValue(r.value, 1, "degC");
                                      })
                .has_value());

    EventBus::publish(Reading{21.5f});

    // Zdarzenie przyszło w kontekście nadawcy — widżetu nie wolno tam dotknąć,
    // więc do tej chwili nic się nie zmieniło.
    CHECK_STR(label.text(), "brak");
    CHECK_EQ(static_cast<int>(queue.pending()), 1);

    // Dopiero przebieg kolejki, czyli task ui.render, nakłada wartość.
    CHECK_EQ(static_cast<int>(queue.drain()), 1);
    CHECK_STR(label.text(), "21.5 degC");
    CHECK_EQ(static_cast<int>(bindings.stats().applied), 1);
}

TEST("Wiązanie: nadmiarowe aktualizacje są scalane") {
    resetWidgets();

    RenderQueue queue;
    BindingHub  bindings(queue);
    Label       label;

    REQUIRE(bindings
                .bind<Label, Reading>(label,
                                      [](Label& l, const Reading& r) {
                                          l.setValue(r.value, 0);
                                      })
                .has_value());

    // Czujnik nadający sto razy na sekundę przy trzydziestu klatkach zalałby
    // kolejkę; zamiast tego nadpisuje wartość, która nie zdążyła się pokazać.
    for (int i = 0; i < 50; ++i) EventBus::publish(Reading{static_cast<float>(i)});

    CHECK_EQ(static_cast<int>(queue.pending()), 1);
    CHECK_EQ(static_cast<int>(bindings.stats().received), 50);
    CHECK_EQ(static_cast<int>(bindings.stats().coalesced), 49);

    queue.drain();
    // Na ekranie ląduje stan najnowszy, a nie zaległy.
    CHECK_STR(label.text(), "49");
}

TEST("Wiązanie: kilka widżetów naraz, każdy z własnym zdarzeniem") {
    resetWidgets();

    RenderQueue queue;
    BindingHub  bindings(queue);
    Label       label;
    Sparkline   chart;

    REQUIRE(bindings
                .bind<Label, Reading>(
                    label, [](Label& l, const Reading& r) { l.setValue(r.value, 0); })
                .has_value());
    REQUIRE(bindings
                .bind<Sparkline, Reading>(
                    chart, [](Sparkline& c, const Reading& r) { c.push(r.value); })
                .has_value());

    EventBus::publish(Reading{7.0f});
    CHECK_EQ(static_cast<int>(queue.drain()), 2);

    CHECK_STR(label.text(), "7");
    CHECK_EQ(static_cast<int>(chart.count()), 1);
    CHECK_EQ(static_cast<int>(bindings.count()), 2);
}

TEST("Wiązanie: limit i błędne argumenty") {
    resetWidgets();

    RenderQueue queue;
    BindingHub  bindings(queue);
    Label       labels[HYDRA_UI_MAX_BINDINGS + 1];

    auto apply = [](Label& l, const Reading& r) { l.setValue(r.value, 0); };
    for (int i = 0; i < HYDRA_UI_MAX_BINDINGS; ++i) {
        REQUIRE(bindings.bind<Label, Reading>(labels[i], apply).has_value());
    }
    CHECK(bindings.bind<Label, Reading>(labels[HYDRA_UI_MAX_BINDINGS], apply).error() ==
          Err::OutOfMemory);
    CHECK(bindings.bind<Label, Reading>(labels[0], nullptr).error() == Err::BadArgument);
}

TEST("Wiązanie: subskrypcje znikają razem z obiektem") {
    resetWidgets();

    RenderQueue queue;
    Label       label;

    {
        BindingHub bindings(queue);
        REQUIRE(bindings
                    .bind<Label, Reading>(
                        label, [](Label& l, const Reading& r) { l.setValue(r.value, 0); })
                    .has_value());
        CHECK_EQ(static_cast<int>(EventBus::stats().subs), 1);
    }

    // Subskrypcja przeżywająca obiekt wołałaby metodę na zwolnionej pamięci.
    CHECK_EQ(static_cast<int>(EventBus::stats().subs), 0);
    EventBus::publish(Reading{1.0f});
    CHECK_EQ(static_cast<int>(queue.pending()), 0);
}
