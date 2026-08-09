/**
 * Hydra — przykład: media-native.
 *
 * Ten sam potok, który na ESP32 gra przez I2S, tutaj gra przez kartę dźwiękową
 * laptopa. Zmienia się jedno ujście — reszta grafu, formaty i pule zostają
 * bez zmian.
 *
 *     cmake -B build -S examples/media-native -D HYDRA_ROOT=$PWD
 *     cmake --build build && ./build/media-native
 *
 * Bez zainstalowanego SDL program się zbuduje i uruchomi, tylko bez dźwięku:
 * `SdlAudioSink::onStart()` zwróci `NotSupported`, a my to zgłosimy i skończymy.
 * To jest normalna ścieżka dla CI i sesji ssh, nie awaria.
 *
 * **Jeden task, nie trzy.** Cały potok chodzi w pętli głównej, bo na PC nie ma
 * czego priorytetyzować — a przy okazji widać, że domeny są etykietą, nie
 * wątkiem: `stepAll()` obsługuje wszystkie naraz i potok o tym nie wie.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  error "Przykład media-native buduje się wyłącznie dla celu native."
#endif

#include <Hydra.h>

#include <stdio.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/media/elements/Basic.hpp"
#include "hydra/media/elements/Sdl.hpp"

HYDRA_LOG_MODULE("native-audio")

using namespace hydra;
using namespace hydra::media;

namespace {

constexpr u32 kSampleRate     = 44100;
constexpr u16 kFramesPerBlock = 512;                  ///< ~12 ms
constexpr u32 kBlockBytes     = kFramesPerBlock * 2;  ///< S16 mono
constexpr u16 kBlockCount     = 8;

u8 gPoolStorage[kBlockBytes * kBlockCount + 32];

Pipeline     gPipeline;
ToneSource   gTone;
Gain         gGain;
SdlAudioSink gSpeakers;

StdoutLogSink gConsole;

/** Melodia: cztery dźwięki po pół sekundy, w kółko. */
constexpr u32 kMelody[] = {440, 554, 659, 880};
constexpr u32 kNoteMs = 500;

}  // namespace

int main() {
    App::config()
        .name("media-native")
        .logLevel(LogLevel::Info)
        .logSink(gConsole);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return 1;
    }

    ToneSource::Config tone;
    tone.format = MediaFormat::audio(kSampleRate, SampleFormat::S16, 1);
    tone.frequencyHz = kMelody[0];
    tone.amplitude = 9000;
    tone.framesPerBlock = kFramesPerBlock;
    gTone.configure(tone);

    SdlAudioSink::Config sink;
    // Sto milisekund zapasu. Mniej oznacza przerwy przy każdym większym
    // obciążeniu systemu ogólnego przeznaczenia, więcej — zauważalne
    // opóźnienie reakcji na zmianę dźwięku poniżej.
    sink.targetLatencyMs = 100;
    gSpeakers.configure(sink);

    gPipeline.addPool(ByteSpan{gPoolStorage, sizeof(gPoolStorage)},
                      kBlockBytes, kBlockCount);
    gPipeline.add(gTone);
    gPipeline.add(gGain);
    gPipeline.add(gSpeakers);
    gPipeline.link(gTone, gGain);
    gPipeline.link(gGain, gSpeakers);

    EventBus::subscribe<MediaFaultRaised>([](const MediaFaultRaised& e) {
        HYDRA_LOGW("zakłócenie %s (razem %lu)", toString(e.fault),
                   static_cast<unsigned long>(e.total));
    });

    if (auto r = gPipeline.prepare(); !r) {
        HYDRA_LOGE("przygotowanie potoku: %s", toString(r.error()));
        return 1;
    }
    if (auto r = gPipeline.start(); !r) {
        HYDRA_LOGW("brak wyjścia audio (%s) — zbuduj z SDL2, żeby usłyszeć dźwięk",
                   toString(r.error()));
        App::stop();
        return 0;
    }

    HYDRA_LOGI("gram — Ctrl+C kończy");

    const u64 startMs = App::uptimeMs();
    u32 note = 0;

    while (App::uptimeMs() - startMs < 8000) {
        const u32 wanted = static_cast<u32>((App::uptimeMs() - startMs) / kNoteMs) % 4;
        if (wanted != note) {
            note = wanted;
            ToneSource::Config next = tone;
            next.frequencyHz = kMelody[note];
            // Przestrojenie generatora w locie. Faza jest zachowana, więc
            // między dźwiękami nie ma trzasku — skok fazy słychać wyraźniej
            // niż samą zmianę wysokości.
            gTone.configure(next);
        }

        gPipeline.stepAll(App::uptimeMs() * 1000ull);
        // Krok co dwie milisekundy. Krócej to zajmowanie rdzenia bez powodu:
        // ujście i tak nie przyjmie więcej, niż zmieści się w zapasie.
        rtos::delayMs(2);
    }

    HYDRA_LOGI("koniec: %lu przerw, %lu wstrzymań",
               static_cast<unsigned long>(gSpeakers.underruns()),
               static_cast<unsigned long>(gSpeakers.throttled()));

    gPipeline.stop();
    App::stop();
    return 0;
}
