#pragma once
/**
 * Hydra — powierzchnia rysująca do bufora w pamięci.
 *
 * Podstawa dla wszystkiego, co ma własny bufor obrazu: monochromatycznych
 * OLED-ów, e-papieru, paneli z transferem DMA, a także testów — bo bufor
 * w pamięci da się sprawdzić piksel po pikselu bez sprzętu.
 *
 * Bufor dostarcza wołający i to on jest jego właścicielem. Framebuffer nigdy
 * nie alokuje (rozdz. 11): rozmiar obrazu jest znany na etapie projektowania
 * urządzenia, więc bufor powinien być statyczny albo świadomie przydzielony
 * w fazie inicjalizacji — na ESP32-S3 z PSRAM najczęściej właśnie tam.
 *
 *     static u8 vram[Framebuffer::bytesNeeded(128, 64, PixelFormat::Mono1)];
 *     Framebuffer fb;
 *     fb.attach(ByteSpan{vram, sizeof(vram)}, 128, 64, PixelFormat::Mono1);
 */

#include "hydra/core/Delegate.hpp"
#include "hydra/gfx/ISurface.hpp"

namespace hydra {
namespace gfx {

class Framebuffer : public ISurface {
public:
    /**
     * Funkcja przenosząca zawartość bufora na panel. Wołana z flush().
     * Bez niej flush() jedynie kasuje znacznik zmian — tak działa framebuffer
     * używany wyłącznie jako cel renderowania (np. w testach).
     */
    using PresentFn = Delegate<Status(CByteSpan, Size, PixelFormat)>;

    /** Liczba bajtów potrzebna na obraz o zadanych wymiarach i formacie. */
    static constexpr size_t bytesNeeded(i16 w, i16 h, PixelFormat fmt) {
        return static_cast<size_t>(rowStride(w, fmt)) * static_cast<size_t>(w > 0 ? h : 0);
    }

    /** Długość jednego wiersza w bajtach; wiersze są wyrównane do pełnego bajtu. */
    static constexpr u32 rowStride(i16 w, PixelFormat fmt) {
        return fmt == PixelFormat::Mono1
                   ? static_cast<u32>((w + 7) / 8)
                   : static_cast<u32>(w) * (bitsPerPixel(fmt) / 8);
    }

    Framebuffer() = default;

    /** Podpina bufor. Zwraca Err::OutOfRange, gdy jest za mały. */
    Status attach(ByteSpan buffer, i16 w, i16 h, PixelFormat fmt);
    bool   attached() const { return buffer_.data() != nullptr; }

    void setPresent(PresentFn fn) { present_ = fn; }

    Size        size() const override { return size_; }
    PixelFormat pixelFormat() const override { return format_; }

    Status fill(Color c) override;
    Status flush() override;

    /** Surowa zawartość bufora — do przekazania sterownikowi panelu. */
    CByteSpan data() const { return CByteSpan{buffer_.data(), buffer_.size()}; }
    u32       stride() const { return stride_; }

    /** Odczyt piksela; wygodne w testach i przy mieszaniu z kanałem alfa. */
    Result<Color> pixelAt(i16 x, i16 y) const;

protected:
    Status        writePixel(i16 x, i16 y, Color c) override;
    Result<Color> readPixel(i16 x, i16 y) const override;

private:
    ByteSpan    buffer_{};
    Size        size_{};
    PixelFormat format_ = PixelFormat::Rgb565;
    u32         stride_ = 0;
    PresentFn   present_{};
};

}  // namespace gfx
}  // namespace hydra
