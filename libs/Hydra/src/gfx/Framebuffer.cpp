/** Hydra — implementacja powierzchni rysującej do bufora w pamięci. */

#include "hydra/gfx/Framebuffer.hpp"

#include <string.h>

namespace hydra {
namespace gfx {

Status Framebuffer::attach(ByteSpan buffer, i16 w, i16 h, PixelFormat fmt) {
    if (!buffer.data() || w <= 0 || h <= 0) return fail(Err::BadArgument);

    const size_t needed = bytesNeeded(w, h, fmt);
    if (buffer.size() < needed) return fail(Err::OutOfRange);

    buffer_ = buffer;
    size_   = Size(w, h);
    format_ = fmt;
    stride_ = rowStride(w, fmt);
    resetClip();
    clearDirty();
    return ok();
}

Status Framebuffer::writePixel(i16 x, i16 y, Color c) {
    if (!attached()) return fail(Err::NotInitialized);

    u8* row = buffer_.data() + static_cast<size_t>(y) * stride_;

    switch (format_) {
        case PixelFormat::Mono1: {
            // Bit najstarszy z lewej — układ zgodny z bitmapami Adafruit
            // i pamięcią obrazu większości kontrolerów e-papieru.
            const u8 mask = static_cast<u8>(0x80 >> (x % 8));
            if (c.mono()) row[x / 8] = static_cast<u8>(row[x / 8] | mask);
            else          row[x / 8] = static_cast<u8>(row[x / 8] & ~mask);
            break;
        }
        case PixelFormat::Rgb565: {
            const u16 v = c.rgb565();
            // Big-endian: taka kolejność bajtów idzie na magistralę SPI
            // do kontrolerów ILI9341, ST7789 i pokrewnych.
            row[x * 2]     = static_cast<u8>(v >> 8);
            row[x * 2 + 1] = static_cast<u8>(v & 0xFF);
            break;
        }
        case PixelFormat::Rgb888:
            row[x * 3]     = c.r;
            row[x * 3 + 1] = c.g;
            row[x * 3 + 2] = c.b;
            break;

        case PixelFormat::Rgba8888:
            row[x * 4]     = c.r;
            row[x * 4 + 1] = c.g;
            row[x * 4 + 2] = c.b;
            row[x * 4 + 3] = c.a;
            break;
    }
    return ok();
}

Result<Color> Framebuffer::readPixel(i16 x, i16 y) const {
    if (!attached()) return unexpected(Err::NotInitialized);
    if (!bounds().contains(x, y)) return unexpected(Err::OutOfRange);

    const u8* row = buffer_.data() + static_cast<size_t>(y) * stride_;

    switch (format_) {
        case PixelFormat::Mono1: {
            const u8 mask = static_cast<u8>(0x80 >> (x % 8));
            return (row[x / 8] & mask) ? colors::white : colors::black;
        }
        case PixelFormat::Rgb565:
            return Color::fromRgb565(
                static_cast<u16>((static_cast<u16>(row[x * 2]) << 8) | row[x * 2 + 1]));
        case PixelFormat::Rgb888:
            return Color(row[x * 3], row[x * 3 + 1], row[x * 3 + 2]);
        case PixelFormat::Rgba8888:
            return Color(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]);
    }
    return unexpected(Err::NotSupported);
}

Result<Color> Framebuffer::pixelAt(i16 x, i16 y) const { return readPixel(x, y); }

Status Framebuffer::fill(Color c) {
    if (!attached()) return fail(Err::NotInitialized);

    // Wypełnienie całości jednolitym kolorem sprowadza się do memset tylko
    // wtedy, gdy wzorzec bajtu się powtarza. W pozostałych przypadkach oraz
    // przy zawężonym obszarze przycinania wracamy do ścieżki ogólnej.
    const bool wholeSurface = clip() == bounds();
    if (!wholeSurface || !c.opaque()) return ISurface::fill(c);

    switch (format_) {
        case PixelFormat::Mono1:
            memset(buffer_.data(), c.mono() ? 0xFF : 0x00,
                   static_cast<size_t>(stride_) * size_.h);
            break;

        case PixelFormat::Rgb565: {
            const u16 v  = c.rgb565();
            const u8  hi = static_cast<u8>(v >> 8);
            const u8  lo = static_cast<u8>(v & 0xFF);
            if (hi == lo) {
                memset(buffer_.data(), hi, static_cast<size_t>(stride_) * size_.h);
            } else {
                u8* p = buffer_.data();
                for (size_t i = 0, n = static_cast<size_t>(stride_) * size_.h; i + 1 < n;
                     i += 2) {
                    p[i]     = hi;
                    p[i + 1] = lo;
                }
            }
            break;
        }

        case PixelFormat::Rgb888:
        case PixelFormat::Rgba8888:
            return ISurface::fill(c);
    }

    markDirty(bounds());
    return ok();
}

Status Framebuffer::flush() {
    if (!attached()) return fail(Err::NotInitialized);

    if (present_) {
        HYDRA_CHECK(present_(data(), size_, format_));
    }
    clearDirty();
    return ok();
}

}  // namespace gfx
}  // namespace hydra
