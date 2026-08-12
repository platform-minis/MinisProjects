/**
 * Hydra — analizator widma. Patrz nagłówek po różnicę wobec MFCC.
 */

#include "hydra/media/elements/Spectrum.hpp"

#if HYDRA_ENABLE_MEDIA

#include <math.h>
#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/media/Pipeline.hpp"
#include "hydra/util/Fft.hpp"

HYDRA_LOG_MODULE("spectrum")

namespace hydra {
namespace media {
namespace {

/**
 * Podłoga mocy odpowiadająca −120 dB.
 *
 * `log(0)` to minus nieskończoność, a cisza jest najczęstszym stanem wejścia.
 * Jedna taka wartość zatruwa wygładzanie na zawsze: średnia wykładnicza
 * z nieskończonością zostaje nieskończonością.
 */
constexpr float kPowerFloor = 1e-12f;

/** Moc → decybele. */
inline float toDb(float power) {
    return 10.0f * log10f(power > kPowerFloor ? power : kPowerFloor);
}

}  // namespace

Status SpectrumAnalyzer::configure(const Config& cfg) {
    if (!util::isValidFftSize(cfg.fftSize) || cfg.fftSize > HYDRA_SPECTRUM_MAX_FFT) {
        HYDRA_LOGE("fftSize %u: musi być potęgą dwójki i nie więcej niż %u",
                   static_cast<unsigned>(cfg.fftSize),
                   static_cast<unsigned>(HYDRA_SPECTRUM_MAX_FFT));
        return fail(Err::BadArgument);
    }
    if (cfg.hopSamples > cfg.fftSize) return fail(Err::BadArgument);

    const u16 available = util::spectrumBins(cfg.fftSize);
    if (cfg.bins > available) {
        // Zwężenie ma sens, poszerzenie nie: prążków nie da się wymyślić.
        HYDRA_LOGE("żądano %u prążków, a przekształcenie daje %u",
                   static_cast<unsigned>(cfg.bins), static_cast<unsigned>(available));
        return fail(Err::BadArgument);
    }

    cfg_ = cfg;
    if (cfg_.hopSamples == 0) cfg_.hopSamples = cfg_.fftSize;
    binCount_ = cfg_.bins > 0 ? cfg_.bins : available;
    primed_ = false;
    return ok();
}

Result<MediaFormat> SpectrumAnalyzer::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);

    if (in.kind != MediaKind::Audio) return unexpected(Err::NotSupported);
    if (in.sampleFormat != SampleFormat::S16) return unexpected(Err::NotSupported);
    if (in.channels != 1) {
        HYDRA_LOGE("analizator liczy z jednego kanału — wstaw mikser przed nim");
        return unexpected(Err::NotSupported);
    }

    sampleRate_ = in.sampleRate;

    const u32 windowsPerSecondMilli = cfg_.hopSamples > 0
        ? (in.sampleRate * 1000u) / cfg_.hopSamples
        : 0;
    return MediaFormat::features(binCount_, windowsPerSecondMilli);
}

MemReq SpectrumAnalyzer::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(binCount_) * sizeof(float);
    req.count = 3;
    return req;
}

Status SpectrumAnalyzer::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;

    if (cfg_.fftSize == 0) {
        HYDRA_LOGE("brak konfiguracji — wołaj configure() przed prepare()");
        return fail(Err::NotInitialized);
    }
    if (sampleRate_ == 0) {
        HYDRA_LOGE("nieznana częstotliwość próbkowania — bez niej oś częstotliwości nie ma skali");
        return fail(Err::NotInitialized);
    }

    pool_ = pipeline.poolFor(*this, 0);
    if (pool_ == nullptr) return fail(Err::OutOfMemory);

    filled_ = 0;
    primed_ = false;
    HYDRA_LOGI("okno %u, przesuw %u, prążków %u, %u Hz",
               static_cast<unsigned>(cfg_.fftSize), static_cast<unsigned>(cfg_.hopSamples),
               static_cast<unsigned>(binCount_), static_cast<unsigned>(sampleRate_));
    return ok();
}

float SpectrumAnalyzer::binHz(u16 index) const {
    if (binCount_ == 0 || sampleRate_ == 0) return 0.0f;

    /*
     * Prążek wyjściowy odpowiada grupie prążków przekształcenia, gdy widmo
     * jest zwężane. Zwracamy środek grupy, a nie jej początek: przy 129
     * prążkach w 64 słupkach różnica to pół szerokości słupka, czyli tyle,
     * ile trzeba, żeby opis osi rozjechał się o jeden.
     */
    const u16 available = util::spectrumBins(cfg_.fftSize);
    const float perOutput = static_cast<float>(available) / static_cast<float>(binCount_);
    const float center = (static_cast<float>(index) + 0.5f) * perOutput;
    return center * static_cast<float>(sampleRate_) / static_cast<float>(cfg_.fftSize);
}

u32 SpectrumAnalyzer::fill(const i16* samples, u32 count) {
    const u32 room = cfg_.fftSize - filled_;
    const u32 take = count < room ? count : room;
    for (u32 i = 0; i < take; ++i) {
        window_[filled_ + i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    filled_ = static_cast<u16>(filled_ + take);
    return take;
}

void SpectrumAnalyzer::slide() {
    if (cfg_.hopSamples >= filled_) {
        filled_ = 0;
        return;
    }
    const u16 keep = static_cast<u16>(filled_ - cfg_.hopSamples);
    memmove(window_, window_ + cfg_.hopSamples, keep * sizeof(float));
    filled_ = keep;
}

void SpectrumAnalyzer::computeWindow() {
    float work[HYDRA_SPECTRUM_MAX_FFT];
    memcpy(work, window_, cfg_.fftSize * sizeof(float));

    util::applyHann(work, cfg_.fftSize);
    if (!util::powerSpectrum(work, scratch_, cfg_.fftSize, power_)) return;

    const u16 available = util::spectrumBins(cfg_.fftSize);
    const float perOutput = static_cast<float>(available) / static_cast<float>(binCount_);
    const float alpha = static_cast<float>(cfg_.smoothing) / 256.0f;

    for (u16 i = 0; i < binCount_; ++i) {
        /*
         * Zwężanie przez **maksimum** w grupie, nie przez średnią.
         *
         * Analizator ma pokazać, że coś w tym paśmie jest. Uśrednienie
         * rozmywa wąski, silny prążek w grupie sąsiadów o tle — czyli gubi
         * dokładnie to, czego się szuka przy diagnostyce wibracji.
         */
        const u16 from = static_cast<u16>(static_cast<float>(i) * perOutput);
        u16 to = static_cast<u16>(static_cast<float>(i + 1) * perOutput);
        if (to <= from) to = static_cast<u16>(from + 1);
        if (to > available) to = available;

        float peak = 0.0f;
        for (u16 b = from; b < to; ++b) {
            if (power_[b] > peak) peak = power_[b];
        }

        const float db = toDb(peak);
        const float clipped = db < cfg_.floorDb ? cfg_.floorDb : db;

        // Pierwsze okno wchodzi bez wygładzania — inaczej obraz dochodziłby
        // do prawdy przez kilkanaście okien i pierwszy pomiar byłby fałszywy.
        output_[i] = (primed_ && alpha > 0.0f)
                         ? (output_[i] * alpha + clipped * (1.0f - alpha))
                         : clipped;
    }

    primed_ = true;
    ++windows_;
}

void SpectrumAnalyzer::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (pipeline_ == nullptr || pool_ == nullptr) return;

    Block in;
    while (take(0, in)) {
        const i16* samples = reinterpret_cast<const i16*>(in.data);
        const u32 total = in.length / sizeof(i16);

        u32 offset = 0;
        while (offset < total) {
            offset += fill(samples + offset, total - offset);

            if (filled_ >= cfg_.fftSize) {
                computeWindow();

                Block out = pool_->acquire();
                if (out.valid()) {
                    const u32 bytes = static_cast<u32>(binCount_) * sizeof(float);
                    memcpy(out.data, output_, bytes);
                    out.length = bytes;
                    out.pts = in.pts;

                    Block evicted;
                    if (!emit(0, out, evicted)) {
                        pool_->release(out);
                        pipeline_->raise(MediaFault::Overrun, *this, 0);
                    } else if (evicted.valid()) {
                        if (BlockPool* p = pipeline_->pool(evicted.pool); p != nullptr) {
                            p->release(evicted);
                        }
                    }
                } else {
                    // Widmo jest obrazem chwili: zgubione okno znaczy jedną
                    // klatkę mniej, a zatrzymanie potoku — brak obrazu w ogóle.
                    pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
                }

                slide();
            }
        }

        if (BlockPool* p = pipeline_->pool(in.pool); p != nullptr) p->release(in);
    }
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
