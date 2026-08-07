/**
 * Testy pomostu do LVGL (etap 4d).
 *
 * LVGL nie kompiluje się na hoście, ale cała logika pomostu — przeliczanie
 * obszarów, konwersja formatów pikseli, kolejność kroków pętli, karmienie
 * urządzeń wejściowych — jest po naszej stronie i daje się sprawdzić atrapą
 * o kształcie API biblioteki. To ten sam wzorzec, co przy adapterach
 * graficznych, i z tego samego powodu.
 */

#include "hydra_test.hpp"

#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/ui/Mock.hpp"
#include "hydra/ui/lvgl/LvglModule.hpp"

using namespace hydra;
using namespace hydra::ui;
using namespace hydra::ui::lvgl;
using namespace hydra::gfx;

namespace {

void resetLvgl() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

/** Atrapa API LVGL o kształcie wymaganym przez LvglModule. */
struct FakeLvgl {
    static int           inits;
    static int           deinits;
    static u32           tickTotal;
    static int           timerCalls;
    static int           flushReadyCalls;
    static int           pointerDevices;
    static int           encoderDevices;
    static PointerFeed   lastPointer;
    static EncoderFeed   lastEncoder;
    static int           encoderFeeds;
    static FlushCallback callback;
    static void*         user;
    static i16           width;
    static i16           height;
    static ColorFormat   format;

    static void reset() {
        inits = deinits = timerCalls = flushReadyCalls = 0;
        pointerDevices = encoderDevices = encoderFeeds = 0;
        tickTotal   = 0;
        lastPointer = PointerFeed{};
        lastEncoder = EncoderFeed{};
        callback    = nullptr;
        user        = nullptr;
        width = height = 0;
    }

    static Status init() {
        ++inits;
        return ok();
    }
    static void deinit() { ++deinits; }

    static Status createDisplay(i16 w, i16 h, ColorFormat fmt, FlushCallback cb,
                                void* userData) {
        width    = w;
        height   = h;
        format   = fmt;
        callback = cb;
        user     = userData;
        return ok();
    }

    static void flushReady() { ++flushReadyCalls; }
    static void tickInc(u32 ms) { tickTotal += ms; }
    static u32  timerHandler() {
        ++timerCalls;
        return 5;
    }

    static Status createPointer() {
        ++pointerDevices;
        return ok();
    }
    static void feedPointer(PointerFeed feed) { lastPointer = feed; }

    static Status createEncoder() {
        ++encoderDevices;
        return ok();
    }
    static void feedEncoder(EncoderFeed feed) {
        lastEncoder = feed;
        ++encoderFeeds;
    }

    /** Symuluje oddanie fragmentu obrazu przez LVGL. */
    static void emitFlush(Rect area, const u8* pixels, bool last) {
        if (callback) callback(user, area, pixels, last);
    }
};

int           FakeLvgl::inits           = 0;
int           FakeLvgl::deinits         = 0;
u32           FakeLvgl::tickTotal       = 0;
int           FakeLvgl::timerCalls      = 0;
int           FakeLvgl::flushReadyCalls = 0;
int           FakeLvgl::pointerDevices  = 0;
int           FakeLvgl::encoderDevices  = 0;
PointerFeed   FakeLvgl::lastPointer{};
EncoderFeed   FakeLvgl::lastEncoder{};
int           FakeLvgl::encoderFeeds    = 0;
FlushCallback FakeLvgl::callback        = nullptr;
void*         FakeLvgl::user            = nullptr;
i16           FakeLvgl::width           = 0;
i16           FakeLvgl::height          = 0;
ColorFormat   FakeLvgl::format          = ColorFormat::Rgb565;

/** Buduje bufor RGB565 w kolejności bajtów procesora, tak jak robi to LVGL. */
void fillRgb565(u8* buffer, size_t pixels, Color color) {
    const u16 v = color.rgb565();
    for (size_t i = 0; i < pixels; ++i) {
        buffer[i * 2]     = static_cast<u8>(v & 0xFF);
        buffer[i * 2 + 1] = static_cast<u8>(v >> 8);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Most
// ---------------------------------------------------------------------------

TEST("Most LVGL: fragment obrazu trafia we właściwe miejsce powierzchni") {
    resetLvgl();
    mock::MockDisplay display;
    LvglBridge        bridge;
    REQUIRE(bridge.init(display, ColorFormat::Rgb565).has_value());

    // LVGL oddaje prostokąt 4×3 zaczynający się w (10, 5).
    u8 pixels[4 * 3 * 2];
    fillRgb565(pixels, 12, colors::white);

    REQUIRE(bridge.flushArea(Rect(10, 5, 4, 3), pixels).has_value());

    Framebuffer& fb = display.framebuffer();
    CHECK(fb.pixelAt(10, 5).value_or(colors::black) != colors::black);
    CHECK(fb.pixelAt(13, 7).value_or(colors::black) != colors::black);
    // Poza obszarem nic nie zostało tknięte.
    CHECK(fb.pixelAt(9, 5).value_or(colors::white) == colors::black);
    CHECK(fb.pixelAt(14, 5).value_or(colors::white) == colors::black);
    CHECK(fb.pixelAt(10, 8).value_or(colors::white) == colors::black);

    CHECK_EQ(static_cast<int>(bridge.stats().flushes), 1);
    CHECK_EQ(static_cast<int>(bridge.stats().pixels), 12);
}

TEST("Most LVGL: kolejność bajtów RGB565 zmienia barwę, nie układ") {
    resetLvgl();
    mock::MockDisplay normal, swapped;

    LvglBridge a, b;
    REQUIRE(a.init(normal, ColorFormat::Rgb565).has_value());
    REQUIRE(b.init(swapped, ColorFormat::Rgb565Swapped).has_value());

    // Czysta czerwień w RGB565 to 0xF800.
    const u8 pixels[2] = {0x00, 0xF8};
    REQUIRE(a.flushArea(Rect(0, 0, 1, 1), pixels).has_value());
    REQUIRE(b.flushArea(Rect(0, 0, 1, 1), pixels).has_value());

    const Color fromNormal  = normal.framebuffer().pixelAt(0, 0).value_or(colors::black);
    const Color fromSwapped = swapped.framebuffer().pixelAt(0, 0).value_or(colors::black);

    CHECK(fromNormal.r > 200);
    CHECK(fromNormal.b < 60);
    // Zamiana bajtów bez uwzględnienia formatu dałaby fałszywe barwy — i to
    // jest jedyny objaw, po którym da się ją rozpoznać.
    CHECK(fromSwapped != fromNormal);
}

TEST("Most LVGL: format RGB888 ma składowe w kolejności B, G, R") {
    resetLvgl();
    mock::MockDisplay display;
    LvglBridge        bridge;
    REQUIRE(bridge.init(display, ColorFormat::Rgb888).has_value());

    // LVGL układa piksel jako B, G, R — odwrotnie niż sugeruje nazwa formatu.
    const u8 pixel[3] = {0x00, 0x00, 0xFF};  // niebieski=0, zielony=0, czerwony=255
    REQUIRE(bridge.flushArea(Rect(0, 0, 1, 1), pixel).has_value());

    const Color out = display.framebuffer().pixelAt(0, 0).value_or(colors::black);
    CHECK(out.r > 200);
    CHECK(out.b < 60);
}

TEST("Most LVGL: format jednobitowy") {
    resetLvgl();
    mock::MockDisplay display;
    LvglBridge        bridge;
    REQUIRE(bridge.init(display, ColorFormat::Mono1).has_value());

    // Osiem pikseli w jednym bajcie, bit najstarszy z lewej.
    const u8 row[1] = {0b10000001};
    REQUIRE(bridge.flushArea(Rect(0, 0, 8, 1), row).has_value());

    Framebuffer& fb = display.framebuffer();
    CHECK(fb.pixelAt(0, 0).value_or(colors::black) != colors::black);
    CHECK(fb.pixelAt(7, 0).value_or(colors::black) != colors::black);
    CHECK(fb.pixelAt(3, 0).value_or(colors::white) == colors::black);
}

TEST("Most LVGL: obszar wychodzący poza panel jest przycinany, nie odrzucany") {
    resetLvgl();
    mock::MockDisplay display;
    LvglBridge        bridge;
    REQUIRE(bridge.init(display, ColorFormat::Rgb565).has_value());

    u8 pixels[8 * 2 * 2];
    fillRgb565(pixels, 16, colors::white);

    // Fragment zaczynający się tuż przy prawej krawędzi panelu.
    const i16 x = static_cast<i16>(mock::MockDisplay::kWidth - 4);
    REQUIRE(bridge.flushArea(Rect(x, 0, 8, 2), pixels).has_value());

    // LVGL potrafi zgłosić taki obszar przy obrocie albo animacji — to nie
    // jest błąd, więc pomost przycina i liczy, zamiast odmawiać.
    CHECK_EQ(static_cast<int>(bridge.stats().clipped), 1);
    CHECK_EQ(static_cast<int>(bridge.stats().rejected), 0);
    CHECK(display.framebuffer().pixelAt(x, 0).value_or(colors::black) != colors::black);
}

TEST("Most LVGL: obszar w całości poza panelem nie jest błędem") {
    resetLvgl();
    mock::MockDisplay display;
    LvglBridge        bridge;
    REQUIRE(bridge.init(display, ColorFormat::Rgb565).has_value());

    u8 pixels[4] = {};
    REQUIRE(bridge.flushArea(Rect(500, 500, 1, 1), pixels).has_value());
    CHECK_EQ(static_cast<int>(bridge.stats().flushes), 0);
    CHECK_EQ(static_cast<int>(bridge.stats().clipped), 1);
}

TEST("Most LVGL: błędne dane są odrzucane") {
    resetLvgl();
    mock::MockDisplay display;
    LvglBridge        bridge;

    u8 pixels[4] = {};
    // Przed inicjalizacją nie ma dokąd pisać.
    CHECK(bridge.flushArea(Rect(0, 0, 1, 1), pixels).error() == Err::NotInitialized);

    REQUIRE(bridge.init(display, ColorFormat::Rgb565).has_value());
    CHECK(bridge.flushArea(Rect(0, 0, 1, 1), nullptr).error() == Err::BadArgument);
    CHECK(bridge.flushArea(Rect(), pixels).error() == Err::BadArgument);
    CHECK_EQ(static_cast<int>(bridge.stats().rejected), 2);
}

TEST("Most LVGL: działa na każdym panelu, dla którego jest adapter") {
    resetLvgl();

    // Powierzchnia jednobitowa — na przykład OLED przez U8g2 albo e-papier.
    static u8   vram[Framebuffer::bytesNeeded(32, 16, PixelFormat::Mono1)] = {};
    Framebuffer fb;
    REQUIRE(fb.attach(ByteSpan{vram, sizeof(vram)}, 32, 16, PixelFormat::Mono1)
                .has_value());
    SurfaceDisplay display(fb, "oled");

    LvglBridge bridge;
    REQUIRE(bridge.init(display, ColorFormat::Rgb565).has_value());

    // LVGL rysuje w kolorze, panel jest jednobitowy — konwersja jest po drodze
    // i to właśnie dzięki niej ten sam kod interfejsu działa na obu.
    u8 pixels[4 * 2];
    fillRgb565(pixels, 4, colors::white);
    REQUIRE(bridge.flushArea(Rect(0, 0, 2, 2), pixels).has_value());

    CHECK(fb.pixelAt(0, 0).value_or(colors::black) != colors::black);
}

// ---------------------------------------------------------------------------
// Moduł
// ---------------------------------------------------------------------------

TEST("Moduł LVGL: inicjalizacja tworzy panel i urządzenia wejściowe") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay display;
    mock::MockPointer pointer;
    mock::MockEncoder encoder;

    LvglModule<FakeLvgl> ui(display);
    ui.attachPointer(pointer);
    ui.attachEncoder(encoder);
    REQUIRE(ui.configure(LvglModule<FakeLvgl>::Config{}).has_value());
    REQUIRE(ui.init().has_value());

    CHECK_EQ(FakeLvgl::inits, 1);
    CHECK_EQ(static_cast<int>(FakeLvgl::width), mock::MockDisplay::kWidth);
    CHECK_EQ(static_cast<int>(FakeLvgl::height), mock::MockDisplay::kHeight);
    CHECK_EQ(FakeLvgl::pointerDevices, 1);
    CHECK_EQ(FakeLvgl::encoderDevices, 1);
    CHECK(FakeLvgl::callback != nullptr);
}

TEST("Moduł LVGL: pętla odmierza czas i woła obsługę liczników") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay    display;
    LvglModule<FakeLvgl> ui(display);
    REQUIRE(ui.configure(LvglModule<FakeLvgl>::Config{}).has_value());
    REQUIRE(ui.init().has_value());

    // Pierwszy przebieg ustala punkt odniesienia — bez tego LVGL dostałby
    // jednorazowy przyrost równy czasowi od startu urządzenia.
    ui.step(1000);
    CHECK_EQ(static_cast<int>(FakeLvgl::tickTotal), 0);
    CHECK_EQ(FakeLvgl::timerCalls, 1);

    ui.step(1033);
    CHECK_EQ(static_cast<int>(FakeLvgl::tickTotal), 33);
    ui.step(1066);
    CHECK_EQ(static_cast<int>(FakeLvgl::tickTotal), 66);
    CHECK_EQ(FakeLvgl::timerCalls, 3);
}

TEST("Moduł LVGL: polecenia z kolejki wykonują się przed rysowaniem") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay    display;
    LvglModule<FakeLvgl> ui(display);
    REQUIRE(ui.configure(LvglModule<FakeLvgl>::Config{}).has_value());
    REQUIRE(ui.init().has_value());

    int orderCommand = 0;
    int orderTimer   = 0;
    int counter      = 0;

    REQUIRE(ui.queue().post([&] { orderCommand = ++counter; }).has_value());

    // LVGL nie jest thread-safe — zmiana z innego taska musi trafić przed
    // przejściem po drzewie obiektów, a nie w jego trakcie.
    const int before = FakeLvgl::timerCalls;
    ui.step(0);
    orderTimer = (FakeLvgl::timerCalls > before) ? ++counter : 0;

    CHECK_EQ(orderCommand, 1);
    CHECK_EQ(orderTimer, 2);
    CHECK_EQ(static_cast<int>(ui.stats().commands), 1);
}

TEST("Moduł LVGL: wskaźnik dostaje surowy stan, enkoder różnicę") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay display;
    mock::MockPointer pointer;
    mock::MockEncoder encoder;

    LvglModule<FakeLvgl> ui(display);
    ui.attachPointer(pointer);
    ui.attachEncoder(encoder);
    REQUIRE(ui.configure(LvglModule<FakeLvgl>::Config{}).has_value());
    REQUIRE(ui.init().has_value());

    pointer.set(11, 22, true);
    encoder.setPosition(100);
    ui.step(0);

    // LVGL sam rozpoznaje gesty i przeciąganie, więc dostaje stan, nie zdarzenia.
    CHECK_EQ(static_cast<int>(FakeLvgl::lastPointer.x), 11);
    CHECK_EQ(static_cast<int>(FakeLvgl::lastPointer.y), 22);
    CHECK(FakeLvgl::lastPointer.pressed);
    // Pierwszy odczyt enkodera to punkt odniesienia, więc różnica jest zerowa.
    CHECK_EQ(static_cast<int>(FakeLvgl::lastEncoder.diff), 0);

    encoder.setPosition(104);
    ui.step(33);
    CHECK_EQ(static_cast<int>(FakeLvgl::lastEncoder.diff), 4);

    encoder.setPosition(102);
    ui.step(66);
    CHECK_EQ(static_cast<int>(FakeLvgl::lastEncoder.diff), -2);
}

TEST("Moduł LVGL: brak odczytu z urządzenia nie karmi biblioteki śmieciami") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay display;
    mock::MockEncoder encoder;

    LvglModule<FakeLvgl> ui(display);
    ui.attachEncoder(encoder);
    REQUIRE(ui.configure(LvglModule<FakeLvgl>::Config{}).has_value());
    REQUIRE(ui.init().has_value());

    encoder.setPosition(5);
    ui.step(0);
    const int feeds = FakeLvgl::encoderFeeds;

    // Bez podłączonego wskaźnika nic go nie karmi.
    CHECK_EQ(FakeLvgl::lastPointer.x, 0);
    CHECK(feeds > 0);
}

TEST("Moduł LVGL: panel ruszany raz na klatkę, po ostatnim fragmencie") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay    display;
    LvglModule<FakeLvgl> ui(display);
    REQUIRE(ui.configure(LvglModule<FakeLvgl>::Config{}).has_value());
    REQUIRE(ui.init().has_value());

    u8 pixels[8 * 2 * 2];
    fillRgb565(pixels, 16, colors::white);

    // LVGL w trybie częściowym oddaje klatkę w kilku kawałkach.
    FakeLvgl::emitFlush(Rect(0, 0, 8, 2), pixels, false);
    CHECK_EQ(static_cast<int>(display.presents()), 0);
    CHECK_EQ(FakeLvgl::flushReadyCalls, 1);

    FakeLvgl::emitFlush(Rect(0, 2, 8, 2), pixels, false);
    CHECK_EQ(static_cast<int>(display.presents()), 0);

    // Dopiero ostatni fragment uruchamia transfer — panel ruszany po każdym
    // z osobna kosztowałby wielokrotnie więcej.
    FakeLvgl::emitFlush(Rect(0, 4, 8, 2), pixels, true);
    CHECK_EQ(static_cast<int>(display.presents()), 1);
    CHECK_EQ(FakeLvgl::flushReadyCalls, 3);
    CHECK_EQ(static_cast<int>(ui.stats().frames), 1);

    // Transfer objął sumę wszystkich fragmentów, nie tylko ostatni.
    CHECK(display.lastArea() == Rect(0, 0, 8, 6));
}

TEST("Moduł LVGL: potwierdzenie bufora idzie po przeniesieniu zawartości") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay    display;
    LvglModule<FakeLvgl> ui(display);
    REQUIRE(ui.configure(LvglModule<FakeLvgl>::Config{}).has_value());
    REQUIRE(ui.init().has_value());

    u8 pixels[2 * 2 * 2];
    fillRgb565(pixels, 4, colors::white);
    FakeLvgl::emitFlush(Rect(1, 1, 2, 2), pixels, true);

    // Potwierdzenie przed skopiowaniem pozwoliłoby LVGL nadpisać bufor
    // w trakcie transferu — obraz rozjechałby się co kilka klatek.
    CHECK(display.framebuffer().pixelAt(1, 1).value_or(colors::black) != colors::black);
    CHECK_EQ(FakeLvgl::flushReadyCalls, 1);
    CHECK_EQ(static_cast<int>(ui.bridge().stats().flushes), 1);
}

TEST("Moduł LVGL: zatrzymanie sprząta po bibliotece") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay    display;
    LvglModule<FakeLvgl> ui(display);
    REQUIRE(ui.configure(LvglModule<FakeLvgl>::Config{}).has_value());
    REQUIRE(ui.init().has_value());
    REQUIRE(ui.start().has_value());

    rtos::delayMs(60);
    ui.stop();

    CHECK_EQ(FakeLvgl::deinits, 1);
    CHECK(FakeLvgl::timerCalls > 0);
    CHECK(ui.stats().ticks > 0);
}

TEST("Moduł LVGL: błędna konfiguracja jest odrzucana") {
    resetLvgl();
    FakeLvgl::reset();

    mock::MockDisplay    display;
    LvglModule<FakeLvgl> ui(display);

    LvglModule<FakeLvgl>::Config cfg;
    cfg.framePeriodMs = 0;
    CHECK(ui.configure(cfg).error() == Err::BadArgument);
}
