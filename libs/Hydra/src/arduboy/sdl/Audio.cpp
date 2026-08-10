/**
 * Wyjście dźwiękowe celu natywnego — fala prostokątna przez SDL.
 *
 * Oryginalny Arduboy ma brzęczyk piezo sterowany wprost licznikiem: nóżka
 * przełącza się z częstotliwością nuty i tyle. Fala jest prostokątna nie
 * z wyboru estetycznego, tylko dlatego, że nic innego się z tego nie da
 * wycisnąć — i właśnie tak brzmią te gry. Odtwarzamy więc prostokąt, a nie
 * sinusa: sinus brzmiałby „ładniej" i nie brzmiałby jak Arduboy.
 *
 * ## Dlaczego wywołanie zwrotne, a nie kolejkowanie
 *
 * `SDL_QueueAudio` wymagałoby dokładania próbek w tempie odtwarzania z pętli
 * gry. Pętla gry stoi jednak na `nextFrame()` i potrafi się zaciąć na jedną
 * klatkę przy przeciążeniu — a wtedy kolejka pustoszeje i słychać trzask.
 * Wywołanie zwrotne generuje próbki samo, w rytmie karty dźwiękowej, a z pętli
 * gry przychodzi wyłącznie **jedna liczba**: bieżąca częstotliwość.
 *
 * ## Wątki
 *
 * Wywołanie zwrotne biegnie w wątku SDL, `setTone()` woła wątek gry. Wymiana
 * idzie przez zmienne atomowe — nie ma tu żadnej struktury, którą trzeba by
 * chronić muteksem, bo cały stan to częstotliwość i głośność.
 *
 * ## Narastanie i opadanie
 *
 * Amplituda dochodzi do wartości docelowej przez kilka milisekund zamiast
 * skakać. Skok daje nieciągłość przebiegu, którą słychać jako strzał — przy
 * grze wyzwalającej dźwięk co odbicie piłki byłoby to nie do zniesienia.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY && HYDRA_PLAT_HOST

#include "hydra/arduboy/Audio.hpp"

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("arduboy.snd")

#if defined(HYDRA_WITH_SDL)

#include <SDL.h>

#include <atomic>

namespace hydra {
namespace arduboy {

namespace {

constexpr int kSampleRate = 44100;
/** 512 próbek to ~11,6 ms opóźnienia — poniżej progu, na którym gracz je czuje. */
constexpr int kBufferFrames = 512;

/** Amplituda nuty zwykłej i „głośnej" z `TONE_HIGH_VOLUME`. */
constexpr i32 kAmplitudeNormal = 6000;
constexpr i32 kAmplitudeLoud   = 14000;

/** Ile próbek zajmuje dojście amplitudy do wartości docelowej (~3 ms). */
constexpr i32 kRampSamples = 128;

struct State {
    std::atomic<u32> frequency{0};
    std::atomic<i32> target{0};

    // Poniższe dotyka wyłącznie wywołanie zwrotne — bez atomów.
    u32 phase     = 0;   ///< Q16: część ułamkowa okresu.
    i32 amplitude = 0;
};

State gState;
SDL_AudioDeviceID gDevice = 0;

/**
 * Generuje próbki.
 *
 * Faza jest w Q16, więc przyrost na próbkę to `f * 65536 / sampleRate`.
 * Znak przebiegu bierze się z najstarszego bitu części całkowitej — to jedno
 * porównanie na próbkę, bez dzielenia i bez liczb zmiennoprzecinkowych.
 */
void feed(void*, Uint8* stream, int lengthBytes) {
    auto* out = reinterpret_cast<i16*>(stream);
    const int samples = lengthBytes / static_cast<int>(sizeof(i16));

    const u32 frequency = gState.frequency.load(std::memory_order_relaxed);
    const i32 target    = gState.target.load(std::memory_order_relaxed);

    const u32 step = frequency > 0
                         ? static_cast<u32>((static_cast<u64>(frequency) << 16) / kSampleRate)
                         : 0;

    for (int i = 0; i < samples; ++i) {
        // Amplituda dochodzi do celu stopniowo — patrz uwaga o trzaskach.
        const i32 stepAmp = (target > gState.amplitude ? 1 : -1) *
                            (kAmplitudeLoud / kRampSamples);
        if (gState.amplitude != target) {
            gState.amplitude += stepAmp;
            if ((stepAmp > 0 && gState.amplitude > target) ||
                (stepAmp < 0 && gState.amplitude < target)) {
                gState.amplitude = target;
            }
        }

        if (step == 0) {
            // Cisza, ale z zachowaną amplitudą opadającą — inaczej wyciszenie
            // ucięłoby przebieg w połowie okresu i znów byłby trzask.
            out[i] = static_cast<i16>((gState.phase & 0x8000u) ? gState.amplitude
                                                               : -gState.amplitude);
            if (gState.amplitude == 0) out[i] = 0;
            continue;
        }

        gState.phase += step;
        out[i] = static_cast<i16>((gState.phase & 0x8000u) ? gState.amplitude
                                                           : -gState.amplitude);
    }
}

/** Ujście podłączane pod sekwencery. Woła je wątek gry. */
void onTone(u16 frequencyHz, bool loud) {
    gState.frequency.store(frequencyHz, std::memory_order_relaxed);
    gState.target.store(frequencyHz == 0 ? 0 : (loud ? kAmplitudeLoud : kAmplitudeNormal),
                        std::memory_order_relaxed);
}

}  // namespace

Status startAudio() {
    if (gDevice != 0) return ok();

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        HYDRA_LOGW("brak podsystemu dzwieku SDL: %s", SDL_GetError());
        return fail(Err::NotSupported);
    }

    SDL_AudioSpec want{};
    want.freq     = kSampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = kBufferFrames;
    want.callback = feed;

    SDL_AudioSpec have{};
    // Bez zgody na zmiany: gdyby karta narzuciła inną częstotliwość próbkowania,
    // wszystkie nuty byłyby przestrojone, a gra brzmiałaby o tercję za wysoko.
    gDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (gDevice == 0) {
        HYDRA_LOGW("nie udalo sie otworzyc karty dzwiekowej: %s", SDL_GetError());
        return fail(Err::NotSupported);
    }

    Tones::setDefaultSink(onTone);
    SDL_PauseAudioDevice(gDevice, 0);

    HYDRA_LOGI("dzwiek: %d Hz, bufor %d probek", have.freq, have.samples);
    return ok();
}

void stopAudio() {
    if (gDevice == 0) return;

    // Wyciszenie przed zamknięciem: urządzenie zamknięte w trakcie nuty
    // zostawia w buforze karty ogon, który słychać po wyjściu z programu.
    onTone(0, false);
    SDL_Delay(20);

    SDL_CloseAudioDevice(gDevice);
    gDevice = 0;
    Tones::setDefaultSink({});
}

bool audioRunning() { return gDevice != 0; }

}  // namespace arduboy
}  // namespace hydra

#else  // brak SDL

namespace hydra {
namespace arduboy {

Status startAudio() {
    // Budowa bez SDL. Gra zadziała i będzie chodzić w tym samym tempie —
    // sekwencer liczy czas niezależnie od tego, czy coś słychać.
    HYDRA_LOGW("zbudowano bez SDL — gra bedzie niema");
    return fail(Err::NotSupported);
}

void stopAudio() {}
bool audioRunning() { return false; }

}  // namespace arduboy
}  // namespace hydra

#endif  // HYDRA_WITH_SDL

#endif  // HYDRA_ENABLE_ARDUBOY && HYDRA_PLAT_HOST
