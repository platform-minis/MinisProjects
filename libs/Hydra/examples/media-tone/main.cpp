/**
 * Hydra — przykład: media-tone.
 *
 * Najmniejszy sensowny potok multimedialny: generator tonu → wzmocnienie →
 * miernik. Bez ani jednego kawałka sprzętu, więc chodzi tak samo na ESP32
 * i na celu `native` — i o to chodzi, bo pozwala sprawdzić topologię, zanim
 * pojawi się przetwornik.
 *
 * Pokazuje trzy rzeczy, które w etapie 1 są całą treścią modułu:
 *
 *  1. **Pule dostarcza aplikacja.** Framework nie ma własnej pamięci; rozmiar
 *     bloku wprost przekłada się na opóźnienie, a liczba bloków — na zapas
 *     przy chwilowym obciążeniu. To są decyzje projektu urządzenia, a nie
 *     biblioteki.
 *  2. **Domeny mają różne okresy.** Źródło chodzi w rytmie próbek (co 4 ms
 *     wychodzi blok 64 ramek przy 16 kHz), miernik — co 100 ms, bo częściej
 *     nie ma po co. Na jednym tasku obie części musiałyby chodzić w tempie
 *     tej szybszej.
 *  3. **Zakłócenia są zdarzeniami.** Przerwa trwa cztery milisekundy i w logu
 *     ginie; licznik na magistrali pokazuje, że urządzenie nie wyrabia.
 *
 * Podmiana `ToneSource` na `I2sSource` (etap 2) albo `MeterSink` na
 * `SdlAudioSink` (etap 4) nie rusza niczego poniżej `setup()`.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/media/elements/Basic.hpp"

HYDRA_LOG_MODULE("tone")

using namespace hydra;
using namespace hydra::media;

namespace {

// --- parametry strumienia ---------------------------------------------------

constexpr u32 kSampleRate = 16000;
constexpr u16 kFramesPerBlock = 64;              ///< 4 ms przy 16 kHz
constexpr u32 kBlockBytes = kFramesPerBlock * 2; ///< S16 mono
constexpr u16 kBlockCount = 6;

/**
 * Pamięć puli.
 *
 * Statyczna, bo po `App::begin()` nic się nie alokuje — a tu akurat widać,
 * po co ta reguła istnieje: sześć bloków po 128 bajtów to cały budżet
 * pamięciowy strumienia i widać go w jednej linijce.
 */
u8 gPoolStorage[kBlockBytes * kBlockCount + 8];

// --- domeny -----------------------------------------------------------------

constexpr DomainId kCapture = 0;   ///< produkcja próbek — rytm strumienia
constexpr DomainId kSlow    = 1;   ///< pomiar i raportowanie

// --- graf -------------------------------------------------------------------

Pipeline   gPipeline;
ToneSource gTone;
Gain       gGain;
MeterSink  gMeter;

class MediaModule : public ModuleBase {
public:
    MediaModule() : ModuleBase("media") {}

protected:
    Status onInit() override {
        ToneSource::Config tone;
        tone.format = MediaFormat::audio(kSampleRate, SampleFormat::S16, 1);
        tone.frequencyHz = 440;
        tone.amplitude = 12000;
        tone.framesPerBlock = kFramesPerBlock;
        HYDRA_CHECK(gTone.configure(tone));

        HYDRA_CHECK(gPipeline.addPool(ByteSpan{gPoolStorage, sizeof(gPoolStorage)},
                                      kBlockBytes, kBlockCount));

        HYDRA_CHECK(gPipeline.add(gTone,  kCapture));
        HYDRA_CHECK(gPipeline.add(gGain,  kCapture));
        HYDRA_CHECK(gPipeline.add(gMeter, kSlow));

        // Wewnątrz domeny wolno gubić najstarszy blok; przez granicę domen
        // potok sam zamieni to na DropNewest, bo wyjmowanie z dwóch wątków
        // byłoby wyścigiem.
        HYDRA_CHECK(gPipeline.link(gTone, gGain));
        HYDRA_CHECK(gPipeline.link(gGain, gMeter));

        return gPipeline.prepare();
    }

    Status onStart() override {
        EventBus::subscribe<MediaFaultRaised>([](const MediaFaultRaised& e) {
            HYDRA_LOGW("zakłócenie %s w elemencie #%u (razem %lu)",
                       toString(e.fault), static_cast<unsigned>(e.element),
                       static_cast<unsigned long>(e.total));
        });

        HYDRA_CHECK(gPipeline.start());

        Task::Cfg capture;
        capture.name = "media.capture";
        capture.prio = Prio::High;
        // Okres krótszy niż blok: przy 4 ms na blok i kroku co 2 ms źródło
        // ma zapas na chwilowe spóźnienie taska.
        HYDRA_CHECK(captureTask_.startPeriodic(capture, 2, [] {
            gPipeline.step(kCapture, App::uptimeMs() * 1000ull);
        }));

        Task::Cfg slow;
        slow.name = "media.slow";
        slow.prio = Prio::Low;
        return slowTask_.startPeriodic(slow, 100, [this] { report(); });
    }

    void onStop() override {
        captureTask_.stopAndWait();
        slowTask_.stopAndWait();
        gPipeline.stop();
    }

private:
    void report() {
        gPipeline.step(kSlow, App::uptimeMs() * 1000ull);

        if (++ticks_ % 10 != 0) return;   // raz na sekundę
        HYDRA_LOGI("bloki %lu, szczyt %u, wolne bufory %u/%u",
                   static_cast<unsigned long>(gMeter.blocks()),
                   static_cast<unsigned>(gMeter.takePeak()),
                   static_cast<unsigned>(gPipeline.pool(0)->available()),
                   static_cast<unsigned>(gPipeline.pool(0)->capacityBlocks()));
    }

    Task captureTask_{};
    Task slowTask_{};
    u32  ticks_ = 0;
};

MediaModule gMedia;
UartLogSink gConsole;

}  // namespace

void setup() {
    App::config()
        .name("media-tone")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gMedia);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Potok chodzi w taskach domen; pętla główna nie ma tu nic do roboty.
}
