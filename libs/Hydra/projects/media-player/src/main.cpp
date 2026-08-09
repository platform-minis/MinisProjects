/**
 * media-player — odtwarzacz z ekranem, ten sam kod na PC i na układzie.
 *
 * Teza projektu jest taka sama jak w `desktop-preview`, tylko rozciągnięta na
 * dźwięk: **różnica między celami mieści się w dwóch obiektach**, a nie
 * w dwóch programach.
 *
 *     podglad (native)   SdlAudioSource → Gain → SdlAudioSink
 *     esp32s3            FileSource     → Gain → I2sSink
 *
 * Wszystko poniżej — graf, pula, sterowanie, rysowanie, obsługa przycisków —
 * jest wspólne. Pauza to `Pipeline::pause()`, niezależnie od tego, czy pod
 * spodem jest karta dźwiękowa laptopa, czy wzmacniacz klasy D.
 *
 * **Dwie domeny, bo dwa rytmy.** Blok audio to ~12 ms; narysowanie ekranu
 * 240×135 trwa dłużej. Wspólny task oznaczałby przerwę w dźwięku przy każdej
 * klatce interfejsu — i to jest dokładnie ten przypadek, dla którego domeny
 * w ogóle istnieją.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include <stdio.h>
#include <string.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/gfx/Font.hpp"
#include "hydra/gfx/Framebuffer.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/media/elements/Basic.hpp"

#if HYDRA_PLAT_HOST
#  include "hydra/gfx/SdlDisplay.hpp"
#  include "hydra/media/elements/Sdl.hpp"
#else
#  include "hydra/media/elements/Audio.hpp"
#  include "hydra/media/elements/Files.hpp"
#endif

HYDRA_LOG_MODULE("player")

using namespace hydra;
using namespace hydra::media;

#if !HYDRA_PLAT_HOST
/**
 * System plików dostarcza projekt, nie framework.
 *
 * Hydra nie ma backendu karty SD — `IFileSystem` jest interfejsem, a jego
 * implementację wnosi urządzenie. Framework nie może za nas zdecydować, czy
 * muzyka leży na karcie, czy we flashu.
 *
 * Na celu `native` takiego wyboru nie ma: odpowiedzią jest katalog, z którego
 * uruchomiono program, i tam backend hostowy rejestruje go sam. Dlatego ta
 * deklaracja dotyczy wyłącznie układu.
 *
 * Poza anonimową przestrzenią nazw: w środku odnosiłaby się do symbolu
 * wewnętrznego tej jednostki translacji, którego projekt nie miałby jak
 * zdefiniować — a konsolidator zgłosiłby to dopiero na końcu budowy.
 */
hydra::hal::IFileSystem& projectFileSystem();
#endif

namespace {

// --- strumień ---------------------------------------------------------------

constexpr u32 kSampleRate     = 44100;
constexpr u8  kChannels       = 2;
constexpr u16 kFramesPerBlock = 512;                              // ~12 ms
constexpr u32 kBlockBytes     = kFramesPerBlock * kChannels * 2;  // S16 stereo
constexpr u16 kBlockCount     = 8;

const MediaFormat kFormat =
    MediaFormat::audio(kSampleRate, SampleFormat::S16, kChannels);

u8 gPoolStorage[kBlockBytes * kBlockCount + 64];

// --- ekran ------------------------------------------------------------------

#ifndef HYDRA_NATIVE_WINDOW_W
#  define HYDRA_NATIVE_WINDOW_W 240
#endif
#ifndef HYDRA_NATIVE_WINDOW_H
#  define HYDRA_NATIVE_WINDOW_H 135
#endif
#ifndef HYDRA_NATIVE_WINDOW_SCALE
#  define HYDRA_NATIVE_WINDOW_SCALE 4
#endif

constexpr i16 kScreenW = HYDRA_NATIVE_WINDOW_W;
constexpr i16 kScreenH = HYDRA_NATIVE_WINDOW_H;
constexpr gfx::PixelFormat kPixelFormat = gfx::PixelFormat::Rgb565;

u8 gVram[gfx::Framebuffer::bytesNeeded(kScreenW, kScreenH, kPixelFormat)];

// --- graf -------------------------------------------------------------------

Pipeline gPipeline;
Gain     gGain;

constexpr DomainId kAudio = 0;
constexpr DomainId kUi    = 1;

#if HYDRA_PLAT_HOST
gfx::SdlDisplay gDisplay;
SdlAudioSource  gSource;
SdlAudioSink    gSink;
#else
FileSource       gSource{::projectFileSystem()};
I2sSink          gSink{hal::Hal::i2s()};
gfx::Framebuffer gPanel;
#endif

/** Powierzchnia rysowania — to samo API po obu stronach. */
gfx::ISurface& screen() {
#if HYDRA_PLAT_HOST
    return gDisplay.surface();
#else
    return gPanel;
#endif
}

// --- interfejs --------------------------------------------------------------

const gfx::Color kBg      = gfx::Color::rgb(16, 20, 28);
const gfx::Color kPanelBg = gfx::Color::rgb(30, 38, 52);
const gfx::Color kAccent  = gfx::Color::rgb(224, 153, 63);
const gfx::Color kText    = gfx::Color::rgb(226, 232, 240);
const gfx::Color kDim     = gfx::Color::rgb(110, 124, 145);

/**
 * Przycisk. Prostokąt i podpis — bez modułu `ui`, bo dwa przyciski nie
 * uzasadniają drzewa widżetów, zdarzeń i układu.
 */
struct Button {
    gfx::Rect   box;
    const char* label;

    bool contains(i16 x, i16 y) const {
        return x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;
    }
};

constexpr Button kPlay { gfx::Rect(14, kScreenH - 44, 96, 30), "PLAY"  };
constexpr Button kPause{ gfx::Rect(kScreenW - 110, kScreenH - 44, 96, 30), "PAUSE" };

bool gPlaying = false;

void drawButton(gfx::ISurface& s, const Button& button, bool active) {
    const gfx::Font& font = gfx::font8x8();

    // Aktywny stan wypełniony, nieaktywny obrysowany. Na monochromatycznym
    // panelu to jedyne rozróżnienie, jakie zostaje — a tu wygląda tak samo.
    if (active) s.fillRect(button.box, kAccent);
    else        s.drawRect(button.box, kDim);

    const i16 textW = gfx::textWidth(font, button.label);
    const i16 x = static_cast<i16>(button.box.x + (button.box.w - textW) / 2);
    const i16 y = static_cast<i16>(button.box.y + (button.box.h - 8) / 2);
    s.drawText(x, y, button.label, active ? kBg : kText, font);
}

void drawScreen() {
    gfx::ISurface& s = screen();
    const gfx::Font& font = gfx::font8x8();

    s.fill(kBg);

    // Pasek tytułu. Nazwa płytki pochodzi z HYDRA_BOARD_NAME, więc ten sam
    // kod wypisze „native" i „esp32s3-devkitc" bez żadnej gałęzi.
    s.fillRect(gfx::Rect(0, 0, kScreenW, 18), kPanelBg);
    s.drawText(6, 5, "media-player", kAccent, font);
    s.drawText(static_cast<i16>(kScreenW - 6 - gfx::textWidth(font, hal::board::name)),
               5, hal::board::name, kDim, font);

    // Źródło i stan — jedyne miejsce, w którym widać, że cele się różnią.
#if HYDRA_PLAT_HOST
    s.drawText(6, 28, "zrodlo: wejscie hosta", kText, font);
#else
    s.drawText(6, 28, "zrodlo: plik WAV", kText, font);
#endif

    char line[48];
    snprintf(line, sizeof(line), "%s  %lu kHz  %s",
             gPlaying ? "gra" : "pauza",
             static_cast<unsigned long>(kSampleRate / 1000),
             kChannels == 2 ? "stereo" : "mono");
    s.drawText(6, 42, line, gPlaying ? kAccent : kDim, font);

    snprintf(line, sizeof(line), "bufory %u/%u",
             static_cast<unsigned>(gPipeline.pool(0)->available()),
             static_cast<unsigned>(gPipeline.pool(0)->capacityBlocks()));
    s.drawText(6, 56, line, kDim, font);

    drawButton(s, kPlay,  gPlaying);
    drawButton(s, kPause, !gPlaying);

    (void)s.flush();
}

/** Naciśnięcie w danym punkcie. Wspólne dla myszy i panelu dotykowego. */
void handleTap(i16 x, i16 y) {
    if (kPlay.contains(x, y) && !gPlaying) {
        if (gPipeline.resume()) { gPlaying = true; HYDRA_LOGI("gram"); }
    } else if (kPause.contains(x, y) && gPlaying) {
        // Pauza zatrzymuje **cały** potok, razem ze źródłem. Zatrzymanie
        // samego ujścia oznaczałoby źródło produkujące w próżnię i pulę,
        // która kończy się po kilku sekundach.
        if (gPipeline.pause()) { gPlaying = false; HYDRA_LOGI("pauza"); }
    }
}

// --- moduł ------------------------------------------------------------------

class PlayerModule : public ModuleBase {
public:
    PlayerModule() : ModuleBase("player") {}

protected:
    Status onInit() override {
#if HYDRA_PLAT_HOST
        SdlAudioSource::Config source;
        source.format = kFormat;
        source.framesPerBlock = kFramesPerBlock;
        HYDRA_CHECK(gSource.configure(source));

        SdlAudioSink::Config sink;
        // Sto milisekund zapasu. Mniej oznacza przerwy przy obciążeniu systemu
        // ogólnego przeznaczenia, więcej — zauważalne opóźnienie pauzy.
        sink.targetLatencyMs = 100;
        gSink.configure(sink);
#else
        FileSource::Config source;
        source.path = "muzyka.wav";
        source.framesPerBlock = kFramesPerBlock;
        source.loop = true;
        HYDRA_CHECK(gSource.configure(source));

        hal::I2sConfig i2s;
        i2s.sampleRate    = kSampleRate;
        i2s.bitsPerSample = 16;
        i2s.channels      = kChannels;
        i2s.bclk = 5;
        i2s.ws   = 6;
        i2s.dout = 7;
        gSink.configure(i2s);

        HYDRA_CHECK(gPanel.attach(ByteSpan{gVram, sizeof(gVram)},
                                  kScreenW, kScreenH, kPixelFormat));
#endif

        gGain.setGainQ8_8(256);   // bez zmiany; suwak głośności to osobna sprawa

        HYDRA_CHECK(gPipeline.addPool(ByteSpan{gPoolStorage, sizeof(gPoolStorage)},
                                      kBlockBytes, kBlockCount, 32));
        HYDRA_CHECK(gPipeline.add(gSource, kAudio));
        HYDRA_CHECK(gPipeline.add(gGain,   kAudio));
        HYDRA_CHECK(gPipeline.add(gSink,   kAudio));
        HYDRA_CHECK(gPipeline.link(gSource, gGain));
        HYDRA_CHECK(gPipeline.link(gGain, gSink));

        return gPipeline.prepare();
    }

    Status onStart() override {
        EventBus::subscribe<MediaFaultRaised>([](const MediaFaultRaised& e) {
            HYDRA_LOGW("zakłócenie %s (razem %lu)", toString(e.fault),
                       static_cast<unsigned long>(e.total));
        });

        if (auto r = gPipeline.start(); !r) {
            // Brak karty dźwiękowej albo pliku nie jest powodem, żeby nie
            // pokazać ekranu — użytkownik ma zobaczyć, co jest nie tak.
            HYDRA_LOGW("potok nie ruszył (%s) — interfejs działa dalej",
                       toString(r.error()));
            return ok();
        }
        // Startujemy w pauzie: dźwięk lecący od razu po włączeniu zaskakuje,
        // a przycisk PLAY istnieje po to, żeby go nacisnąć.
        (void)gPipeline.pause();
        gPlaying = false;
        return ok();
    }

    void onStop() override { gPipeline.stop(); }
};

PlayerModule gPlayer;

#if HYDRA_PLAT_HOST
StdoutLogSink gConsole;
#else
UartLogSink   gConsole;
#endif

}  // namespace

// ---------------------------------------------------------------------------
// Cel `podglad`: okno na pulpicie
// ---------------------------------------------------------------------------

#if HYDRA_PLAT_HOST

int main() {
    App::config()
        .name("media-player")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gPlayer);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return 1;
    }

    gfx::SdlDisplay::Cfg window;
    window.title  = "media-player";
    window.width  = kScreenW;
    window.height = kScreenH;
    window.scale  = HYDRA_NATIVE_WINDOW_SCALE;
    window.format = kPixelFormat;

    if (auto r = gDisplay.begin(ByteSpan{gVram, sizeof(gVram)}, window); !r) {
        HYDRA_LOGW("okno niedostępne (%s) — zbuduj z SDL2, żeby zobaczyć interfejs",
                   toString(r.error()));
        App::stop();
        return 0;
    }

    // Klawiatura obok myszy: spacja to odruch, a przy pracy nad dźwiękiem
    // sięganie po mysz oznacza spuszczenie z oka wskaźnika poziomu.
    gDisplay.setKeyHandler([](u32 key, bool down) {
        if (!down) return;
        if (key == ' ') handleTap(gPlaying ? kPause.box.x : kPlay.box.x,
                                  kPlay.box.y);
    });

    bool wasDown = false;
    while (gDisplay.pump()) {
        const gfx::SdlDisplay::Pointer p = gDisplay.pointer();
        // Reagujemy na zbocze, nie na stan: przytrzymany przycisk myszy
        // przełączałby odtwarzanie kilkadziesiąt razy na sekundę.
        if (p.down && !wasDown) handleTap(p.x, p.y);
        wasDown = p.down;

        gPipeline.step(kAudio, App::uptimeMs() * 1000ull);
        gPipeline.step(kUi, App::uptimeMs() * 1000ull);
        drawScreen();
    }

    App::stop();
    return 0;
}

// ---------------------------------------------------------------------------
// Cel `esp32s3`: panel i taski
// ---------------------------------------------------------------------------

#else

namespace {

Task gAudioTask;
Task gUiTask;

/**
 * Panel dostarcza projekt.
 *
 * Tutaj `Framebuffer` rysuje do pamięci, a wypchnięcie na wyświetlacz jest
 * puste — podmiana na adapter z `gfx/adapters/` to trzy linie:
 *
 *     TFT_eSPI tft;
 *     hydra::gfx::TftEspiSurface<TFT_eSPI> surface(tft);
 *     // i `screen()` zwraca `surface` zamiast `gPanel`
 */
Status presentToPanel(CByteSpan, gfx::Size, gfx::PixelFormat) { return ok(); }

class UiModule : public ModuleBase {
public:
    UiModule() : ModuleBase("ui") {}

protected:
    Status onInit() override { gPanel.setPresent(presentToPanel); return ok(); }

    Status onStart() override {
        Task::Cfg audio;
        audio.name = "media.audio";
        audio.prio = Prio::High;
        HYDRA_CHECK(gAudioTask.startPeriodic(audio, 4, [] {
            gPipeline.step(kAudio, App::uptimeMs() * 1000ull);
        }));

        Task::Cfg ui;
        ui.name = "media.ui";
        ui.prio = Prio::Low;
        return gUiTask.startPeriodic(ui, 33, [] {
            gPipeline.step(kUi, App::uptimeMs() * 1000ull);
            drawScreen();
        });
    }

    void onStop() override {
        gAudioTask.stopAndWait();
        gUiTask.stopAndWait();
    }
};

UiModule gUi;

}  // namespace

void setup() {
    App::config()
        .name("media-player")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gPlayer)
        .add(gUi);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Panel dotykowy woła `handleTap(x, y)` — ta sama funkcja co mysz na PC.
}

#endif
