/**
 * Hydra — MFCC jako element potoku. Patrz nagłówek po drogę sygnału.
 */

#include "hydra/media/elements/Mfcc.hpp"

#if HYDRA_ENABLE_MEDIA

#include <math.h>
#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/core/RealMath.hpp"
#include "hydra/media/Pipeline.hpp"
#include "hydra/util/Fft.hpp"

HYDRA_LOG_MODULE("mfcc")

namespace hydra {
namespace media {
namespace {

/**
 * Herc → mel.
 *
 * Wzór O’Shaughnessy’ego, ten sam, którego używa większość implementacji
 * rozpoznawania mowy. Skala oddaje to, że ucho rozróżnia 200 Hz od 300 Hz
 * lepiej niż 4000 Hz od 4100 Hz.
 */
float hzToMel(float hz) {
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

float melToHz(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

/**
 * Podłoga logarytmu.
 *
 * Cisza w paśmie daje energię zero, a `log(0)` to minus nieskończoność —
 * jedna taka wartość zatruwa całe DCT i model dostaje wektor złożony z NaN.
 * Podłoga jest niżej niż cokolwiek, co niesie informację, i wyżej niż zero.
 */
constexpr float kEnergyFloor = 1e-10f;

}  // namespace

Status MfccExtractor::configure(const Config& cfg) {
    if (!util::isValidFftSize(cfg.fftSize) || cfg.fftSize > HYDRA_MFCC_MAX_FFT) {
        HYDRA_LOGE("fftSize %u: musi być potęgą dwójki i nie więcej niż %u",
                   static_cast<unsigned>(cfg.fftSize), static_cast<unsigned>(HYDRA_MFCC_MAX_FFT));
        return fail(Err::BadArgument);
    }
    if (cfg.filterCount == 0 || cfg.filterCount > HYDRA_MFCC_MAX_FILTERS) return fail(Err::BadArgument);
    if (cfg.coeffCount == 0 || cfg.coeffCount > HYDRA_MFCC_MAX_COEFFS) return fail(Err::BadArgument);
    // Więcej współczynników niż filtrów nie ma z czego policzyć — DCT nie
    // tworzy informacji, tylko ją przestawia.
    if (cfg.coeffCount > cfg.filterCount) return fail(Err::BadArgument);
    if (cfg.hopSamples > cfg.fftSize) return fail(Err::BadArgument);

    cfg_ = cfg;
    if (cfg_.hopSamples == 0) cfg_.hopSamples = cfg_.fftSize;
    return ok();
}

Result<MediaFormat> MfccExtractor::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);

    if (in.kind != MediaKind::Audio) return unexpected(Err::NotSupported);
    if (in.sampleFormat != SampleFormat::S16) return unexpected(Err::NotSupported);
    if (in.channels != 1) {
        // Mieszanie kanałów jest osobną decyzją i osobnym elementem: średnia
        // z dwóch mikrofonów to co innego niż wybór jednego, a MFCC nie ma
        // podstaw, żeby rozstrzygać za użytkownika.
        HYDRA_LOGE("MFCC liczy z jednego kanału — wstaw mikser przed nim");
        return unexpected(Err::NotSupported);
    }

    sampleRate_ = in.sampleRate;

    // Stawka okien: ile razy na sekundę wychodzi wektor cech.
    const u32 windowsPerSecondMilli = cfg_.hopSamples > 0
        ? (in.sampleRate * 1000u) / cfg_.hopSamples
        : 0;
    return MediaFormat::features(cfg_.coeffCount, windowsPerSecondMilli);
}

MemReq MfccExtractor::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(cfg_.coeffCount) * sizeof(float);
    // Trzy bloki: jeden liczony, jeden w kolejce, jeden u odbiorcy. Dwa
    // wystarczają, dopóki odbiorca nadąża — a gdy nie nadąża, potok woli
    // zgubić okno niż stanąć.
    req.count = 3;
    return req;
}

Status MfccExtractor::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;

    if (cfg_.fftSize == 0) {
        HYDRA_LOGE("brak konfiguracji — wołaj configure() przed prepare()");
        return fail(Err::NotInitialized);
    }
    if (sampleRate_ == 0) {
        HYDRA_LOGE("nieznana częstotliwość próbkowania — bank filtrów zależy od niej");
        return fail(Err::NotInitialized);
    }

    pool_ = pipeline.poolFor(*this, 0);
    if (pool_ == nullptr) return fail(Err::OutOfMemory);

    buildFilterBank(sampleRate_);
    filled_ = 0;

    HYDRA_LOGI("okno %u, przesuw %u, filtrów %u, współczynników %u, %u Hz",
               static_cast<unsigned>(cfg_.fftSize), static_cast<unsigned>(cfg_.hopSamples),
               static_cast<unsigned>(cfg_.filterCount), static_cast<unsigned>(cfg_.coeffCount),
               static_cast<unsigned>(sampleRate_));
    return ok();
}

void MfccExtractor::buildFilterBank(u32 sampleRate) {
    const u16 bins = util::spectrumBins(cfg_.fftSize);
    const float nyquist = static_cast<float>(sampleRate) / 2.0f;
    const float highHz = cfg_.highHz > 0 ? static_cast<float>(cfg_.highHz) : nyquist;

    const float melLow = hzToMel(static_cast<float>(cfg_.lowHz));
    const float melHigh = hzToMel(highHz);
    const float melStep = (melHigh - melLow) / static_cast<float>(cfg_.filterCount + 1);

    /*
     * Filtry zachodzą na siebie: szczyt jednego jest krańcem sąsiadów.
     * Rozłączne dawałyby dziury w paśmie — dźwięk trafiający między filtry
     * przestawałby istnieć dla modelu.
     */
    for (u8 i = 0; i < cfg_.filterCount; ++i) {
        const float mStart = melLow + melStep * static_cast<float>(i);
        const float mPeak  = melLow + melStep * static_cast<float>(i + 1);
        const float mEnd   = melLow + melStep * static_cast<float>(i + 2);

        const auto toBin = [&](float mel) -> u16 {
            const float hz = melToHz(mel);
            const float bin = hz * static_cast<float>(cfg_.fftSize) / static_cast<float>(sampleRate);
            const i32 rounded = static_cast<i32>(bin + 0.5f);
            if (rounded < 0) return 0;
            return static_cast<u16>(rounded >= bins ? bins - 1 : rounded);
        };

        filterStart_[i] = toBin(mStart);
        filterPeak_[i]  = toBin(mPeak);
        filterEnd_[i]   = toBin(mEnd);
    }
}

u32 MfccExtractor::fill(const i16* samples, u32 count) {
    const u32 room = cfg_.fftSize - filled_;
    const u32 take = count < room ? count : room;

    // Przeliczenie na `float` z normalizacją do [-1, 1]: dalej wszystko jest
    // zmiennoprzecinkowe, a skala bezwzględna i tak znika w logarytmie.
    for (u32 i = 0; i < take; ++i) {
        window_[filled_ + i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    filled_ = static_cast<u16>(filled_ + take);
    return take;
}

void MfccExtractor::slide() {
    if (cfg_.hopSamples >= filled_) {
        filled_ = 0;
        return;
    }
    const u16 keep = static_cast<u16>(filled_ - cfg_.hopSamples);
    memmove(window_, window_ + cfg_.hopSamples, keep * sizeof(float));
    filled_ = keep;
}

void MfccExtractor::computeWindow() {
    // Kopia okna: FFT niszczy wejście, a przy zakładce reszta okna jest jeszcze
    // potrzebna do następnego przebiegu.
    float work[HYDRA_MFCC_MAX_FFT];
    memcpy(work, window_, cfg_.fftSize * sizeof(float));

    util::applyHann(work, cfg_.fftSize);
    if (!util::powerSpectrum(work, scratch_, cfg_.fftSize, power_)) return;

    // Bank filtrów: energia w paśmie jako suma ważona prążków.
    for (u8 f = 0; f < cfg_.filterCount; ++f) {
        const u16 start = filterStart_[f];
        const u16 peak  = filterPeak_[f];
        const u16 end   = filterEnd_[f];

        float sum = 0.0f;
        for (u16 b = start; b <= end && b < util::spectrumBins(cfg_.fftSize); ++b) {
            // Waga trójkątna liczona w miejscu — patrz komentarz przy polach.
            float weight = 0.0f;
            if (b <= peak && peak > start) {
                weight = static_cast<float>(b - start) / static_cast<float>(peak - start);
            } else if (b > peak && end > peak) {
                weight = static_cast<float>(end - b) / static_cast<float>(end - peak);
            } else if (b == peak) {
                weight = 1.0f;
            }
            sum += power_[b] * weight;
        }

        energies_[f] = logf(sum > kEnergyFloor ? sum : kEnergyFloor);
    }

    // DCT-II: dekorelacja. Bez niej sąsiednie filtry niosą prawie to samo,
    // a model musiałby uczyć się tej zależności zamiast treści.
    for (u8 k = 0; k < cfg_.coeffCount; ++k) {
        float sum = 0.0f;
        for (u8 f = 0; f < cfg_.filterCount; ++f) {
            sum += energies_[f] * cosReal(kPi * static_cast<float>(k) *
                                          (static_cast<float>(f) + 0.5f) /
                                          static_cast<float>(cfg_.filterCount));
        }
        coeffs_[k] = sum;
    }

    ++windows_;
}

void MfccExtractor::process(u64 nowUs) {
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
                    const u32 bytes = static_cast<u32>(cfg_.coeffCount) * sizeof(float);
                    memcpy(out.data, coeffs_, bytes);
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
                    // Brak bloku znaczy, że odbiorca nie nadąża. Okno przepada,
                    // ale potok idzie dalej — cechy są strumieniem, w którym
                    // pojedyncza dziura jest mniej szkodliwa niż zatrzymanie.
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
