/**
 * desktop-preview — jeden ekran, dwa cele.
 *
 * Sedno projektu mieści się w jednym zdaniu: `drawStatus()` nie wie, gdzie
 * rysuje. Dostaje `gfx::ISurface` i tyle. Na celu `podglad` pod spodem jest
 * okno SDL, na `esp32s3` — bufor wypychany na panel I²C. Kod rysujący jest
 * bajt w bajt ten sam i to jest cała teza tego projektu.
 *
 * Wynika z tego rzecz praktyczna: ekran projektuje się w oknie, w pętli
 * „popraw i zobacz" trwającej sekundy, a nie w cyklu „skompiluj, wgraj,
 * zmruż oczy" trwającym minutę. Na sprzęt trafia coś, co już wygląda dobrze.
 *
 * Podział wątków jest ten sam po obu stronach i też nie jest przypadkiem:
 *
 *   • task `sense.sim` liczy dane i publikuje je na magistrali,
 *   • pętla rysująca czyta ostatni stan i maluje.
 *
 * Task nie dotyka powierzchni ani razu. Na urządzeniu byłby to wyścig
 * z transferem DMA na panel, na hoście — wyścig z konwersją klatki, który
 * TSan zgłasza. Ta sama zasada, dwa różne objawy.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
// Deklaracje setup() i loop(): STM32duino umieszcza je w bloku extern "C",
// więc bez tego konsolidator nie znajduje definicji poniżej.
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include <atomic>

#include <math.h>
#include <stdio.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/gfx/Font.hpp"
#include "hydra/gfx/Framebuffer.hpp"
#include "hydra/hal/Board.hpp"

#if HYDRA_PLAT_HOST
#  include "hydra/gfx/SdlDisplay.hpp"
#endif

HYDRA_LOG_MODULE("preview")

using namespace hydra;

namespace {

// --- panel -----------------------------------------------------------------
//
// Rozmiar podaje cel w pliku .hydra (sekcja `native`), a generator przekazuje
// go flagami. Wartości domyślne odpowiadają temu, co jest w projekcie, żeby
// plik dało się zbudować także ręcznie, bez generowania.

#ifndef HYDRA_NATIVE_WINDOW_W
#  define HYDRA_NATIVE_WINDOW_W 128
#endif
#ifndef HYDRA_NATIVE_WINDOW_H
#  define HYDRA_NATIVE_WINDOW_H 64
#endif
#ifndef HYDRA_NATIVE_WINDOW_SCALE
#  define HYDRA_NATIVE_WINDOW_SCALE 6
#endif

constexpr i16 kWidth  = HYDRA_NATIVE_WINDOW_W;
constexpr i16 kHeight = HYDRA_NATIVE_WINDOW_H;

constexpr gfx::PixelFormat kFormat = gfx::PixelFormat::Mono1;

// Bufor obrazu jest statyczny. Zasada „nic się nie alokuje po App::begin()"
// obowiązuje także na hoście — inaczej cel natywny przestałby sprawdzać to,
// po co istnieje.
u8 gVram[gfx::Framebuffer::bytesNeeded(kWidth, kHeight, kFormat)];

// --- dane ------------------------------------------------------------------

/** Odczyt czujnika. POD do 32 bajtów, więc przechodzi magistralą zdarzeń. */
struct Sample {
    u32 sequence;
    i16 temperatureC10;   ///< dziesiąte części stopnia
    i16 pressureHpa;
};

/** Ostatni stan: pisze subskrybent, czyta pętla rysująca. */
struct View {
    std::atomic<u32> sequence{0};
    std::atomic<i16> temperatureC10{0};
    std::atomic<i16> pressureHpa{0};
};

View gView;

/**
 * Źródło danych.
 *
 * Na sprzęcie w tym miejscu stałby BMP280 z paczki `bmp280`, zarejestrowany
 * w hubie czujników. Tutaj jest model, bo projekt ma się dać uruchomić, zanim
 * cokolwiek zostanie przylutowane — dokładnie po to jest sekcja `simulation`
 * w pliku projektu.
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
        EventBus::publish(Sample{
            sequence_,
            static_cast<i16>(215.0f + 45.0f * sinf(phase)),
            static_cast<i16>(1013.0f + 6.0f * sinf(phase * 0.37f)),
        });
    }

    Task task_{};
    u32  sequence_ = 0;
};

// --- ekran -----------------------------------------------------------------

/**
 * Jedyna funkcja rysująca w całym projekcie.
 *
 * Przyjmuje `ISurface`, więc nie ma jak zależeć od tego, czy pod spodem jest
 * okno, czy panel. Gdyby przyjmowała `SdlDisplay` albo `Framebuffer`,
 * przeniesienie ekranu na sprzęt oznaczałoby przepisanie jej — i po dwóch
 * takich przeniesieniach istniałyby dwie wersje, które się rozjeżdżają.
 */
void drawStatus(gfx::ISurface& s) {
    const gfx::Font& font = gfx::font8x8();

    s.fill(gfx::colors::black);

    // Pasek tytułu w negatywie — na monochromatycznym panelu to jedyny
    // sposób na wyróżnienie nagłówka.
    s.fillRect(gfx::Rect(0, 0, kWidth, 11), gfx::colors::white);
    s.drawText(2, 2, hal::board::name, gfx::colors::black, gfx::colors::white, font);

    char line[32];

    const i16 tenths = gView.temperatureC10.load(std::memory_order_relaxed);
    snprintf(line, sizeof(line), "%d.%d C", tenths / 10,
             (tenths < 0 ? -tenths : tenths) % 10);
    s.drawText(2, 16, line, gfx::colors::white, font);

    snprintf(line, sizeof(line), "%d hPa",
             static_cast<int>(gView.pressureHpa.load(std::memory_order_relaxed)));
    s.drawText(2, 28, line, gfx::colors::white, font);

    snprintf(line, sizeof(line), "#%lu  %lus",
             static_cast<unsigned long>(gView.sequence.load(std::memory_order_relaxed)),
             static_cast<unsigned long>(App::uptimeMs() / 1000));
    s.drawText(2, 44, line, gfx::colors::white, font);

    // Pasek postępu z temperatury — sprawdza przycinanie na krawędzi.
    const i16 fill = static_cast<i16>((tenths - 170) * (kWidth - 4) / 90);
    s.drawRect(gfx::Rect(2, 56, kWidth - 4, 6), gfx::colors::white);
    if (fill > 0) s.fillRect(gfx::Rect(2, 56, fill, 6), gfx::colors::white);
}

void subscribeView() {
    EventBus::subscribe<Sample>([](const Sample& e) {
        gView.sequence.store(e.sequence, std::memory_order_relaxed);
        gView.temperatureC10.store(e.temperatureC10, std::memory_order_relaxed);
        gView.pressureHpa.store(e.pressureHpa, std::memory_order_relaxed);
    });
}

SimSensorModule gSensor;

#if HYDRA_PLAT_HOST
StdoutLogSink   gConsole;
gfx::SdlDisplay gDisplay;
#else
UartLogSink     gConsole;
gfx::Framebuffer gFb;
#endif

}  // namespace

#if HYDRA_PLAT_HOST

// --- cel `podglad`: okno na pulpicie ---------------------------------------

int main() {
    App::config()
        .name("desktop-preview")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gSensor);

    subscribeView();

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return 1;
    }

    gfx::SdlDisplay::Cfg cfg;
    cfg.title  = "desktop-preview — OLED 128x64";
    cfg.width  = kWidth;
    cfg.height = kHeight;
    cfg.scale  = HYDRA_NATIVE_WINDOW_SCALE;
    cfg.format = kFormat;
    // Barwy typowego OLED-a SSD1306, żeby podgląd wyglądał jak panel,
    // a nie jak czarno-biały wydruk.
    cfg.monoOn  = gfx::Color::rgb(180, 220, 255);
    cfg.monoOff = gfx::Color::rgb(8, 12, 18);

    if (auto r = gDisplay.begin(ByteSpan{gVram, sizeof(gVram)}, cfg); !r) {
        // Brak ekranu nie jest awarią: w CI i przez ssh okna nie ma, a logika
        // ma się dać uruchomić tak samo. Wtedy program tylko loguje.
        HYDRA_LOGW("okno niedostępne (%s) — praca bez interfejsu",
                   toString(r.error()));
        rtos::delayMs(3000);
        App::stop();
        return 0;
    }

    while (gDisplay.pump()) {
        drawStatus(gDisplay.surface());
        gDisplay.surface().flush();
    }

    HYDRA_LOGI("zamknięto po %lu klatkach",
               static_cast<unsigned long>(gDisplay.framesPresented()));
    App::stop();
    return 0;
}

#else

// --- cel `esp32s3`: panel na magistrali ------------------------------------

/**
 * Wypchnięcie bufora na panel.
 *
 * Zostawione puste celowo: sterownik panelu jest wyborem projektu, a nie
 * frameworka. Dla SSD1306 przez adapter U8g2 to kilka linii:
 *
 *     #include <U8g2lib.h>
 *     #include <hydra/gfx/adapters/U8g2Surface.hpp>
 *     U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);
 *     hydra::gfx::U8g2Surface<decltype(u8g2)> surface(u8g2);
 *
 * — i wtedy `drawStatus(surface)` zamiast `drawStatus(gFb)`. Bufor pośredni
 * zostaje tu po to, żeby ta sama ścieżka działała także na panelach bez
 * własnej pamięci.
 */
Status presentToPanel(CByteSpan pixels, Size size, gfx::PixelFormat format) {
    HYDRA_UNUSED(pixels);
    HYDRA_UNUSED(size);
    HYDRA_UNUSED(format);
    return ok();
}

void setup() {
    App::config()
        .name("desktop-preview")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gSensor);

    subscribeView();

    if (auto r = gFb.attach(ByteSpan{gVram, sizeof(gVram)}, kWidth, kHeight, kFormat); !r) {
        HYDRA_LOGE("bufor obrazu: %s", toString(r.error()));
    }
    gFb.setPresent(presentToPanel);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Odświeżanie w pętli głównej, a nie w tasku czujnika — ta sama granica
    // co na hoście. 30 Hz wystarcza dla panelu na I²C i zostawia magistralę
    // wolną dla pomiarów.
    drawStatus(gFb);
    gFb.flush();
    rtos::delayMs(33);
}

#endif
