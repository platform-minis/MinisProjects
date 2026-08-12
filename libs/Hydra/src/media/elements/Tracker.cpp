/**
 * Hydra — moduł Amigi jako źródło potoku. Patrz nagłówek po decyzje.
 */

#include "hydra/media/elements/Tracker.hpp"

#if HYDRA_ENABLE_MEDIA

#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/media/Pipeline.hpp"

#include "pocketmod.h"

HYDRA_LOG_MODULE("mod")

namespace hydra {
namespace media {
namespace {

static_assert(sizeof(pocketmod_context) <= HYDRA_MOD_CONTEXT_BYTES,
              "HYDRA_MOD_CONTEXT_BYTES za małe dla pocketmod_context — "
              "podnieś stałą w Tracker.hpp");

/** Ile ramek stereo mieści się w buforze pośrednim na stosie. */
constexpr u16 kRenderChunkFrames = 128;

/**
 * `float` w zakresie [-1, 1] → `i16` z przycięciem.
 *
 * Przycięcie, a nie zawinięcie: przepełnienie `i16` przy zawinięciu daje
 * trzask o pełnej amplitudzie, czyli najgłośniejszy możliwy objaw najcichszego
 * błędu. Moduły z Amigi bywają zmiksowane z zapasem, ale efekt głośności
 * i kilka kanałów naraz potrafią przekroczyć zakres.
 */
inline i16 toS16(float sample) {
    const float scaled = sample * 32767.0f;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= -32768.0f) return -32768;
    return static_cast<i16>(scaled);
}

}  // namespace

Status ModSource::load(CByteSpan module, const Config& cfg) {
    if (module.data() == nullptr || module.size() == 0) return fail(Err::BadArgument);
    if (cfg.sampleRate == 0 || cfg.framesPerBlock == 0) return fail(Err::BadArgument);

    auto* ctx = reinterpret_cast<pocketmod_context*>(context_);
    // pocketmod zwraca 0 przy pliku, którego nie rozpoznaje — nagłówek modułu
    // ma stałą sygnaturę, więc to jest sprawdzenie formatu, nie tylko rozmiaru.
    if (!pocketmod_init(ctx, module.data(), static_cast<int>(module.size()),
                        static_cast<int>(cfg.sampleRate))) {
        HYDRA_LOGE("to nie jest moduł Protrackera albo plik jest obcięty (%u B)",
                   static_cast<unsigned>(module.size()));
        ready_ = false;
        return fail(Err::BadArgument);
    }

    cfg_ = cfg;
    ready_ = true;
    frames_ = 0;
    finishing_ = false;
    finished_ = false;

    HYDRA_LOGI("moduł wczytany: %u B, %u Hz, blok %u ramek",
               static_cast<unsigned>(module.size()),
               static_cast<unsigned>(cfg.sampleRate),
               static_cast<unsigned>(cfg.framesPerBlock));
    return ok();
}

u32 ModSource::loops() const {
    if (!ready_) return 0;
    auto* ctx = const_cast<pocketmod_context*>(
        reinterpret_cast<const pocketmod_context*>(context_));
    return static_cast<u32>(pocketmod_loop_count(ctx));
}

Result<MediaFormat> ModSource::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    HYDRA_UNUSED(in);   // źródło nie ma wejścia — format wymyśla samo

    if (!ready_) return unexpected(Err::NotInitialized);
    // Stereo zawsze: odtwarzacz produkuje dwa kanały, a mieszanie do mono to
    // osobna decyzja i osobny element.
    return MediaFormat::audio(cfg_.sampleRate, SampleFormat::S16, 2);
}

MemReq ModSource::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(cfg_.framesPerBlock) * 2 * sizeof(i16);
    // Trzy bloki: jeden wypełniany, jeden w kolejce, jeden u odbiorcy.
    req.count = 3;
    return req;
}

Status ModSource::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    if (!ready_) {
        HYDRA_LOGE("brak modułu — wołaj load() przed prepare()");
        return fail(Err::NotInitialized);
    }

    pool_ = pipeline.poolFor(*this, 0);
    if (pool_ == nullptr) return fail(Err::OutOfMemory);
    return ok();
}

void ModSource::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (!ready_ || pool_ == nullptr || finished_) return;

    Block block = pool_->acquire();
    if (!block.valid()) {
        // Brak bloku znaczy, że odbiorca nie nadąża. Źródło czeka do następnego
        // przebiegu — muzyka w tle woli zaczekać niż zgubić fragment.
        pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
        return;
    }

    auto* ctx = reinterpret_cast<pocketmod_context*>(context_);
    i16* out = reinterpret_cast<i16*>(block.data);
    const u16 wanted = cfg_.framesPerBlock;
    u16 written = 0;

    /*
     * Render idzie porcjami przez bufor na stosie, bo pocketmod produkuje
     * `float`, a walutą potoku jest `i16`.
     *
     * Bufor pośredni w obiekcie kosztowałby pamięć przez cały czas życia
     * elementu; na stosie żyje tyle, ile trwa `process()`. Sto dwadzieścia
     * osiem ramek stereo to kilobajt — mieści się w stosie taska Hydry.
     */
    while (written < wanted) {
        float chunk[kRenderChunkFrames][2];
        const u16 take = (wanted - written) < kRenderChunkFrames
                             ? static_cast<u16>(wanted - written)
                             : kRenderChunkFrames;

        const int bytes = pocketmod_render(ctx, chunk, static_cast<int>(take * sizeof(chunk[0])));
        const u16 got = static_cast<u16>(bytes / static_cast<int>(sizeof(chunk[0])));
        if (got == 0) break;

        for (u16 i = 0; i < got; ++i) {
            out[(written + i) * 2 + 0] = toS16(chunk[i][0]);
            out[(written + i) * 2 + 1] = toS16(chunk[i][1]);
        }
        written = static_cast<u16>(written + got);

        // Koniec po zadanej liczbie przejść. Zero znaczy „graj bez końca" —
        // dla muzyki w tle to stan normalny, a nie brak decyzji.
        if (cfg_.maxLoops > 0 && pocketmod_loop_count(ctx) >= cfg_.maxLoops) {
            finishing_ = true;
            break;
        }
    }

    if (written == 0) {
        pool_->release(block);
        return;
    }

    block.length = static_cast<u32>(written) * 2 * sizeof(i16);
    // Znacznik czasu liczony z liczby ramek, a nie z zegara: strumień ma być
    // ciągły niezależnie od tego, kiedy task zdążył go wypełnić.
    block.pts = (frames_ * 1000000ULL) / cfg_.sampleRate;
    frames_ += written;

    if (finishing_) {
        block.set(kBlockEos);
        finished_ = true;
    }

    Block evicted;
    if (!emit(0, block, evicted)) {
        pool_->release(block);
        pipeline_->raise(MediaFault::Overrun, *this, 0);
        return;
    }
    if (evicted.valid()) {
        if (BlockPool* p = pipeline_->pool(evicted.pool); p != nullptr) p->release(evicted);
    }
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
