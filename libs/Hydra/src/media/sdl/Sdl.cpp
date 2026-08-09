/**
 * Hydra — elementy potoku oparte o SDL.
 *
 * Drugi (po src/gfx/sdl/) i ostatni katalog w bibliotece, który widzi SDL.
 * Reguła w tools/check_includes.sh pilnuje, żeby lista się nie wydłużała:
 * nagłówek Sdl.hpp nie zawiera ani jednego typu SDL-a, więc aplikacja i reszta
 * modułu media go nie widzą.
 *
 * Wariant bez SDL jest na końcu pliku i nie jest atrapą testową — to normalna
 * ścieżka dla CI, kontenera i sesji ssh. `onStart()` zwraca wtedy
 * `Err::NotSupported`, a aplikacja decyduje, czy to błąd, czy powód, żeby
 * ruszyć bez dźwięku.
 */

#include "hydra/media/elements/Sdl.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/core/Log.hpp"

#include <string.h>

HYDRA_LOG_MODULE("media.sdl")

namespace hydra {
namespace media {

// ---------------------------------------------------------------------------
// Arytmetyka opóźnienia — niezależna od SDL, więc i testowalna bez niego
// ---------------------------------------------------------------------------

u32 bytesForMillis(const MediaFormat& format, u16 milliseconds) {
    if (format.kind != MediaKind::Audio || format.sampleRate == 0) return 0;
    return static_cast<u32>(static_cast<u64>(format.sampleRate) * milliseconds / 1000u) *
           format.unitBytes();
}

/**
 * Czy element audio poradzi sobie z tym formatem próbki.
 *
 * **Wspólne dla obu wariantów budowy.** Wcześniej ta decyzja siedziała tylko
 * w gałęzi z SDL, a wariant bez niego przyjmował wszystko — czyli `prepare()`
 * dawał inny wynik w zależności od tego, czy maszyna ma libsdl2-dev. Test
 * wyłapał to jako U8 przechodzące negocjację; objawem na maszynie z SDL byłby
 * potok, który nagle odmawia startu po instalacji biblioteki.
 *
 * U8 jest bez znaku i wymagałby przesunięcia punktu zerowego, S24 nie ma
 * odpowiednika w SDL. Przeliczanie próbek to zadanie filtru, nie ujścia.
 */
bool supportedSampleFormat(SampleFormat format) {
    return format == SampleFormat::S16 || format == SampleFormat::S32 ||
           format == SampleFormat::F32;
}

u8 queueBudget(u32 queued, u32 highBytes, u32 blockBytes) {
    if (blockBytes == 0) return 0;
    if (queued >= highBytes) return 0;

    const u32 room = highBytes - queued;
    const u32 blocks = room / blockBytes;
    // Górne ograniczenie na jeden krok. Bez niego pierwsze wywołanie po starcie
    // wepchnęłoby cały zapas naraz, zabierając czas domeny w chwili, w której
    // reszta potoku dopiero się rozkręca.
    constexpr u32 kMaxPerStep = 8;
    return static_cast<u8>(blocks < kMaxPerStep ? blocks : kMaxPerStep);
}

// ---------------------------------------------------------------------------
// Cykl życia niezależny od SDL
//
// Negocjacja i przygotowanie muszą dawać ten sam wynik niezależnie od tego,
// czy biblioteka jest w buildzie: inaczej `prepare()` całego potoku zależałby
// od tego, co ktoś ma zainstalowane na swojej maszynie.
// ---------------------------------------------------------------------------

Result<MediaFormat> SdlAudioSink::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    return in;
}

Status SdlAudioSink::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    format_ = input(0).format();

    if (format_.kind != MediaKind::Audio) return fail(Err::NotSupported);
    if (!supportedSampleFormat(format_.sampleFormat)) {
        HYDRA_LOGE("sdl-out: format %s nieobsługiwany — wstaw filtr konwersji",
                   toString(format_.sampleFormat));
        return fail(Err::NotSupported);
    }
    highBytes_ = bytesForMillis(format_, cfg_.targetLatencyMs);
    return ok();
}

Status SdlAudioSource::configure(const Config& cfg) {
    if (!cfg.format.valid() || cfg.framesPerBlock == 0) return fail(Err::BadArgument);
    if (!supportedSampleFormat(cfg.format.sampleFormat)) return fail(Err::NotSupported);
    cfg_ = cfg;
    return ok();
}

Result<MediaFormat> SdlAudioSource::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    HYDRA_UNUSED(in);
    if (!cfg_.format.valid()) return unexpected(Err::NotInitialized);
    return cfg_.format;
}

MemReq SdlAudioSource::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(cfg_.framesPerBlock) * cfg_.format.unitBytes();
    req.count = 3;
    return req;
}

Status SdlAudioSource::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    return pool_ != nullptr ? ok() : fail(Err::OutOfMemory);
}

}  // namespace media
}  // namespace hydra

#if defined(HYDRA_WITH_SDL)

#include <SDL.h>

namespace hydra {
namespace media {
namespace {

/** Format próbki Hydry → format SDL. Zero = nieobsługiwany. */
SDL_AudioFormat sdlFormat(SampleFormat format) {
    switch (format) {
        case SampleFormat::S16: return AUDIO_S16SYS;
        case SampleFormat::S32: return AUDIO_S32SYS;
        case SampleFormat::F32: return AUDIO_F32SYS;
        // U8 i S24 wypadają: pierwszy jest bez znaku i wymagałby przesunięcia
        // punktu zerowego, drugi nie ma odpowiednika w SDL. Przeliczanie
        // próbek należy do filtru, nie do ujścia.
        default: return 0;
    }
}

/**
 * Podnosi podsystem dźwięku.
 *
 * Osobno od wideo, bo maszyna bez karty dźwiękowej ma mieć działające okno.
 * `SDL_INIT_EVERYTHING` wywracałoby całą inicjalizację na braku dźwięku —
 * w programie, który chce tylko coś narysować.
 */
Status ensureAudio() {
    if (SDL_WasInit(SDL_INIT_AUDIO) != 0) return ok();
    SDL_SetMainReady();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        HYDRA_LOGE("SDL_InitSubSystem(AUDIO): %s", SDL_GetError());
        return fail(Err::IoError);
    }
    return ok();
}

/** Otwiera urządzenie w formacie **dokładnie** takim, jaki podał potok. */
Result<u32> openDevice(const MediaFormat& format, const char* name, bool capture) {
    const SDL_AudioFormat sample = sdlFormat(format.sampleFormat);
    if (sample == 0) return unexpected(Err::NotSupported);

    SDL_AudioSpec want = {};
    want.freq     = static_cast<int>(format.sampleRate);
    want.format   = sample;
    want.channels = format.channels;
    // Rozmiar porcji sterownika. 1024 ramki to ~64 ms przy 16 kHz — na tyle
    // dużo, żeby system operacyjny ogólnego przeznaczenia zdążył nas obudzić.
    want.samples  = 1024;
    want.callback = nullptr;   // tryb kolejkowy; patrz nagłówek klasy

    SDL_AudioSpec have = {};
    // Zero jako flagi zmian: SDL ma podać dokładnie to, o co prosimy, albo
    // odmówić. Konwersja w locie po cichu zmieniłaby częstotliwość i cały
    // rachunek opóźnienia w potoku przestałby się zgadzać.
    const SDL_AudioDeviceID id =
        SDL_OpenAudioDevice(name, capture ? 1 : 0, &want, &have, 0);
    if (id == 0) {
        HYDRA_LOGE("SDL_OpenAudioDevice: %s", SDL_GetError());
        return unexpected(Err::IoError);
    }
    return static_cast<u32>(id);
}

}  // namespace

// ---------------------------------------------------------------------------
// SdlAudioSink
// ---------------------------------------------------------------------------

SdlAudioSink::~SdlAudioSink() { onStop(); }

Status SdlAudioSink::onStart() {
    HYDRA_CHECK(ensureAudio());
    auto id = openDevice(format_, cfg_.device, false);
    if (!id) return fail(id.error());

    device_ = *id;
    primed_ = false;
    // Odtwarzanie ruszy dopiero, gdy uzbiera się zapas — patrz `process()`.
    SDL_PauseAudioDevice(static_cast<SDL_AudioDeviceID>(device_), 1);

    HYDRA_LOGI("wyjście audio: %lu Hz / %u kan., zapas %u ms (%lu B)",
               static_cast<unsigned long>(format_.sampleRate),
               static_cast<unsigned>(format_.channels),
               static_cast<unsigned>(cfg_.targetLatencyMs),
               static_cast<unsigned long>(highBytes_));
    return ok();
}

void SdlAudioSink::onStop() {
    if (device_ == 0) return;
    SDL_CloseAudioDevice(static_cast<SDL_AudioDeviceID>(device_));
    device_ = 0;
}

void SdlAudioSink::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (device_ == 0) return;

    const SDL_AudioDeviceID id = static_cast<SDL_AudioDeviceID>(device_);
    const u32 pending = SDL_GetQueuedAudioSize(id);

    // Kolejka pusta przy już uruchomionym odtwarzaniu to przerwa, którą
    // słychać. Zdarzenie, nie log: trwa kilkadziesiąt milisekund i w logu ginie.
    if (primed_ && pending == 0) {
        ++underruns_;
        pipeline_->raise(MediaFault::Underrun, *this, 0);
    }

    Block block;
    if (!input(0).peek(block)) return;

    const u8 allowed = queueBudget(pending, highBytes_, block.length);
    if (allowed == 0) {
        // Kolejka wystarczająco pełna. Blok zostaje w padzie — wstrzymanie
        // produkcji jest tańsze niż odrzucenie i nie słychać go.
        ++throttled_;
        return;
    }

    for (u8 i = 0; i < allowed && take(0, block); ++i) {
        if (block.length > 0 &&
            SDL_QueueAudio(id, block.data, block.length) != 0) {
            HYDRA_LOGW("SDL_QueueAudio: %s", SDL_GetError());
        } else {
            queued_ += block.length;
        }

        const bool eos = block.has(kBlockEos);
        if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);
        if (eos) {
            EventBus::publish(MediaEndOfStream{index(), queued_});
            return;
        }
    }

    // Start dopiero po uzbieraniu połowy zapasu. Ruszenie od pierwszego bloku
    // daje przerwę w drugiej sekundzie, bo karta wybiera szybciej, niż potok
    // zdąży się rozkręcić.
    if (!primed_ && SDL_GetQueuedAudioSize(id) >= highBytes_ / 2) {
        SDL_PauseAudioDevice(id, 0);
        primed_ = true;
    }
}

// ---------------------------------------------------------------------------
// SdlAudioSource
// ---------------------------------------------------------------------------

SdlAudioSource::~SdlAudioSource() { onStop(); }

Status SdlAudioSource::onStart() {
    HYDRA_CHECK(ensureAudio());
    auto id = openDevice(cfg_.format, cfg_.device, true);
    if (!id) return fail(id.error());

    device_ = *id;
    SDL_PauseAudioDevice(static_cast<SDL_AudioDeviceID>(device_), 0);
    return ok();
}

void SdlAudioSource::onStop() {
    if (device_ == 0) return;
    SDL_CloseAudioDevice(static_cast<SDL_AudioDeviceID>(device_));
    device_ = 0;
}

void SdlAudioSource::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (device_ == 0) return;

    const SDL_AudioDeviceID id = static_cast<SDL_AudioDeviceID>(device_);

    // Tylko pełne bloki. Blok krótszy niż zadeklarowany rozsypałby rachunek
    // znaczników czasu, a mikrofon i tak dosypie resztę w następnym kroku.
    while (SDL_GetQueuedAudioSize(id) >= cfg_.framesPerBlock * cfg_.format.unitBytes()) {
        Block block = pool_->acquire();
        if (!block.valid()) {
            pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
            return;
        }

        const u32 want = cfg_.framesPerBlock * cfg_.format.unitBytes();
        const u32 got = SDL_DequeueAudio(id, block.data, want);
        if (got == 0) { pool_->release(block); return; }

        block.length = got;
        block.pts = frames_ * 1000000ull / cfg_.format.sampleRate;
        frames_ += got / cfg_.format.unitBytes();

        Block evicted;
        if (!emit(0, block, evicted)) {
            pool_->release(block);
            pipeline_->raise(MediaFault::Overrun, *this, 0);
            return;
        }
        if (evicted.valid()) pool_->release(evicted);
    }
}

}  // namespace media
}  // namespace hydra

#else  // !HYDRA_WITH_SDL

namespace hydra {
namespace media {

// Wyłącznie to, co naprawdę wymaga biblioteki. Negocjacja i przygotowanie
// są wspólne i stoją wyżej — dzięki temu nie ma jak się rozjechać.

SdlAudioSink::~SdlAudioSink() = default;
Status SdlAudioSink::onStart() { return fail(Err::NotSupported); }
void   SdlAudioSink::onStop() {}
void   SdlAudioSink::process(u64) {}

SdlAudioSource::~SdlAudioSource() = default;
Status SdlAudioSource::onStart() { return fail(Err::NotSupported); }
void   SdlAudioSource::onStop() {}
void   SdlAudioSource::process(u64) {}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_WITH_SDL

// ---------------------------------------------------------------------------
// SdlVideoSink — bez bezpośredniego użycia SDL, całość przez gfx::SdlDisplay
// ---------------------------------------------------------------------------

namespace hydra {
namespace media {
namespace {

/** Format klatki potoku → format powierzchni gfx. */
gfx::PixelFormat surfaceFormat(FrameFormat format) {
    switch (format) {
        case FrameFormat::Gray8:  return gfx::PixelFormat::Mono1;   // patrz uwaga niżej
        case FrameFormat::Rgb565: return gfx::PixelFormat::Rgb565;
        case FrameFormat::Rgb888: return gfx::PixelFormat::Rgb888;
        default:                  return gfx::PixelFormat::Rgb565;
    }
}

}  // namespace

Result<MediaFormat> SdlVideoSink::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    return in;
}

Status SdlVideoSink::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    format_ = input(0).format();

    if (format_.kind != MediaKind::Video) {
        HYDRA_LOGE("sdl-video: na wejściu nie ma obrazu");
        return fail(Err::NotSupported);
    }
    if (format_.frameFormat == FrameFormat::Jpeg ||
        format_.frameFormat == FrameFormat::Yuv422) {
        // Dekodowanie i konwersja przestrzeni barw to etap 5. Odmowa teraz
        // jest lepsza niż okno z szumem, które wygląda na usterkę kamery.
        HYDRA_LOGE("sdl-video: %s wymaga dekodera — wstaw go przed podglądem",
                   toString(format_.frameFormat));
        return fail(Err::NotSupported);
    }
    if (format_.frameFormat == FrameFormat::Gray8) {
        HYDRA_LOGE("sdl-video: gray8 nie ma jeszcze konwersji na powierzchnię");
        return fail(Err::NotSupported);
    }

    const u32 needed = format_.frameBytes();
    if (buffer_.size() < needed) {
        HYDRA_LOGE("sdl-video: bufor okna %lu B, klatka potrzebuje %lu B",
                   static_cast<unsigned long>(buffer_.size()),
                   static_cast<unsigned long>(needed));
        return fail(Err::OutOfRange);
    }
    return ok();
}

Status SdlVideoSink::onStart() {
    gfx::SdlDisplay::Cfg cfg;
    cfg.title  = cfg_.title;
    cfg.width  = static_cast<i16>(format_.width);
    cfg.height = static_cast<i16>(format_.height);
    cfg.scale  = cfg_.scale;
    cfg.format = surfaceFormat(format_.frameFormat);
    cfg.vsync  = cfg_.vsync;

    HYDRA_CHECK(display_.begin(buffer_, cfg));
    started_ = true;
    return ok();
}

void SdlVideoSink::onStop() {
    display_.end();
    started_ = false;
}

bool SdlVideoSink::open() {
    if (!started_) return false;
    return display_.pump();
}

void SdlVideoSink::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (!started_) return;

    Block block;
    // Tylko ostatnia klatka ma sens: monitor odświeża się 60 razy na sekundę,
    // a zaległe klatki i tak nikt nie zobaczy. Starsze zwalniamy bez rysowania.
    Block newest{};
    while (take(0, block)) {
        if (newest.valid()) {
            if (BlockPool* p = pipeline_->pool(newest.pool); p != nullptr) p->release(newest);
        }
        newest = block;
    }
    if (!newest.valid()) return;

    const u32 needed = format_.frameBytes();
    if (newest.length >= needed) {
        // Kopia, nie podmiana wskaźnika — powód w nagłówku klasy.
        memcpy(buffer_.data(), newest.data, needed);
        display_.framebuffer().markDirty(display_.surface().bounds());
        (void)display_.surface().flush();
        ++frames_;
    }

    if (BlockPool* p = pipeline_->pool(newest.pool); p != nullptr) p->release(newest);
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
