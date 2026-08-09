/**
 * Hydra — przykład: media-record.
 *
 * Mikrofon → rozgałęzienie → {plik na karcie, strumień po sieci}. Nagrywanie
 * z jednoczesnym podglądem zdalnym: to, po co w ogóle istnieje `Tee`.
 *
 * **Trzy domeny, bo trzy różne rytmy.** Przetwornik chodzi w takt próbek
 * i spóźnienie słychać; karta SD potrafi zamilknąć na sto milisekund przy
 * kasowaniu sektora; gniazdo TCP blokuje się, gdy druga strona nie czyta.
 * Gdyby wszystko siedziało w jednym tasku, każde z tych zdarzeń zabierałoby
 * czas pozostałym dwóm — a najgorsze skutki miałoby to dla mikrofonu, czyli
 * dla jedynego elementu, którego nie da się nadrobić.
 *
 * Granice domen są też miejscami, w których polityka przepełnienia zaczyna
 * mieć znaczenie i różni się dla obu gałęzi:
 *
 *   • do pliku — `DropNewest`: kolejność ma znaczenie, dziura na końcu jest
 *     lepsza niż dziura w środku nagrania,
 *   • do sieci — też `DropNewest`, bo potok wymusza ją przez granicę domen;
 *     dla podglądu wolelibyśmy `DropOldest`, ale wyjmowanie z ogona przez
 *     producenta byłoby wyścigiem z konsumentem w drugim wątku.
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
#include "hydra/media/elements/Files.hpp"
#include "hydra/media/elements/Net.hpp"

HYDRA_LOG_MODULE("record");

using namespace hydra;
using namespace hydra::media;

namespace {

constexpr u32 kSampleRate     = 16000;
constexpr u16 kFramesPerBlock = 256;                  ///< 16 ms przy 16 kHz
constexpr u32 kBlockBytes     = kFramesPerBlock * 2;  ///< S16 mono
constexpr u16 kBlockCount     = 10;

/**
 * Dziesięć bloków, nie cztery.
 *
 * Rozgałęzienie trzyma każdy blok tak długo, jak **wolniejsza** z gałęzi —
 * a tą jest karta. Zapas policzony pod samą ścieżkę audio kończyłby się pustą
 * pulą przy pierwszym dłuższym zapisie.
 */
u8 gPoolStorage[kBlockBytes * kBlockCount + 64];

constexpr DomainId kAudio = 0;   ///< przetwornik — rytm próbek
constexpr DomainId kCard  = 1;   ///< karta SD
constexpr DomainId kNet   = 2;   ///< gniazdo

Pipeline gPipeline;

/**
 * Gdzie ląduje nagranie.
 *
 * Na celu `native` odpowiedź jest jedna — katalog, z którego uruchomiono
 * program — i backend hostowy rejestruje go sam, więc `Hal::fileSystem()`
 * wystarcza. Nagranie powstaje obok binarki i da się je otworzyć edytorem.
 *
 * Na układzie takiego wyboru nie ma: karta czy flash to decyzja urządzenia,
 * a Hydra nie ma dziś backendu żadnego z nich. Implementację wnosi projekt.
 */
#if !HYDRA_PLAT_HOST
extern hal::IFileSystem& projectFileSystem();   ///< definiuje projekt
#endif

hal::IFileSystem& storage() {
#if HYDRA_PLAT_HOST
    return hal::Hal::fileSystem();
#else
    return projectFileSystem();
#endif
}

I2sSource gMic{hal::Hal::i2s()};
Gain      gGain;
Tee       gTee;
FileSink  gFile{storage()};

/**
 * Gniazdo.
 *
 * Łączeniem i ponownym łączeniem zajmuje się `net::ConnectionManager`; element
 * tylko pisze do gotowego gniazda i sam z siebie niczego nie otwiera. Dzięki
 * temu backoff jest w jednym miejscu, a nie w każdym elemencie osobno.
 */
net::IClient* gSocket = nullptr;
NetSink*      gNetSink = nullptr;

class RecorderModule : public ModuleBase {
public:
    RecorderModule() : ModuleBase("recorder") {}

protected:
    Status onInit() override {
        hal::I2sConfig i2s;
        i2s.sampleRate = kSampleRate;
        i2s.bitsPerSample = 16;
        i2s.channels = 1;
        i2s.bclk = 5;
        i2s.ws   = 6;
        i2s.din  = 8;
        gMic.configure(i2s);
        gMic.setFramesPerBlock(kFramesPerBlock);

        gGain.setGainQ8_8(512);   // ×2 — mikrofony MEMS dają cichy sygnał

        FileSink::Config file;
        file.path = "nagranie.wav";
        // Uzupełniamy nagłówek co ~2 sekundy. Zanik zasilania kosztuje wtedy
        // ogon nagrania, a nie cały plik.
        file.patchEvery = 128;
        HYDRA_CHECK(gFile.configure(file));

        HYDRA_CHECK(gPipeline.addPool(ByteSpan{gPoolStorage, sizeof(gPoolStorage)},
                                      kBlockBytes, kBlockCount, 32));

        HYDRA_CHECK(gPipeline.add(gMic,  kAudio));
        HYDRA_CHECK(gPipeline.add(gGain, kAudio));
        HYDRA_CHECK(gPipeline.add(gTee,  kAudio));
        HYDRA_CHECK(gPipeline.add(gFile, kCard));

        HYDRA_CHECK(gPipeline.link(gMic, gGain));
        HYDRA_CHECK(gPipeline.link(gGain, gTee));
        HYDRA_CHECK(gPipeline.link(gTee, 0, gFile, 0, OverflowPolicy::DropNewest));

        // Gałąź sieciowa jest opcjonalna: bez gniazda nagranie ma dalej powstawać.
        // Niepodłączone wyjście `Tee` nie jest błędem — bloki są liczone
        // i zwalniane, a potok działa.
        if (gNetSink != nullptr) {
            HYDRA_CHECK(gPipeline.add(*gNetSink, kNet));
            HYDRA_CHECK(gPipeline.link(gTee, 1, *gNetSink, 0, OverflowPolicy::DropNewest));
        }

        return gPipeline.prepare();
    }

    Status onStart() override {
        EventBus::subscribe<MediaFaultRaised>([](const MediaFaultRaised& e) {
            HYDRA_LOGW("zakłócenie %s w elemencie #%u (razem %lu)",
                       toString(e.fault), static_cast<unsigned>(e.element),
                       static_cast<unsigned long>(e.total));
        });
        EventBus::subscribe<MediaEndOfStream>([](const MediaEndOfStream&) {
            HYDRA_LOGI("nagranie zamknięte");
        });

        HYDRA_CHECK(gPipeline.start());

        Task::Cfg audio;
        audio.name = "media.audio";
        audio.prio = Prio::High;
        HYDRA_CHECK(audioTask_.startPeriodic(audio, 8, [] {
            gPipeline.step(kAudio, App::uptimeMs() * 1000ull);
        }));

        Task::Cfg card;
        card.name = "media.card";
        card.prio = Prio::Low;
        HYDRA_CHECK(cardTask_.startPeriodic(card, 50, [] {
            gPipeline.step(kCard, App::uptimeMs() * 1000ull);
        }));

        Task::Cfg net;
        net.name = "media.net";
        net.prio = Prio::Low;
        return netTask_.startPeriodic(net, 50, [] {
            gPipeline.step(kNet, App::uptimeMs() * 1000ull);
        });
    }

    void onStop() override {
        audioTask_.stopAndWait();
        cardTask_.stopAndWait();
        netTask_.stopAndWait();
        // Zatrzymanie potoku uzupełnia nagłówek WAV i zamyka plik.
        gPipeline.stop();
    }

private:
    Task audioTask_{};
    Task cardTask_{};
    Task netTask_{};
};

RecorderModule gRecorder;
UartLogSink    gConsole;

}  // namespace

void setup() {
    App::config()
        .name("media-record")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gRecorder);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
    }
}

void loop() {
    // Wszystko w taskach domen: media.audio, media.card, media.net.
}
