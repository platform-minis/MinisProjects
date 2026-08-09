/** Hydra — implementacja elementów obrazu. */

#include "hydra/media/elements/Video.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/core/Log.hpp"

#include <string.h>

HYDRA_LOG_MODULE("media.video")

namespace hydra {
namespace media {
namespace {

FrameFormat frameFormatOf(hal::CameraFormat format) {
    switch (format) {
        case hal::CameraFormat::Yuv422:    return FrameFormat::Yuv422;
        case hal::CameraFormat::Rgb565:    return FrameFormat::Rgb565;
        case hal::CameraFormat::Grayscale: return FrameFormat::Gray8;
        case hal::CameraFormat::Jpeg:      return FrameFormat::Jpeg;
        default:                           return FrameFormat::None;
    }
}

/** Bajty na piksel; 0 dla formatów o zmiennej długości. */
u8 pixelBytes(FrameFormat format) { return static_cast<u8>(bitsPerPixel(format) / 8); }

// --- konwersje barw --------------------------------------------------------

/**
 * YUV → RGB w arytmetyce stałoprzecinkowej.
 *
 * Współczynniki BT.601 przemnożone przez 256. Wersja zmiennoprzecinkowa
 * kosztowałaby na RP2040 emulację przy każdym pikselu — przy QVGA to 76 800
 * wywołań na klatkę.
 */
inline void yuvToRgb(i32 y, i32 u, i32 v, u8& r, u8& g, u8& b) {
    const i32 c = y - 16;
    const i32 d = u - 128;
    const i32 e = v - 128;

    auto clamp = [](i32 value) -> u8 {
        return static_cast<u8>(value < 0 ? 0 : (value > 255 ? 255 : value));
    };
    r = clamp((298 * c + 409 * e + 128) >> 8);
    g = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
    b = clamp((298 * c + 516 * d + 128) >> 8);
}

/** RGB888 → RGB565, bajt starszy pierwszy (układ Hydry). */
inline void putRgb565(u8* at, u8 r, u8 g, u8 b) {
    const u16 pixel = static_cast<u16>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    at[0] = static_cast<u8>(pixel >> 8);
    at[1] = static_cast<u8>(pixel);
}

}  // namespace

// ---------------------------------------------------------------------------
// JPEG
// ---------------------------------------------------------------------------

Result<JpegInfo> jpegInfo(CByteSpan data) {
    const u8* p = data.data();
    const size_t n = data.size();
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return unexpected(Err::Protocol);

    size_t at = 2;
    while (at + 4 <= n) {
        if (p[at] != 0xFF) { ++at; continue; }   // wypełnienie między znacznikami

        const u8 marker = p[at + 1];
        if (marker == 0xFF) { ++at; continue; }
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            at += 2;   // znaczniki bez ładunku
            continue;
        }

        const u16 length = static_cast<u16>((static_cast<u16>(p[at + 2]) << 8) | p[at + 3]);
        if (length < 2 || at + 2 + length > n) return unexpected(Err::Protocol);

        // SOF0…SOF15 poza 0xC4 (tablice Huffmana), 0xC8 (rozszerzenie JPEG)
        // i 0xCC (tablice arytmetyczne) — te trzy nie opisują ramki, mimo że
        // wpadają w ten sam zakres.
        const bool isSof = marker >= 0xC0 && marker <= 0xCF &&
                           marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (isSof) {
            if (length < 8) return unexpected(Err::Protocol);
            JpegInfo info;
            info.height     = static_cast<u16>((static_cast<u16>(p[at + 5]) << 8) | p[at + 6]);
            info.width      = static_cast<u16>((static_cast<u16>(p[at + 7]) << 8) | p[at + 8]);
            info.components = p[at + 9];
            return info.valid() ? Result<JpegInfo>{info}
                                : Result<JpegInfo>{unexpected(Err::Protocol)};
        }

        // SOS — dalej idą już dane skompresowane, ramki tu nie ma.
        if (marker == 0xDA) break;
        at += 2 + length;
    }
    return unexpected(Err::NotFound);
}

// ---------------------------------------------------------------------------
// CameraSource
// ---------------------------------------------------------------------------

Status CameraSource::configure(const Config& cfg) {
    cfg_ = cfg;
    if (cfg_.framesPerStep == 0) cfg_.framesPerStep = 1;
    return ok();
}

Result<MediaFormat> CameraSource::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    HYDRA_UNUSED(in);

    const FrameFormat frame = frameFormatOf(cfg_.camera.format);
    if (frame == FrameFormat::None) return unexpected(Err::NotSupported);

    format_ = MediaFormat::video(frame,
                                 hal::widthOf(cfg_.camera.resolution),
                                 hal::heightOf(cfg_.camera.resolution));
    return format_;
}

MemReq CameraSource::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    const u16 w = hal::widthOf(cfg_.camera.resolution);
    const u16 h = hal::heightOf(cfg_.camera.resolution);

    MemReq req;
    if (cfg_.camera.format == hal::CameraFormat::Jpeg) {
        // Rozmiar skompresowanej klatki zależy od treści. Jedna ósma surowej
        // to typowy wynik przy jakości 12 dla obrazu z detalami; bloków
        // większych i tak nie ma jak przewidzieć, więc liczymy je i zgłaszamy.
        req.blockSize = static_cast<u32>(w) * h / 4u;
    } else {
        req.blockSize = static_cast<u32>(w) * h *
                        pixelBytes(frameFormatOf(cfg_.camera.format));
    }
    req.count = 3;
    req.alignment = 32;
    return req;
}

Status CameraSource::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    return pool_ != nullptr ? ok() : fail(Err::OutOfMemory);
}

Status CameraSource::onStart() { return camera_.begin(cfg_.camera); }

void CameraSource::onStop() { camera_.end(); }

void CameraSource::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    for (u8 i = 0; i < cfg_.framesPerStep; ++i) {
        auto captured = camera_.capture();
        if (!captured) return;   // brak gotowej klatki to stan normalny

        hal::CameraFrame frame = *captured;

        Block block = pool_->acquire();
        if (!block.valid()) {
            // Oddajemy klatkę **przed** wyjściem. Zatrzymanie jej przy braku
            // miejsca w puli zatrzymałoby kamerę na stałe: bez wolnego bufora
            // sensor nie ma gdzie pisać, a pula i tak się nie zwolni.
            camera_.release(frame);
            pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
            return;
        }

        if (frame.length > block.capacity) {
            ++oversized_;
            HYDRA_LOGW("klatka %lu B nie mieści się w bloku %lu B — podnieś "
                       "rozmiar puli albo obniż jakość JPEG",
                       static_cast<unsigned long>(frame.length),
                       static_cast<unsigned long>(block.capacity));
            camera_.release(frame);
            pool_->release(block);
            pipeline_->raise(MediaFault::TooLarge, *this, 0);
            continue;
        }

        memcpy(block.data, frame.data, frame.length);
        block.length = frame.length;
        block.pts    = frame.timestampUs;
        // Każda klatka obrazu nieskompresowanego jest samodzielna; JPEG
        // z modułu kamery też — moduły nie robią predykcji międzyklatkowej.
        block.set(kBlockKeyframe);

        // Klatka wraca do sterownika natychmiast po skopiowaniu — powód
        // w komentarzu przy klasie.
        camera_.release(frame);
        ++frames_;

        Block evicted;
        if (!emit(0, block, evicted)) {
            pool_->release(block);
            pipeline_->raise(MediaFault::Overrun, *this, 0);
            return;
        }
        if (evicted.valid()) pool_->release(evicted);
    }
}

// ---------------------------------------------------------------------------
// Scaler
// ---------------------------------------------------------------------------

Result<MediaFormat> Scaler::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    if (in.kind != MediaKind::Video) return unexpected(Err::NotSupported);
    if (pixelBytes(in.frameFormat) == 0) {
        // JPEG i inne formaty o zmiennej długości nie mają pikseli, do których
        // dałoby się sięgnąć. Skalowanie ich wymagałoby dekodowania — czyli
        // tego, czego ten etap świadomie nie robi.
        return unexpected(Err::NotSupported);
    }

    in_  = in;
    out_ = in;
    out_.width  = outW_ != 0 ? outW_ : in.width;
    out_.height = outH_ != 0 ? outH_ : in.height;
    if (out_.width == 0 || out_.height == 0) return unexpected(Err::BadArgument);
    return out_;
}

MemReq Scaler::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = out_.frameBytes();
    req.count = 2;
    return req;
}

Status Scaler::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    if (pool_ == nullptr) return fail(Err::OutOfMemory);

    bytesPerPixel_ = pixelBytes(in_.frameFormat);
    // Współczynniki raz, w Q16. Dzielenie na piksel byłoby najdroższą
    // operacją w całym elemencie — na Cortex-M0+ nie ma go nawet w sprzęcie.
    stepX_ = (static_cast<u32>(in_.width) << 16) / out_.width;
    stepY_ = (static_cast<u32>(in_.height) << 16) / out_.height;
    return ok();
}

void Scaler::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    Block src;
    while (take(0, src)) {
        BlockPool* srcPool = pipeline_->pool(src.pool);

        Block dst = pool_->acquire();
        if (!dst.valid()) {
            if (srcPool != nullptr) srcPool->release(src);
            pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
            return;
        }

        const u32 srcStride = static_cast<u32>(in_.width) * bytesPerPixel_;
        const u32 dstStride = static_cast<u32>(out_.width) * bytesPerPixel_;

        u32 sy = 0;
        for (u16 y = 0; y < out_.height; ++y) {
            const u8* srcRow = src.data + static_cast<size_t>(sy >> 16) * srcStride;
            u8*       dstRow = dst.data + static_cast<size_t>(y) * dstStride;

            u32 sx = 0;
            for (u16 x = 0; x < out_.width; ++x) {
                const u8* from = srcRow + static_cast<size_t>(sx >> 16) * bytesPerPixel_;
                u8* to = dstRow + static_cast<size_t>(x) * bytesPerPixel_;
                for (u8 b = 0; b < bytesPerPixel_; ++b) to[b] = from[b];
                sx += stepX_;
            }
            sy += stepY_;
        }

        dst.length = out_.frameBytes();
        dst.pts    = src.pts;
        dst.flags  = src.flags;
        ++frames_;

        if (srcPool != nullptr) srcPool->release(src);

        Block evicted;
        if (!emit(0, dst, evicted)) {
            pool_->release(dst);
            pipeline_->raise(MediaFault::Overrun, *this, 0);
            return;
        }
        if (evicted.valid()) pool_->release(evicted);
    }
}

// ---------------------------------------------------------------------------
// ColorConvert
// ---------------------------------------------------------------------------

Result<MediaFormat> ColorConvert::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    if (in.kind != MediaKind::Video) return unexpected(Err::NotSupported);

    const bool supported =
        (in.frameFormat == FrameFormat::Yuv422 && target_ == FrameFormat::Rgb565) ||
        (in.frameFormat == FrameFormat::Yuv422 && target_ == FrameFormat::Gray8)  ||
        (in.frameFormat == FrameFormat::Rgb565 && target_ == FrameFormat::Gray8);
    if (!supported) {
        HYDRA_LOGE("convert: %s → %s nieobsługiwane", toString(in.frameFormat),
                   toString(target_));
        return unexpected(Err::NotSupported);
    }

    in_  = in;
    out_ = in;
    out_.frameFormat = target_;
    return out_;
}

MemReq ColorConvert::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = out_.frameBytes();
    req.count = 2;
    return req;
}

Status ColorConvert::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    return pool_ != nullptr ? ok() : fail(Err::OutOfMemory);
}

void ColorConvert::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    Block src;
    while (take(0, src)) {
        BlockPool* srcPool = pipeline_->pool(src.pool);

        Block dst = pool_->acquire();
        if (!dst.valid()) {
            if (srcPool != nullptr) srcPool->release(src);
            pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
            return;
        }

        const u32 pixels = static_cast<u32>(in_.width) * in_.height;

        if (in_.frameFormat == FrameFormat::Yuv422 && target_ == FrameFormat::Gray8) {
            // Składowa Y **jest** jasnością — konwersja to wybranie co drugiego
            // bajtu. To jedyny powód, dla którego warto trzymać sensor w YUV,
            // gdy liczy się tylko luminancja.
            for (u32 i = 0; i < pixels; ++i) dst.data[i] = src.data[i * 2];

        } else if (in_.frameFormat == FrameFormat::Yuv422) {
            // YUYV: dwa piksele dzielą jedną parę chrominancji.
            for (u32 i = 0; i < pixels; i += 2) {
                const i32 y0 = src.data[i * 2];
                const i32 u  = src.data[i * 2 + 1];
                const i32 y1 = src.data[i * 2 + 2];
                const i32 v  = src.data[i * 2 + 3];

                u8 r, g, b;
                yuvToRgb(y0, u, v, r, g, b);
                putRgb565(dst.data + i * 2, r, g, b);
                yuvToRgb(y1, u, v, r, g, b);
                putRgb565(dst.data + (i + 1) * 2, r, g, b);
            }

        } else {   // Rgb565 → Gray8
            for (u32 i = 0; i < pixels; ++i) {
                const u16 pixel = static_cast<u16>((static_cast<u16>(src.data[i * 2]) << 8) |
                                                   src.data[i * 2 + 1]);
                // Rozciągnięcie 5/6 bitów na 8 i wagi BT.601 w postaci
                // całkowitej: 77/150/29 sumuje się do 256.
                const i32 r = ((pixel >> 11) & 0x1F) << 3;
                const i32 g = ((pixel >> 5) & 0x3F) << 2;
                const i32 b = (pixel & 0x1F) << 3;
                dst.data[i] = static_cast<u8>((77 * r + 150 * g + 29 * b) >> 8);
            }
        }

        dst.length = out_.frameBytes();
        dst.pts    = src.pts;
        dst.flags  = src.flags;
        ++frames_;

        if (srcPool != nullptr) srcPool->release(src);

        Block evicted;
        if (!emit(0, dst, evicted)) {
            pool_->release(dst);
            pipeline_->raise(MediaFault::Overrun, *this, 0);
            return;
        }
        if (evicted.valid()) pool_->release(evicted);
    }
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
