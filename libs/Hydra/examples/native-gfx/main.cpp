/**
 * Hydra — przykład: native-gfx.
 *
 * Ta sama aplikacja co na urządzeniu, tylko panel jest oknem na pulpicie.
 * Pokazuje, że cel `native` nie jest trybem podglądu ani makietą: chodzi tu
 * pełny rdzeń Hydry — moduł z cyklem życia, task okresowy z egzekwowanym
 * okresem, magistrala zdarzeń — a jedyną różnicą wobec ESP32 z panelem IPS
 * jest to, czym jest `gfx::ISurface`.
 *
 * Budowa (z katalogu przykładu):
 *
 *     cmake -B build -D HYDRA_ROOT=../..
 *     cmake --build build
 *     ./build/native-gfx
 *
 * Podział pracy między wątkami jest tu celowy i odpowiada temu z urządzenia:
 *
 *   • task `sense.sim` liczy dane i publikuje je na magistrali — nie dotyka
 *     ekranu ani razu,
 *   • pętla główna rysuje i obsługuje okno.
 *
 * Na urządzeniu wygląda to identycznie: `sense.poll` mierzy, `ui.render`
 * rysuje. Gdyby task rysował wprost do bufora, TSan zgłosiłby wyścig
 * z konwersją klatki — i miałby rację, bo na panelu z DMA byłby to ten sam błąd.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  error "Przykład native-gfx buduje się wyłącznie dla celu native (HYDRA_FORCE_HOST)."
#endif

#include <Hydra.h>

#include <atomic>

#include <math.h>
#include <stdio.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/gfx/Font.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/gfx/SdlDisplay.hpp"

HYDRA_LOG_MODULE("native-gfx")

using namespace hydra;

namespace {

// --- okno ------------------------------------------------------------------
//
// Rozmiar podaje cel w pliku .hydra (sekcja `native`), a Studio przekazuje go
// flagami. Wartości domyślne odpowiadają typowemu panelowi IPS 2,8".

#ifndef HYDRA_NATIVE_WINDOW_W
#  define HYDRA_NATIVE_WINDOW_W 320
#endif
#ifndef HYDRA_NATIVE_WINDOW_H
#  define HYDRA_NATIVE_WINDOW_H 240
#endif
#ifndef HYDRA_NATIVE_WINDOW_SCALE
#  define HYDRA_NATIVE_WINDOW_SCALE 3
#endif

constexpr i16 kWidth  = HYDRA_NATIVE_WINDOW_W;
constexpr i16 kHeight = HYDRA_NATIVE_WINDOW_H;

constexpr gfx::PixelFormat kFormat = gfx::PixelFormat::Rgb565;

// Bufor obrazu jest statyczny — po App::begin() nic się nie alokuje (rozdz. 11),
// i ta zasada obowiązuje na hoście tak samo jak na MCU. Inaczej cel `native`
// przestałby sprawdzać to, po co istnieje.
u8 gVram[gfx::SdlDisplay::bytesNeeded(kWidth, kHeight, kFormat)];

gfx::SdlDisplay gDisplay;

// --- dane ------------------------------------------------------------------

/** Odczyt „czujnika" — ten sam kształt, co zdarzenia modułu sense. */
struct Reading {
    u32 sequence;
    i16 temperatureC10;   ///< dziesiąte części stopnia
    i16 pressureHpa;
};

/**
 * Moduł liczący dane. Nie wie nic o ekranie i nie ma prawa wiedzieć — dokładnie
 * jak na urządzeniu. Wymiana z warstwą rysującą idzie przez magistralę.
 */
class SimSensorModule : public ModuleBase {
public:
    SimSensorModule() : ModuleBase("sim.sensor") {}

protected:
    Status onInit() override { return ok(); }

    Status onStart() override {
        Task::Cfg cfg;
        cfg.name = "sense.sim";
        cfg.prio = Prio::Normal;
        return task_.startPeriodic(cfg, 50, [this] { tick(); });
    }

    void onStop() override { task_.stopAndWait(); }

private:
    void tick() {
        ++sequence_;
        const float phase = static_cast<float>(sequence_) * 0.05f;
        const Reading reading{
            sequence_,
            static_cast<i16>(215.0f + 45.0f * sinf(phase)),
            static_cast<i16>(1013.0f + 6.0f * sinf(phase * 0.37f)),
        };
        EventBus::publish(reading);
    }

    Task task_{};
    u32  sequence_ = 0;
};

// --- widok -----------------------------------------------------------------

/**
 * Ostatni odczyt i historia do wykresu.
 *
 * Zapisuje go subskrybent magistrali (wątek taska), czyta pętla rysująca
 * (wątek główny). Dla przykładu o jednym producencie i jednym konsumencie
 * pola atomowe wystarczą i są tańsze niż zamek na każdej klatce.
 */
struct View {
    std::atomic<u32> sequence{0};
    std::atomic<i16> temperatureC10{0};
    std::atomic<i16> pressureHpa{0};

    /** Bufor cykliczny wartości do wykresu — piszący tylko task. */
    static constexpr i16 kHistory = kWidth - 20;
    i16                  history[kHistory] = {};
    std::atomic<u32>     head{0};
};

View gView;

const gfx::Color kBackground = gfx::Color::rgb(12, 16, 24);
const gfx::Color kPanel      = gfx::Color::rgb(24, 32, 46);
const gfx::Color kAccent     = gfx::Color::rgb(224, 153, 63);
const gfx::Color kText       = gfx::Color::rgb(226, 232, 240);
const gfx::Color kDim        = gfx::Color::rgb(100, 116, 139);

void drawFrame(gfx::ISurface& s) {
    const gfx::Font& font = gfx::font8x8();

    s.fill(kBackground);

    // Pasek tytułu — nazwa płytki pochodzi z HYDRA_BOARD_NAME, więc ten sam
    // kod wypisze „esp32s3-pico" po przełączeniu celu.
    s.fillRect(gfx::Rect(0, 0, kWidth, 14), kPanel);
    s.drawText(4, 4, App::deviceName(), kAccent, font);

    char header[48];
    snprintf(header, sizeof(header), "%s  %lus", hal::board::name,
             static_cast<unsigned long>(App::uptimeMs() / 1000));
    s.drawText(static_cast<i16>(kWidth - 4 - gfx::textWidth(font, header)), 4, header, kDim, font);

    // Odczyty.
    char line[64];
    const i16 tenths = gView.temperatureC10.load(std::memory_order_relaxed);
    snprintf(line, sizeof(line), "temperatura  %d.%d C", tenths / 10, (tenths < 0 ? -tenths : tenths) % 10);
    s.drawText(6, 24, line, kText, font);

    snprintf(line, sizeof(line), "cisnienie    %d hPa",
             static_cast<int>(gView.pressureHpa.load(std::memory_order_relaxed)));
    s.drawText(6, 36, line, kText, font);

    snprintf(line, sizeof(line), "odczyt       #%lu",
             static_cast<unsigned long>(gView.sequence.load(std::memory_order_relaxed)));
    s.drawText(6, 48, line, kDim, font);

    // Wykres historii.
    const gfx::Rect plot(10, 66, View::kHistory, kHeight - 66 - 22);
    s.drawRect(plot, kDim);

    const u32 head = gView.head.load(std::memory_order_acquire);
    for (i16 i = 1; i < View::kHistory; ++i) {
        const i16 a = gView.history[(head + i - 1) % View::kHistory];
        const i16 b = gView.history[(head + i) % View::kHistory];
        if (a == 0 && b == 0) continue;
        s.line(static_cast<i16>(plot.x + i - 1), static_cast<i16>(plot.y + plot.h - 1 - a / 4),
               static_cast<i16>(plot.x + i),     static_cast<i16>(plot.y + plot.h - 1 - b / 4),
               kAccent);
    }

    // Wskaźnik myszy — dowód, że wejście dochodzi tam, gdzie ma dojść.
    const gfx::SdlDisplay::Pointer p = gDisplay.pointer();
    const gfx::Color cursor = p.down ? kAccent : kDim;
    s.hLine(static_cast<i16>(p.x - 4), p.y, 9, cursor);
    s.vLine(p.x, static_cast<i16>(p.y - 4), 9, cursor);

    s.drawText(6, kHeight - 12, "Esc konczy", kDim, font);
}

// --- log -------------------------------------------------------------------

StdoutLogSink   gConsole;
SimSensorModule gSensor;

}  // namespace

int main() {
    App::config()
        .name("native-gfx")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gSensor);

    // Widok aktualizuje subskrybent, a nie task — moduł nie musi wiedzieć,
    // że ktokolwiek go ogląda.
    EventBus::subscribe<Reading>([](const Reading& e) {
        gView.sequence.store(e.sequence, std::memory_order_relaxed);
        gView.temperatureC10.store(e.temperatureC10, std::memory_order_relaxed);
        gView.pressureHpa.store(e.pressureHpa, std::memory_order_relaxed);

        const u32 head = gView.head.load(std::memory_order_relaxed);
        gView.history[head % View::kHistory] = e.temperatureC10;
        gView.head.store(head + 1, std::memory_order_release);
    });

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return 1;
    }

    gfx::SdlDisplay::Cfg cfg;
    cfg.title  = "Hydra — native-gfx";
    cfg.width  = kWidth;
    cfg.height = kHeight;
    cfg.scale  = HYDRA_NATIVE_WINDOW_SCALE;
    cfg.format = kFormat;
    cfg.vsync  = true;

    if (auto r = gDisplay.begin(ByteSpan{gVram, sizeof(gVram)}, cfg); !r) {
        // Brak ekranu nie jest awarią aplikacji: w CI i przez ssh okna nie ma,
        // a logika ma się dać uruchomić tak samo. Kończymy zerem, bo program
        // zrobił to, co mógł zrobić w tym środowisku.
        HYDRA_LOGW("okno niedostępne (%s) — praca bez interfejsu przez 3 s",
                   toString(r.error()));
        rtos::delayMs(3000);
        App::stop();
        return 0;
    }

    while (gDisplay.pump()) {
        drawFrame(gDisplay.surface());
        gDisplay.surface().flush();
    }

    HYDRA_LOGI("zamknięto po %lu klatkach",
               static_cast<unsigned long>(gDisplay.framesPresented()));
    gDisplay.end();
    App::stop();
    return 0;
}
