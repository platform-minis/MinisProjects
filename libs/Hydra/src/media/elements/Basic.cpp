/** Hydra — implementacja elementów programowych potoku. */

#include "hydra/media/elements/Basic.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/core/Log.hpp"

#include <string.h>

HYDRA_LOG_MODULE("media.el")

namespace hydra {
namespace media {
namespace {

/**
 * Ćwiartka sinusa w 64 punktach, skala 0…32767.
 *
 * Tablica zamiast `sin()`: RP2040 liczy funkcje przestępne programowo,
 * a wynik ma być identyczny na każdej platformie, żeby test mógł porównać
 * próbki co do bitu.
 */
const i16 kSineQuarter[64] = {
        0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
     6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
};

/** Sinus dla fazy 0…255 obiegu, wynik −32767…32767. */
i16 sine256(u8 phase) {
    const u8 quadrant = static_cast<u8>(phase >> 6);
    const u8 index    = static_cast<u8>(phase & 0x3F);
    switch (quadrant) {
        case 0:  return kSineQuarter[index];
        case 1:  return kSineQuarter[63 - index];
        case 2:  return static_cast<i16>(-kSineQuarter[index]);
        default: return static_cast<i16>(-kSineQuarter[63 - index]);
    }
}

/** Przycięcie do zakresu i16 — nigdy zawinięcie. */
i16 clampToS16(i32 value, u32& clipCounter) {
    if (value > 32767)  { ++clipCounter; return 32767; }
    if (value < -32768) { ++clipCounter; return -32768; }
    return static_cast<i16>(value);
}

}  // namespace

// ---------------------------------------------------------------------------
// ToneSource
// ---------------------------------------------------------------------------

Status ToneSource::configure(const Config& cfg) {
    if (cfg.format.kind != MediaKind::Audio) return fail(Err::BadArgument);
    if (cfg.format.sampleFormat != SampleFormat::S16) return fail(Err::NotSupported);
    if (cfg.format.sampleRate == 0 || cfg.format.channels == 0) return fail(Err::BadArgument);
    if (cfg.framesPerBlock == 0) return fail(Err::BadArgument);

    cfg_ = cfg;
    // Krok fazy w Q16 obiegu na próbkę. Q16 zamiast całkowitego, bo przy
    // 440 Hz i 16 kHz krok wynosi 7,04 — zaokrąglenie do 7 daje 437 Hz,
    // słyszalnie fałszywie.
    phaseStep_ = static_cast<u32>((static_cast<u64>(cfg.frequencyHz) << 16) /
                                  cfg.format.sampleRate);
    return ok();
}

Result<MediaFormat> ToneSource::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    HYDRA_UNUSED(in);   // źródło nie ma wejścia — format wymyśla samo
    if (!cfg_.format.valid()) return unexpected(Err::NotInitialized);
    return cfg_.format;
}

MemReq ToneSource::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(cfg_.framesPerBlock) * cfg_.format.unitBytes();
    // Trzy bloki, nie jeden: jeden jest w drodze, jeden u odbiorcy, jeden
    // wypełniany. Przy dwóch źródło czeka na zwolnienie i wchodzi w rytm
    // konsumenta zamiast trzymać własny.
    req.count = 3;
    return req;
}

Status ToneSource::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    return pool_ != nullptr ? ok() : fail(Err::OutOfMemory);
}

void ToneSource::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (finished_ || pool_ == nullptr) return;

    Block block = pool_->acquire();
    if (!block.valid()) {
        // Pula pusta oznacza konsumenta wolniejszego od źródła. To nie jest
        // błąd do zignorowania: przy dźwięku ciągłym oznacza przerwę.
        pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
        return;
    }

    const u32 unit   = cfg_.format.unitBytes();
    const u16 frames = static_cast<u16>(block.capacity / unit < cfg_.framesPerBlock
                                            ? block.capacity / unit
                                            : cfg_.framesPerBlock);

    i16* samples = reinterpret_cast<i16*>(block.data);
    for (u16 f = 0; f < frames; ++f) {
        const i32 value = (static_cast<i32>(sine256(static_cast<u8>(phase_ >> 8))) *
                           cfg_.amplitude) / 32767;
        for (u8 ch = 0; ch < cfg_.format.channels; ++ch) {
            samples[f * cfg_.format.channels + ch] = static_cast<i16>(value);
        }
        phase_ = (phase_ + phaseStep_) & 0xFFFF;
    }

    block.length = static_cast<u32>(frames) * unit;
    // Znacznik czasu z licznika próbek, nie z zegara systemowego: dźwięk ma
    // własny czas, wyznaczony przez częstotliwość próbkowania. Branie go
    // z millis() dawałoby dryf względem przetwornika.
    block.pts = frames_ * 1000000ull / cfg_.format.sampleRate;
    frames_ += frames;

    if (finishing_) { block.set(kBlockEos); finished_ = true; }

    Block evicted;
    if (!emit(0, block, evicted)) {
        pool_->release(block);
        pipeline_->raise(MediaFault::Overrun, *this, 0);
        return;
    }
    if (evicted.valid()) pool_->release(evicted);
}

// ---------------------------------------------------------------------------
// Gain
// ---------------------------------------------------------------------------

Result<MediaFormat> Gain::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    if (in.kind != MediaKind::Audio) return unexpected(Err::NotSupported);
    if (in.sampleFormat != SampleFormat::S16) return unexpected(Err::NotSupported);
    return in;   // filtr nie zmienia formatu
}

Status Gain::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    return ok();
}

void Gain::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    Block block;
    // Pętla, a nie jedno pobranie: gdy domena filtru chodzi rzadziej niż
    // źródła, kolejka rośnie i pojedyncze pobranie na krok nigdy jej nie
    // nadgoni. Ograniczeniem jest głębokość kolejki, więc pętla nie jest
    // nieskończona.
    while (take(0, block)) {
        if (gain_ != 256) {
            i16* samples = reinterpret_cast<i16*>(block.data);
            const u32 count = block.length / sizeof(i16);
            for (u32 i = 0; i < count; ++i) {
                // Q8.8 przez mnożenie na 32 bitach — na 16 przepełniłoby się
                // przy wzmocnieniu większym niż dwa.
                samples[i] = clampToS16(
                    (static_cast<i32>(samples[i]) * gain_) >> 8, clipped_);
            }
        }

        Block evicted;
        if (!emit(0, block, evicted)) {
            if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);
            pipeline_->raise(MediaFault::Overrun, *this, 0);
            return;
        }
        if (evicted.valid()) {
            if (BlockPool* p = pipeline_->pool(evicted.pool); p != nullptr) p->release(evicted);
        }
    }
}

// ---------------------------------------------------------------------------
// Tee
// ---------------------------------------------------------------------------

Result<MediaFormat> Tee::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    if (!in.valid()) return unexpected(Err::NotSupported);
    return in;
}

Status Tee::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    return ok();
}

void Tee::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    Block block;
    while (take(0, block)) {
        BlockPool* p = pipeline_->pool(block.pool);
        if (p == nullptr) continue;

        // Drugie odwołanie **przed** wysłaniem, nie po. Odbiorca pierwszej
        // gałęzi bywa w innym wątku i może zdążyć zwolnić blok, zanim
        // wrócimy — wtedy retain() trafiłby w miejsce już wolne.
        p->retain(block);

        Block evicted;
        for (u8 pad = 0; pad < 2; ++pad) {
            Block copy = block;
            if (!emit(pad, copy, evicted)) {
                p->release(copy);
                pipeline_->raise(MediaFault::Overrun, *this, pad);
                continue;
            }
            if (evicted.valid()) p->release(evicted);
        }
    }
}

// ---------------------------------------------------------------------------
// MeterSink
// ---------------------------------------------------------------------------

Status MeterSink::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    return ok();
}

void MeterSink::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    Block block;
    while (take(0, block)) {
        ++blocks_;
        bytes_ += block.length;
        lastPts_ = block.pts;

        const MediaFormat& format = input(0).format();
        if (format.kind == MediaKind::Audio && format.sampleFormat == SampleFormat::S16) {
            const i16* samples = reinterpret_cast<const i16*>(block.data);
            const u32 count = block.length / sizeof(i16);
            for (u32 i = 0; i < count; ++i) {
                // −32768 nie ma dodatniego odpowiednika w i16; negacja
                // zawinęłaby się z powrotem na siebie i szczyt wyszedłby
                // ujemny. Stąd rachunek na 32 bitach.
                const i32 magnitude = samples[i] < 0 ? -static_cast<i32>(samples[i])
                                                     : samples[i];
                if (magnitude > peak_) peak_ = static_cast<u16>(magnitude);
            }
        }

        if (block.has(kBlockEos) && !sawEos_) {
            sawEos_ = true;
            EventBus::publish(MediaEndOfStream{index(), blocks_});
        }

        if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);
    }
}

u16 MeterSink::takePeak() {
    const u16 value = peak_;
    peak_ = 0;
    return value;
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
