/**
 * Hydra — przykład: media-i2s.
 *
 * Mikrofon → wzmocnienie → głośnik, całość po I2S. Klasyczne „przesłuchanie":
 * najkrótszy potok, na którym słychać, czy sprzęt w ogóle działa i jakie ma
 * opóźnienie.
 *
 * Wszystko dzieje się w jednej domenie i to nie jest uproszczenie, tylko
 * właściwy wybór: wejście i wyjście chodzą w tym samym rytmie próbek, więc
 * rozdzielenie ich na dwa taski dodałoby przekazanie przez kolejkę bez ani
 * jednej korzyści. Domeny rozdziela się tam, gdzie różni się **priorytet**,
 * a nie tam, gdzie różni się kierunek.
 *
 * Opóźnienie wynika z trzech rzeczy i widać je w liczbach poniżej:
 * bloku (128 ramek = 8 ms przy 16 kHz), zapasu buforów u sterownika
 * i okresu taska. Zmniejszanie bloku skraca opóźnienie i podnosi ryzyko
 * przerw — to jest ten kompromis, który trzeba wybrać świadomie.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/media/elements/Audio.hpp"
#include "hydra/media/elements/Basic.hpp"

HYDRA_LOG_MODULE("i2s-demo")

using namespace hydra;
using namespace hydra::media;

namespace {

constexpr u32 kSampleRate     = 16000;
constexpr u16 kFramesPerBlock = 128;               ///< 8 ms przy 16 kHz
constexpr u32 kBlockBytes     = kFramesPerBlock * 2;  ///< S16 mono
constexpr u16 kBlockCount     = 8;

/**
 * Bufory wyrównane do linii pamięci podręcznej.
 *
 * Kontroler DMA zapisuje z jej pominięciem; niewyrównany początek oznacza
 * uszkodzenie sąsiednich danych przy czyszczeniu. Pula wyrównuje sama, ale
 * musi mieć z czego — stąd zapas.
 */
u8 gPoolStorage[kBlockBytes * kBlockCount + 64];

Pipeline   gPipeline;
I2sSource  gMic{hal::Hal::i2s()};
Gain       gGain;
I2sSink    gSpeaker{hal::Hal::i2s()};

/**
 * Ten sam kontroler po obu stronach.
 *
 * Na ESP32 jeden port I2S pracuje dwukierunkowo, więc mikrofon i głośnik
 * dzielą zegar — i tylko dlatego nie potrzeba tu przepróbkowania. Przy dwóch
 * osobnych przetwornikach z własnymi kwarcami trzeba dołożyć resampler,
 * bo 16 000 Hz jednego i 16 000 Hz drugiego to dwie różne częstotliwości.
 */
hal::I2sConfig i2sConfig() {
    hal::I2sConfig cfg;
    cfg.sampleRate    = kSampleRate;
    cfg.bitsPerSample = 16;
    cfg.channels      = 1;
    cfg.standard      = hal::I2sStandard::Philips;
    cfg.bclk = 5;
    cfg.ws   = 6;
    cfg.dout = 7;    // do wzmacniacza
    cfg.din  = 8;    // z mikrofonu
    return cfg;
}

class AudioModule : public ModuleBase {
public:
    AudioModule() : ModuleBase("audio") {}

protected:
    Status onInit() override {
        gMic.configure(i2sConfig());
        gMic.setFramesPerBlock(kFramesPerBlock);
        gSpeaker.configure(i2sConfig());

        // ×2. Mikrofony MEMS dają sygnał cichy; wzmocnienie w potoku jest
        // tańsze niż stopień analogowy i da się je zmienić bez lutownicy.
        gGain.setGainQ8_8(512);

        HYDRA_CHECK(gPipeline.addPool(ByteSpan{gPoolStorage, sizeof(gPoolStorage)},
                                      kBlockBytes, kBlockCount, 32));
        HYDRA_CHECK(gPipeline.add(gMic));
        HYDRA_CHECK(gPipeline.add(gGain));
        HYDRA_CHECK(gPipeline.add(gSpeaker));
        HYDRA_CHECK(gPipeline.link(gMic, gGain));
        HYDRA_CHECK(gPipeline.link(gGain, gSpeaker));

        return gPipeline.prepare();
    }

    Status onStart() override {
        EventBus::subscribe<MediaFaultRaised>([](const MediaFaultRaised& e) {
            // Przerwa trwa osiem milisekund i w logu ginie; liczba narastająca
            // pokazuje, że urządzenie nie wyrabia.
            HYDRA_LOGW("zakłócenie %s (razem %lu)", toString(e.fault),
                       static_cast<unsigned long>(e.total));
        });

        HYDRA_CHECK(gPipeline.start());

        Task::Cfg audio;
        audio.name = "media.audio";
        audio.prio = Prio::High;
        // Okres krótszy niż blok, żeby bufor u sterownika nigdy się nie skończył
        // w chwili, gdy task akurat czeka na swoją kolej.
        HYDRA_CHECK(task_.startPeriodic(audio, 4, [] {
            gPipeline.stepAll(App::uptimeMs() * 1000ull);
        }));

        Task::Cfg report;
        report.name = "media.report";
        report.prio = Prio::Low;
        return reportTask_.startPeriodic(report, 5000, [this] { report_(); });
    }

    void onStop() override {
        task_.stopAndWait();
        reportTask_.stopAndWait();
        gPipeline.stop();
    }

private:
    void report_() {
        HYDRA_LOGI("ramki %lu, wysłane bloki %lu, xrun %lu, wolne bufory %u/%u",
                   static_cast<unsigned long>(gMic.framesCaptured()),
                   static_cast<unsigned long>(gSpeaker.submitted()),
                   static_cast<unsigned long>(gSpeaker.xruns()),
                   static_cast<unsigned>(gPipeline.pool(0)->available()),
                   static_cast<unsigned>(gPipeline.pool(0)->capacityBlocks()));
    }

    Task task_{};
    Task reportTask_{};
};

AudioModule gAudio;
UartLogSink gConsole;

}  // namespace

void setup() {
    App::config()
        .name("media-i2s")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gAudio);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Potok chodzi w tasku media.audio.
}
