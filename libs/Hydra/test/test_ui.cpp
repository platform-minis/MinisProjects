/**
 * Testy potoku renderowania i wejścia (etap 4b).
 *
 * Panel atrapowy pozwala sprawdzić to, czego na sprzęcie nie widać: jaki
 * dokładnie obszar został wysłany, ile klatek pominięto i czy podwójne
 * buforowanie nie zostawia śladów poprzedniej zawartości.
 */

#include "hydra_test.hpp"

#include <atomic>

#include "hydra/core/App.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/ui/Mock.hpp"
#include "hydra/ui/UiModule.hpp"

using namespace hydra;
using namespace hydra::ui;
using namespace hydra::gfx;

namespace {

void resetUi() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

}  // namespace

// ---------------------------------------------------------------------------
// Kolejka poleceń
// ---------------------------------------------------------------------------

TEST("Kolejka: polecenia wykonują się w kolejności zgłoszenia") {
    RenderQueue q;
    int order[4] = {};
    int idx      = 0;

    for (int i = 1; i <= 3; ++i) {
        REQUIRE(q.post([&order, &idx, i] { order[idx++] = i; }).has_value());
    }
    CHECK_EQ(static_cast<int>(q.pending()), 3);

    CHECK_EQ(static_cast<int>(q.drain()), 3);
    CHECK_EQ(order[0], 1);
    CHECK_EQ(order[1], 2);
    CHECK_EQ(order[2], 3);
    CHECK_EQ(static_cast<int>(q.pending()), 0);
}

TEST("Kolejka: puste polecenie jest odrzucane") {
    RenderQueue q;
    CHECK(q.post(RenderQueue::Command{}).error() == Err::BadArgument);
    CHECK_EQ(static_cast<int>(q.pending()), 0);
}

TEST("Kolejka: przepełnienie porzuca najstarsze, nie najnowsze") {
    RenderQueue q;
    int executed[HYDRA_UI_QUEUE_DEPTH + 2] = {};
    int idx = 0;

    for (int i = 0; i < HYDRA_UI_QUEUE_DEPTH; ++i) {
        REQUIRE(q.post([&executed, &idx, i] { executed[idx++] = i; }).has_value());
    }
    // Kolejka pełna: kolejne zgłoszenie mieści się kosztem najstarszego.
    auto overflowed = q.post([&executed, &idx] { executed[idx++] = 999; });
    CHECK(overflowed.error() == Err::WouldBlock);
    CHECK_EQ(static_cast<int>(q.dropped()), 1);

    q.drain();
    // Pierwsze polecenie przepadło, ostatnie się wykonało — w interfejsie
    // świeższa informacja jest cenniejsza od starszej.
    CHECK_EQ(executed[0], 1);
    CHECK_EQ(executed[idx - 1], 999);
}

TEST("Kolejka: polecenie może zakolejkować kolejne bez zakleszczenia") {
    RenderQueue q;
    int inner = 0;

    REQUIRE(q.post([&q, &inner] {
                // Wykonanie pod blokadą kolejki byłoby tu zakleszczeniem.
                q.post([&inner] { inner = 42; });
            }).has_value());

    CHECK_EQ(static_cast<int>(q.drain(1)), 1);
    CHECK_EQ(inner, 0);            // zagnieżdżone czeka na kolejny przebieg
    CHECK_EQ(static_cast<int>(q.drain()), 1);
    CHECK_EQ(inner, 42);
}

TEST("Kolejka: limit poleceń na przebieg jest respektowany") {
    RenderQueue q;
    int count = 0;
    for (int i = 0; i < 5; ++i) REQUIRE(q.post([&count] { ++count; }).has_value());

    CHECK_EQ(static_cast<int>(q.drain(2)), 2);
    CHECK_EQ(count, 2);
    CHECK_EQ(static_cast<int>(q.pending()), 3);
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

TEST("Renderer: pierwsza klatka przerysowuje wszystko") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;

    REQUIRE(renderer.init(display).has_value());
    CHECK(display.begun());
    // Zawartość bufora po starcie jest nieokreślona — pierwsza klatka
    // nie ma prawa niczego pominąć.
    CHECK(renderer.needsRedraw());

    int draws = 0;
    renderer.setDraw([&](ISurface&, Rect area) {
        ++draws;
        CHECK(area == Rect(0, 0, mock::MockDisplay::kWidth, mock::MockDisplay::kHeight));
    });

    REQUIRE(renderer.renderFrame(0).has_value());
    CHECK_EQ(draws, 1);
    CHECK_EQ(static_cast<int>(display.presents()), 1);
    CHECK(!renderer.needsRedraw());
}

TEST("Renderer: klatka bez zmian nie rysuje i nie transferuje") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;
    REQUIRE(renderer.init(display).has_value());

    int draws = 0;
    renderer.setDraw([&](ISurface&, Rect) { ++draws; });

    REQUIRE(renderer.renderFrame(0).has_value());
    CHECK_EQ(draws, 1);

    // Nieruchomy ekran statusu to stan, w którym urządzenie spędza większość
    // życia — nie może kosztować ani transferu, ani rysowania.
    for (Millis t = 33; t < 330; t += 33) REQUIRE(renderer.renderFrame(t).has_value());

    CHECK_EQ(draws, 1);
    CHECK_EQ(static_cast<int>(display.presents()), 1);
    CHECK_EQ(static_cast<int>(renderer.stats().skipped), 9);
    CHECK_EQ(static_cast<int>(renderer.stats().frames), 1);
}

TEST("Renderer: unieważnienie fragmentu odświeża tylko jego") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;
    REQUIRE(renderer.init(display).has_value());
    renderer.setDraw([](ISurface&, Rect) {});
    REQUIRE(renderer.renderFrame(0).has_value());

    Rect drawnArea;
    renderer.setDraw([&](ISurface&, Rect area) { drawnArea = area; });

    renderer.invalidate(Rect(10, 5, 8, 4));
    REQUIRE(renderer.renderFrame(33).has_value());

    CHECK(drawnArea == Rect(10, 5, 8, 4));
    CHECK(display.lastArea() == Rect(10, 5, 8, 4));
    CHECK_EQ(static_cast<int>(display.presents()), 2);
}

TEST("Renderer: kolejne unieważnienia sumują się w jeden obszar") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;
    REQUIRE(renderer.init(display).has_value());
    renderer.setDraw([](ISurface&, Rect) {});
    REQUIRE(renderer.renderFrame(0).has_value());

    Rect drawnArea;
    renderer.setDraw([&](ISurface&, Rect area) { drawnArea = area; });

    renderer.invalidate(Rect(2, 2, 4, 4));
    renderer.invalidate(Rect(20, 10, 4, 4));
    REQUIRE(renderer.renderFrame(33).has_value());

    // Jeden transfer obejmujący oba fragmenty zamiast dwóch transferów —
    // na wolnym SPI to zwykle tańsze niż dwie transakcje.
    CHECK(drawnArea == Rect(2, 2, 22, 12));
}

TEST("Renderer: przycinanie chroni obszar poza unieważnionym") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;
    REQUIRE(renderer.init(display).has_value());

    // Pierwsza klatka maluje całość na biało.
    renderer.setDraw([](ISurface& s, Rect) { s.fill(colors::white); });
    REQUIRE(renderer.renderFrame(0).has_value());
    CHECK(display.framebuffer().pixelAt(0, 0).value_or(colors::black) == colors::white);

    // Druga próbuje zamalować wszystko na czarno, ale unieważniony jest
    // wyłącznie mały fragment.
    renderer.invalidate(Rect(4, 4, 4, 4));
    renderer.setDraw([](ISurface& s, Rect) { s.fill(colors::black); });
    REQUIRE(renderer.renderFrame(33).has_value());

    CHECK(display.framebuffer().pixelAt(5, 5).value_or(colors::white) == colors::black);
    // Poza obszarem zawartość została nietknięta — bez tego widżet mógłby
    // zamazać sąsiada przy każdym własnym odświeżeniu.
    CHECK(display.framebuffer().pixelAt(0, 0).value_or(colors::black) == colors::white);
    CHECK(display.framebuffer().pixelAt(20, 20).value_or(colors::black) == colors::white);
}

TEST("Renderer: panel bez odświeżania częściowego dostaje pełną ramkę") {
    resetUi();
    mock::MockDisplay display;
    display.setPartial(false);

    Renderer renderer;
    REQUIRE(renderer.init(display).has_value());
    renderer.setDraw([](ISurface&, Rect) {});
    REQUIRE(renderer.renderFrame(0).has_value());

    renderer.invalidate(Rect(10, 10, 2, 2));
    REQUIRE(renderer.renderFrame(33).has_value());

    // Fragment zostałby wysłany w złe miejsce albo zignorowany — panel
    // bez tej możliwości musi dostać wszystko.
    CHECK(display.lastArea() == Rect(0, 0, mock::MockDisplay::kWidth,
                                     mock::MockDisplay::kHeight));
}

TEST("Renderer: przy dwóch buforach odświeża także obszar poprzedniej klatki") {
    resetUi();
    mock::MockDisplay display;
    display.setBufferCount(2);

    Renderer renderer;
    REQUIRE(renderer.init(display).has_value());
    renderer.setDraw([](ISurface&, Rect) {});
    REQUIRE(renderer.renderFrame(0).has_value());

    Rect drawnArea;
    renderer.setDraw([&](ISurface&, Rect area) { drawnArea = area; });

    renderer.invalidate(Rect(2, 2, 4, 4));
    REQUIRE(renderer.renderFrame(33).has_value());
    // Poprzednia klatka objęła całą powierzchnię, więc i ta musi.
    CHECK(drawnArea == Rect(0, 0, mock::MockDisplay::kWidth, mock::MockDisplay::kHeight));

    renderer.invalidate(Rect(20, 10, 4, 4));
    REQUIRE(renderer.renderFrame(66).has_value());
    // Bufor, do którego teraz rysujemy, pamięta stan sprzed dwóch klatek —
    // pominięcie obszaru (2,2,4,4) zostawiłoby w nim ślad poprzedniej treści.
    CHECK(drawnArea == Rect(2, 2, 22, 12));
}

TEST("Renderer: polecenia z kolejki wykonują się przed rysowaniem") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;
    REQUIRE(renderer.init(display).has_value());
    renderer.setDraw([](ISurface&, Rect) {});
    REQUIRE(renderer.renderFrame(0).has_value());

    Rect drawnArea;
    bool commandRan = false;
    renderer.setDraw([&](ISurface&, Rect area) { drawnArea = area; });

    // Polecenie unieważnia obszar — musi zdążyć przed wyznaczeniem tego,
    // co zostanie przerysowane, inaczej zmiana trafiłaby dopiero do następnej
    // klatki i interfejs reagowałby z opóźnieniem.
    REQUIRE(renderer.queue()
                .post([&] {
                    commandRan = true;
                    renderer.invalidate(Rect(1, 1, 6, 6));
                })
                .has_value());

    REQUIRE(renderer.renderFrame(33).has_value());
    CHECK(commandRan);
    CHECK(drawnArea == Rect(1, 1, 6, 6));
    CHECK_EQ(static_cast<int>(renderer.stats().commands), 1);
}

TEST("Renderer: nieudany transfer zostawia obszar do ponowienia") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;
    REQUIRE(renderer.init(display).has_value());

    int draws = 0;
    renderer.setDraw([&](ISurface&, Rect) { ++draws; });
    display.failNextPresent(Err::IoError);

    CHECK(renderer.renderFrame(0).error() == Err::IoError);
    CHECK_EQ(draws, 1);
    // Bez tego na ekranie zostałaby treść narysowana w połowie, a renderer
    // uznałby ją za aktualną.
    CHECK(renderer.needsRedraw());

    REQUIRE(renderer.renderFrame(33).has_value());
    CHECK_EQ(draws, 2);
    CHECK(!renderer.needsRedraw());
}

TEST("Renderer: obszar naruszony ponad zamierzony i tak jest wysyłany") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;
    REQUIRE(renderer.init(display).has_value());
    renderer.setDraw([](ISurface&, Rect) {});
    REQUIRE(renderer.renderFrame(0).has_value());

    // Funkcja rysująca wychodzi poza obszar, jaki zadeklarowano.
    renderer.invalidate(Rect(4, 4, 4, 4));
    renderer.setDraw([](ISurface& s, Rect) {
        s.resetClip();  // celowo omija przycinanie
        s.fillRect(Rect(0, 0, 40, 20), colors::red);
    });
    REQUIRE(renderer.renderFrame(33).has_value());

    // Transfer obejmuje to, co naprawdę zostało zabrudzone — inaczej reszta
    // zmian zostałaby na ekranie niewidoczna do następnego odświeżenia.
    CHECK(display.lastArea().w >= 40);
    CHECK(display.lastArea().h >= 20);
}

TEST("Renderer: statystyki liczą klatki, pominięcia i czasy") {
    resetUi();
    mock::MockDisplay display;
    Renderer          renderer;
    Renderer::Config  cfg;
    cfg.publishFrameEvents = true;
    REQUIRE(renderer.init(display, cfg).has_value());

    int  frames = 0;
    auto sub    = EventBus::subscribe<FrameRendered>([&](const FrameRendered&) { ++frames; });
    REQUIRE(sub.has_value());

    renderer.setDraw([](ISurface& s, Rect) { s.fill(colors::blue); });
    REQUIRE(renderer.renderFrame(0).has_value());
    REQUIRE(renderer.renderFrame(33).has_value());  // pominięta

    CHECK_EQ(static_cast<int>(renderer.stats().frames), 1);
    CHECK_EQ(static_cast<int>(renderer.stats().skipped), 1);
    CHECK_EQ(frames, 1);
}

TEST("Renderer: bez inicjalizacji zgłasza błąd zamiast rysować w pustkę") {
    Renderer renderer;
    CHECK(renderer.renderFrame(0).error() == Err::NotInitialized);
    CHECK(!renderer.needsRedraw());
}

// ---------------------------------------------------------------------------
// Wejście
// ---------------------------------------------------------------------------

TEST("Wejście: dotknięcie, przesunięcie i oderwanie") {
    resetUi();
    mock::MockPointer pointer;
    InputRouter       router;
    router.attachPointer(pointer);
    REQUIRE(router.begin().has_value());

    PointerEvent last{};
    int          events = 0;
    auto sub = EventBus::subscribe<PointerEvent>([&](const PointerEvent& e) {
        last = e;
        ++events;
    });
    REQUIRE(sub.has_value());

    // Pierwszy odczyt tylko ustala punkt odniesienia — inaczej ekran dotknięty
    // w chwili startu dałby fałszywe dotknięcie.
    pointer.set(10, 20, true);
    CHECK_EQ(static_cast<int>(router.poll(0)), 0);
    CHECK_EQ(events, 0);

    pointer.set(10, 20, false);
    CHECK_EQ(static_cast<int>(router.poll(10)), 1);
    CHECK(last.action == PointerAction::Up);

    pointer.set(30, 40, true);
    CHECK_EQ(static_cast<int>(router.poll(20)), 1);
    CHECK(last.action == PointerAction::Down);
    CHECK_EQ(static_cast<int>(last.x), 30);
    CHECK_EQ(static_cast<int>(last.dx), 0);  // przy dotknięciu nie ma przesunięcia

    pointer.set(35, 44, true);
    CHECK_EQ(static_cast<int>(router.poll(30)), 1);
    CHECK(last.action == PointerAction::Move);
    CHECK_EQ(static_cast<int>(last.dx), 5);
    CHECK_EQ(static_cast<int>(last.dy), 4);
}

TEST("Wejście: oderwanie zgłaszane w ostatnim punkcie kontaktu") {
    resetUi();
    mock::MockPointer pointer;
    InputRouter       router;
    router.attachPointer(pointer);

    PointerEvent last{};
    auto sub = EventBus::subscribe<PointerEvent>([&](const PointerEvent& e) { last = e; });
    REQUIRE(sub.has_value());

    pointer.set(0, 0, false);
    router.poll(0);
    pointer.set(50, 60, true);
    router.poll(10);

    // Panel po oderwaniu potrafi zwrócić śmieciowe współrzędne, a to na nich
    // opiera się trafienie w przycisk.
    pointer.set(-999, -999, false);
    router.poll(20);
    CHECK(last.action == PointerAction::Up);
    CHECK_EQ(static_cast<int>(last.x), 50);
    CHECK_EQ(static_cast<int>(last.y), 60);
}

TEST("Wejście: brak nowego odczytu nie jest awarią") {
    resetUi();
    mock::MockPointer pointer;
    InputRouter       router;
    router.attachPointer(pointer);

    int  events = 0;
    auto sub    = EventBus::subscribe<PointerEvent>([&](const PointerEvent&) { ++events; });
    REQUIRE(sub.has_value());

    pointer.set(0, 0, false);
    router.poll(0);
    pointer.set(10, 10, true);
    pointer.reportNoData();
    CHECK_EQ(static_cast<int>(router.poll(10)), 0);
    CHECK_EQ(events, 0);

    // Kolejny odczyt już dochodzi.
    CHECK_EQ(static_cast<int>(router.poll(20)), 1);
}

TEST("Wejście: enkoder zgłasza różnicę, nie pozycję") {
    resetUi();
    mock::MockEncoder encoder;
    InputRouter       router;
    router.attachEncoder(encoder);

    EncoderEvent last{};
    int          events = 0;
    auto sub = EventBus::subscribe<EncoderEvent>([&](const EncoderEvent& e) {
        last = e;
        ++events;
    });
    REQUIRE(sub.has_value());

    encoder.setPosition(1000);
    CHECK_EQ(static_cast<int>(router.poll(0)), 0);  // punkt odniesienia

    encoder.setPosition(1003);
    CHECK_EQ(static_cast<int>(router.poll(10)), 1);
    // Subskrybent nie musi pamiętać poprzedniej wartości ani radzić sobie
    // z przepełnieniem licznika.
    CHECK_EQ(static_cast<int>(last.delta), 3);

    encoder.setPosition(998);
    router.poll(20);
    CHECK_EQ(static_cast<int>(last.delta), -5);

    // Brak ruchu to brak zdarzenia.
    CHECK_EQ(static_cast<int>(router.poll(30)), 0);
    CHECK_EQ(events, 2);

    encoder.setPressed(true);
    CHECK_EQ(static_cast<int>(router.poll(40)), 1);
    CHECK(last.pressed);
    CHECK_EQ(static_cast<int>(last.delta), 0);
}

TEST("Wejście: przycisk niesie czas przytrzymania przy zwolnieniu") {
    resetUi();
    mock::MockButtons buttons;
    InputRouter       router;
    router.attachButtons(buttons);

    ButtonEvent last{};
    int         events = 0;
    auto sub = EventBus::subscribe<ButtonEvent>([&](const ButtonEvent& e) {
        last = e;
        ++events;
    });
    REQUIRE(sub.has_value());

    router.poll(0);  // punkt odniesienia
    CHECK_EQ(events, 0);

    buttons.set(1, true);
    CHECK_EQ(static_cast<int>(router.poll(100)), 1);
    CHECK_EQ(static_cast<int>(last.id), 1);
    CHECK(last.pressed);
    CHECK_EQ(static_cast<int>(last.heldMs), 0);

    buttons.set(1, false);
    CHECK_EQ(static_cast<int>(router.poll(1350)), 1);
    CHECK(!last.pressed);
    // Czas mierzy framework — subskrybent dostaje go razem ze zdarzeniem.
    CHECK_EQ(static_cast<int>(last.heldMs), 1250);
}

TEST("Wejście: kilka urządzeń naraz") {
    resetUi();
    mock::MockPointer pointer;
    mock::MockEncoder encoder;
    mock::MockButtons buttons;

    InputRouter router;
    router.attachPointer(pointer);
    router.attachEncoder(encoder);
    router.attachButtons(buttons);
    REQUIRE(router.begin().has_value());

    router.poll(0);

    pointer.set(5, 5, true);
    encoder.setPosition(7);
    buttons.set(0, true);
    CHECK_EQ(static_cast<int>(router.poll(10)), 3);

    CHECK_EQ(static_cast<int>(router.lastPointer().x), 5);
}

// ---------------------------------------------------------------------------
// Moduł
// ---------------------------------------------------------------------------

TEST("UiModule: pętla łączy wejście, kolejkę i rysowanie") {
    resetUi();
    mock::MockDisplay display;
    mock::MockPointer pointer;

    UiModule ui(display);
    ui.attachPointer(pointer);

    UiModule::Config cfg;
    cfg.framePeriodMs = 20;
    REQUIRE(ui.configure(cfg).has_value());
    REQUIRE(ui.init().has_value());

    int draws = 0;
    ui.renderer().setDraw([&](ISurface&, Rect) { ++draws; });

    pointer.set(0, 0, false);
    ui.step(0);
    CHECK_EQ(draws, 1);
    CHECK_EQ(static_cast<int>(ui.stats().ticks), 1);

    // Bez zmian klatka jest pomijana, ale wejście dalej jest odpytywane.
    pointer.set(9, 9, true);
    ui.step(20);
    CHECK_EQ(draws, 1);
    CHECK_EQ(static_cast<int>(ui.stats().inputEvents), 1);

    // Zmiana zgłoszona z innego taska idzie przez kolejkę.
    REQUIRE(ui.queue().post([&ui] { ui.renderer().invalidate(); }).has_value());
    ui.step(40);
    CHECK_EQ(draws, 2);
}

TEST("UiModule: task ui.render rysuje cyklicznie") {
    resetUi();
    mock::MockDisplay display;

    UiModule         ui(display);
    UiModule::Config cfg;
    cfg.framePeriodMs = 10;
    cfg.renderer.skipIdleFrames = false;  // wymuszamy stałe odświeżanie
    REQUIRE(ui.configure(cfg).has_value());

    std::atomic<int> draws{0};
    REQUIRE(ui.init().has_value());
    ui.renderer().setDraw([&](ISurface&, Rect) { ++draws; });

    REQUIRE(ui.start().has_value());
    rtos::delayMs(120);
    ui.stop();

    // ~120 ms przy okresie 10 ms; luz na szum schedulera hosta.
    CHECK(draws.load() >= 5);
    CHECK_EQ(static_cast<int>(display.presents()), draws.load());
}

TEST("UiModule: błędna konfiguracja jest odrzucana") {
    resetUi();
    mock::MockDisplay display;
    UiModule          ui(display);

    UiModule::Config cfg;
    cfg.framePeriodMs = 0;
    CHECK(ui.configure(cfg).error() == Err::BadArgument);
}

// ---------------------------------------------------------------------------
// Panel z powierzchni
// ---------------------------------------------------------------------------

TEST("SurfaceDisplay: dowolna powierzchnia gfx staje się panelem") {
    resetUi();

    static u8   pixels[Framebuffer::bytesNeeded(16, 8, PixelFormat::Mono1)] = {};
    Framebuffer fb;
    REQUIRE(fb.attach(ByteSpan{pixels, sizeof(pixels)}, 16, 8, PixelFormat::Mono1)
                .has_value());

    int flushes = 0;
    fb.setPresent([&](CByteSpan, Size, PixelFormat) {
        ++flushes;
        return ok();
    });

    // To dzięki temu każdy adapter z gfx/adapters/ jest od razu wyświetlaczem
    // modułu UI, bez pisania osobnego sterownika.
    SurfaceDisplay display(fb, "oled");
    Renderer       renderer;
    REQUIRE(renderer.init(display).has_value());

    CHECK_STR(display.name(), "oled");
    CHECK(display.size() == Size(16, 8));
    // Bez wiedzy o panelu zakładamy najprostszy przypadek: pełna ramka.
    CHECK(!display.supportsPartial());

    renderer.setDraw([](ISurface& s, Rect) { s.fill(colors::white); });
    REQUIRE(renderer.renderFrame(0).has_value());
    CHECK_EQ(flushes, 1);
}
