/**
 * wav-player — odtwarzacz `test.wav` z ekranem, ten sam kod na każdym celu.
 *
 *     FileSource("test.wav") → Gain → <ujście zależne od celu>
 *
 * Źródło, graf, sterowanie i cały interfejs są wspólne. Różnica mieści się
 * w **jednym obiekcie** — ujściu — i w tym, skąd bierze się nośnik.
 *
 * ── Rzecz, która przesądza o kształcie tego pliku ──────────────────────────
 *
 * **Format bierze się z pliku, a nie z konfiguracji.** Nagłówek WAV mówi, czy
 * to 44,1 kHz stereo, czy 8 kHz mono — i dopiero wtedy wiadomo, jak ustawić
 * kontroler I2S i jak duży jest blok. Plik 44,1 kHz odtworzony jako 16 kHz
 * brzmi jak nagranie zwolnione i wygląda na usterkę przetwornika.
 *
 * Dlatego `onInit()` robi rzeczy w tej kolejności:
 *
 *     1. FileSource::negotiate()   ← otwiera plik i czyta nagłówek
 *     2. konfiguracja ujścia       ← z formatu odczytanego z pliku
 *     3. addPool()                 ← rozmiar bloku też z formatu
 *     4. add / link / prepare
 *
 * Wywołanie `negotiate()` wprost wygląda na obejście, a jest jedynym miejscem,
 * w którym aplikacja ma prawo poznać format **przed** przygotowaniem potoku.
 * Alternatywą byłoby wpisanie 44,1 kHz na sztywno i granie wszystkiego
 * z niewłaściwą prędkością poza jednym plikiem.
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
#include "hydra/media/elements/Files.hpp"

#if HYDRA_PLAT_HOST
#  include "hydra/gfx/SdlDisplay.hpp"
#  include "hydra/media/elements/Sdl.hpp"
#else
#  include "hydra/media/elements/Audio.hpp"
#endif

HYDRA_LOG_MODULE("wav")

using namespace hydra;
using namespace hydra::media;

#if !HYDRA_PLAT_HOST
/**
 * Nośnik dostarcza projekt urządzenia — karta SD albo LittleFS we flashu.
 * Hydra nie ma backendu żadnego z nich, bo to decyzja o urządzeniu.
 *
 * Poza anonimową przestrzenią nazw: w środku deklaracja odnosiłaby się do
 * symbolu wewnętrznego tej jednostki translacji, którego nikt nie miałby jak
 * zdefiniować.
 */
hydra::hal::IFileSystem& projectFileSystem();
#endif

namespace {

constexpr const char* kFile = "test.wav";

/** Ramki w bloku. Wprost wyznaczają opóźnienie reakcji na pauzę. */
constexpr u16 kFramesPerBlock = 512;
/** Najgorszy przypadek: stereo 16-bit. Pula jest statyczna, więc liczy się on. */
constexpr u32 kMaxBlockBytes = kFramesPerBlock * 2 * 2;
constexpr u16 kBlockCount    = 8;

u8 gPoolStorage[kMaxBlockBytes * kBlockCount + 64];

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

constexpr DomainId kAudio = 0;
constexpr DomainId kUi    = 1;

Pipeline gPipeline;
Gain     gGain;

hal::IFileSystem& storage() {
#if HYDRA_PLAT_HOST
    // Katalog, z którego uruchomiono program — backend hostowy rejestruje go
    // sam. `test.wav` kładzie się więc obok binarki i podmienia bez budowania.
    return hal::Hal::fileSystem();
#else
    return projectFileSystem();
#endif
}

FileSource gSource{storage()};

#if HYDRA_PLAT_HOST
gfx::SdlDisplay gDisplay;
SdlAudioSink    gSink;
#else
I2sSink          gSink{hal::Hal::i2s()};
gfx::Framebuffer gPanel;
#endif

gfx::ISurface& screen() {
#if HYDRA_PLAT_HOST
    return gDisplay.surface();
#else
    return gPanel;
#endif
}

// --- stan -------------------------------------------------------------------

bool        gPlaying = false;
bool        gReady   = false;   ///< czy potok w ogóle ruszył
MediaFormat gFormat{};
char        gProblem[64] = {};  ///< co poszło nie tak, jeśli poszło

// --- interfejs --------------------------------------------------------------

const gfx::Color kBg      = gfx::Color::rgb(16, 20, 28);
const gfx::Color kBar     = gfx::Color::rgb(30, 38, 52);
const gfx::Color kAccent  = gfx::Color::rgb(224, 153, 63);
const gfx::Color kText    = gfx::Color::rgb(226, 232, 240);
const gfx::Color kDim     = gfx::Color::rgb(110, 124, 145);
const gfx::Color kBad     = gfx::Color::rgb(220, 80, 80);

/**
 * Jeden przycisk, nie dwa.
 *
 * Play i pauza wykluczają się, więc dwa przyciski oznaczają, że jeden jest
 * zawsze nieczynny — a użytkownik i tak celuje w ten, który akurat działa.
 * Przełącznik pokazuje **czynność**, nie stan: gdy gra, na przycisku jest
 * „PAUSE", bo to on się stanie po naciśnięciu.
 */
constexpr gfx::Rect kButton(kScreenW / 2 - 44, kScreenH - 42, 88, 30);

bool inButton(i16 x, i16 y) {
    return x >= kButton.x && x < kButton.x + kButton.w &&
           y >= kButton.y && y < kButton.y + kButton.h;
}

/** Postęp 0…1000. Liczony w tysięcznych, żeby nie sięgać po liczby ułamkowe. */
u16 progressPermille() {
    const u32 total = gSource.info().dataBytes;
    if (total == 0 || gFormat.unitBytes() == 0) return 0;
    const u64 done = gSource.framesRead() * gFormat.unitBytes();
    return done >= total ? 1000u : static_cast<u16>(done * 1000ull / total);
}

void drawScreen() {
    gfx::ISurface& s = screen();
    const gfx::Font& font = gfx::font8x8();

    s.fill(kBg);

    s.fillRect(gfx::Rect(0, 0, kScreenW, 18), kBar);
    s.drawText(6, 5, "wav-player", kAccent, font);
    s.drawText(static_cast<i16>(kScreenW - 6 - gfx::textWidth(font, hal::board::name)),
               5, hal::board::name, kDim, font);

    // Nazwa pliku — to, o co prosi każdy, kto patrzy na odtwarzacz.
    s.drawText(6, 28, kFile, kText, font);

    char line[48];
    if (gReady) {
        snprintf(line, sizeof(line), "%lu Hz  %s  %s",
                 static_cast<unsigned long>(gFormat.sampleRate),
                 gFormat.channels == 2 ? "stereo" : "mono",
                 gPlaying ? "gra" : "pauza");
        s.drawText(6, 42, line, gPlaying ? kAccent : kDim, font);
    } else {
        s.drawText(6, 42, gProblem[0] ? gProblem : "brak pliku", kBad, font);
    }

    // Pasek postępu. Sam prostokąt zamiast liczby: przy odtwarzaniu z pliku
    // interesuje „ile zostało", a nie ile dokładnie sekund minęło.
    const gfx::Rect track(6, 62, kScreenW - 12, 8);
    s.drawRect(track, kDim);
    if (gReady) {
        const i16 filled = static_cast<i16>((track.w - 2) * progressPermille() / 1000);
        if (filled > 0) {
            s.fillRect(gfx::Rect(static_cast<i16>(track.x + 1),
                                 static_cast<i16>(track.y + 1),
                                 filled, static_cast<i16>(track.h - 2)), kAccent);
        }
    }

    // Przycisk. Aktywny wypełniony, spoczynkowy obrysowany — jedyne
    // rozróżnienie, jakie zostaje na panelu monochromatycznym.
    const char* label = gPlaying ? "PAUSE" : "PLAY";
    if (gPlaying) s.fillRect(kButton, kAccent);
    else          s.drawRect(kButton, gReady ? kText : kDim);

    const i16 textW = gfx::textWidth(font, label);
    s.drawText(static_cast<i16>(kButton.x + (kButton.w - textW) / 2),
               static_cast<i16>(kButton.y + (kButton.h - 8) / 2),
               label, gPlaying ? kBg : (gReady ? kText : kDim), font);

    (void)s.flush();
}

/** Naciśnięcie — wspólne dla myszy, klawisza i panelu dotykowego. */
void toggle() {
    if (!gReady) return;

    if (gPlaying) {
        // Pauza zatrzymuje **cały** potok, razem ze źródłem. Zatrzymanie
        // samego ujścia oznaczałoby czytanie pliku w próżnię i pulę, która
        // kończy się po kilku sekundach.
        if (gPipeline.pause()) { gPlaying = false; HYDRA_LOGI("pauza"); }
    } else {
        if (gPipeline.resume()) { gPlaying = true; HYDRA_LOGI("gram"); }
    }
}

void note(const char* text) {
    snprintf(gProblem, sizeof(gProblem), "%s", text);
}

// --- moduł ------------------------------------------------------------------

class PlayerModule : public ModuleBase {
public:
    PlayerModule() : ModuleBase("player") {}

protected:
    Status onInit() override {
#if !HYDRA_PLAT_HOST
        HYDRA_CHECK(gPanel.attach(ByteSpan{gVram, sizeof(gVram)},
                                  kScreenW, kScreenH, kPixelFormat));
#endif
        // Ekran musi działać nawet wtedy, gdy pliku nie ma — bo to właśnie
        // wtedy użytkownik potrzebuje się dowiedzieć, czego brakuje.
        if (!storage().mounted() && !storage().mount()) {
            note("brak nosnika");
            return ok();
        }

        FileSource::Config source;
        source.path = kFile;
        source.framesPerBlock = kFramesPerBlock;
        source.loop = true;
        HYDRA_CHECK(gSource.configure(source));

        // Krok 1: nagłówek. Dopiero on mówi, jak ustawić sprzęt i pulę.
        auto format = gSource.negotiate(0, MediaFormat{});
        if (!format) {
            HYDRA_LOGE("%s: %s", kFile, toString(format.error()));
            note("nie moge otworzyc pliku");
            return ok();
        }
        gFormat = *format;

        const u32 blockBytes = static_cast<u32>(kFramesPerBlock) * gFormat.unitBytes();
        if (blockBytes > kMaxBlockBytes) {
            // Więcej niż stereo 16-bit nie zmieści się w statycznej puli.
            // Odmowa jest lepsza niż blok obcięty w połowie ramki.
            note("format za szeroki");
            return ok();
        }

        // Krok 2: ujście z formatu pliku.
#if HYDRA_PLAT_HOST
        SdlAudioSink::Config sink;
        sink.targetLatencyMs = 100;
        gSink.configure(sink);
#else
        hal::I2sConfig i2s;
        i2s.sampleRate    = gFormat.sampleRate;
        i2s.bitsPerSample = 16;
        i2s.channels      = gFormat.channels;
        i2s.bclk = 5;
        i2s.ws   = 6;
        i2s.dout = 7;
        gSink.configure(i2s);
#endif

        gGain.setGainQ8_8(256);   // bez zmiany

        // Krok 3: pula o rozmiarze wynikającym z formatu.
        HYDRA_CHECK(gPipeline.addPool(ByteSpan{gPoolStorage, sizeof(gPoolStorage)},
                                      blockBytes, kBlockCount, 32));
        HYDRA_CHECK(gPipeline.add(gSource, kAudio));
        HYDRA_CHECK(gPipeline.add(gGain,   kAudio));
        HYDRA_CHECK(gPipeline.add(gSink,   kAudio));
        HYDRA_CHECK(gPipeline.link(gSource, gGain));
        HYDRA_CHECK(gPipeline.link(gGain, gSink));

        if (auto r = gPipeline.prepare(); !r) {
            HYDRA_LOGE("przygotowanie potoku: %s", toString(r.error()));
            note("potok odmowil");
            return ok();
        }

        HYDRA_LOGI("%s: %lu Hz, %u kan., %lu B danych", kFile,
                   static_cast<unsigned long>(gFormat.sampleRate),
                   static_cast<unsigned>(gFormat.channels),
                   static_cast<unsigned long>(gSource.info().dataBytes));
        return ok();
    }

    Status onStart() override {
        EventBus::subscribe<MediaFaultRaised>([](const MediaFaultRaised& e) {
            HYDRA_LOGW("zakłócenie %s (razem %lu)", toString(e.fault),
                       static_cast<unsigned long>(e.total));
        });

        if (gProblem[0] != '\0') return ok();   // ekran pokaże powód

        if (auto r = gPipeline.start(); !r) {
            HYDRA_LOGW("wyjście audio niedostępne (%s)", toString(r.error()));
            note("brak wyjscia audio");
            return ok();
        }

        // Startujemy w pauzie: dźwięk lecący od razu po włączeniu zaskakuje,
        // a przycisk istnieje po to, żeby go nacisnąć.
        (void)gPipeline.pause();
        gPlaying = false;
        gReady   = true;
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

void run() {
    App::config()
        .name("wav-player")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gPlayer);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Cel `podglad`: okno na pulpicie
// ---------------------------------------------------------------------------

#if HYDRA_PLAT_HOST

int main() {
    run();

    gfx::SdlDisplay::Cfg window;
    window.title  = "wav-player";
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

    // Spacja obok myszy: przy pracy nad dźwiękiem sięganie po mysz oznacza
    // spuszczenie z oka wskaźnika poziomu.
    gDisplay.setKeyHandler([](u32 key, bool down) {
        if (down && key == ' ') toggle();
    });

    bool wasDown = false;
    while (gDisplay.pump()) {
        const gfx::SdlDisplay::Pointer p = gDisplay.pointer();
        // Reagujemy na zbocze, nie na stan: przytrzymany przycisk myszy
        // przełączałby odtwarzanie kilkadziesiąt razy na sekundę.
        if (p.down && !wasDown && inButton(p.x, p.y)) toggle();
        wasDown = p.down;

        const u64 nowUs = App::uptimeMs() * 1000ull;
        gPipeline.step(kAudio, nowUs);
        gPipeline.step(kUi, nowUs);
        drawScreen();
    }

    App::stop();
    return 0;
}

// ---------------------------------------------------------------------------
// Cele sprzętowe: panel i taski
// ---------------------------------------------------------------------------

#else

namespace {

Task gAudioTask;
Task gUiTask;

/**
 * Wypchnięcie na panel dostarcza projekt.
 *
 * Podmiana na adapter z `gfx/adapters/` to trzy linie:
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
        .name("wav-player")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gPlayer)
        .add(gUi);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Panel dotykowy woła `toggle()` — ta sama funkcja co mysz i spacja.
}

#endif
