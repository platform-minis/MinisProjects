/** Hydra — atrapa I2S z pętlą zwrotną. */

#include "hydra/hal/Mock.hpp"

#if HYDRA_PLAT_HOST

#include <string.h>

namespace hydra {
namespace hal {
namespace mock {

Status MockI2s::begin(const I2sConfig& cfg) {
    if (cfg.sampleRate == 0 || cfg.channels == 0) return fail(Err::BadArgument);
    if (cfg.bitsPerSample != 16 && cfg.bitsPerSample != 24 && cfg.bitsPerSample != 32) {
        return fail(Err::NotSupported);
    }
    cfg_ = cfg;
    running_ = true;
    return ok();
}

void MockI2s::end() {
    running_ = false;
    for (auto& slot : slots_) slot.used = false;
}

Status MockI2s::submit(ByteSpan buffer) {
    if (!running_) return fail(Err::NotInitialized);
    if (buffer.data() == nullptr || buffer.size() == 0) return fail(Err::BadArgument);

    for (auto& slot : slots_) {
        if (slot.used) continue;

        slot.buffer = buffer;
        slot.age    = 0;
        slot.used   = true;

        if (cfg_.direction == I2sDirection::Tx) {
            // Nadanie: treść trafia do dziennika i do strumienia zwrotnego.
            // Pętla jest tu po to, żeby test mógł zbudować łańcuch
            // źródło → filtr → I2S → I2S → ujście i porównać próbki.
            const size_t take = buffer.size() < kLogBytes - logLen_
                                    ? buffer.size() : kLogBytes - logLen_;
            memcpy(log_ + logLen_, buffer.data(), take);
            logLen_ += take;

            const size_t loop = buffer.size() < kLogBytes - rxLen_
                                    ? buffer.size() : kLogBytes - rxLen_;
            memcpy(rx_ + rxLen_, buffer.data(), loop);
            rxLen_ += loop;

            slot.bytes = static_cast<u32>(buffer.size());
        } else {
            // Odbiór: sterownik wypełnia bufor. Brak danych to nie błąd, tylko
            // cisza — sprzęt oddaje wtedy zera i podbija licznik xrun, dokładnie
            // jak prawdziwy kontroler, który nie zdążył nic wczytać.
            const size_t ready = rxLen_ - rxRead_;
            const size_t take = ready < buffer.size() ? ready : buffer.size();
            if (take > 0) memcpy(buffer.data(), rx_ + rxRead_, take);
            if (take < buffer.size()) {
                memset(buffer.data() + take, 0, buffer.size() - take);
                ++xruns_;
            }
            rxRead_ += take;
            slot.bytes = static_cast<u32>(buffer.size());
        }
        return ok();
    }
    return fail(Err::Busy);
}

bool MockI2s::reclaim(ByteSpan& buffer, u32& bytes) {
    for (auto& slot : slots_) {
        if (!slot.used) continue;
        if (slot.age < latency_) { ++slot.age; continue; }

        buffer = slot.buffer;
        bytes  = slot.bytes;
        slot.used = false;
        return true;
    }
    return false;
}

void MockI2s::feed(CByteSpan data) {
    const size_t take = data.size() < kLogBytes - rxLen_ ? data.size() : kLogBytes - rxLen_;
    memcpy(rx_ + rxLen_, data.data(), take);
    rxLen_ += take;
}

void MockI2s::clear() {
    running_ = false;
    latency_ = 0;
    xruns_   = 0;
    logLen_  = 0;
    rxLen_   = 0;
    rxRead_  = 0;
    cfg_     = I2sConfig{};
    for (auto& slot : slots_) slot.used = false;
}

// ---------------------------------------------------------------------------

Status MockDac::enable(u8 channel) {
    if (channel >= kChannels) return fail(Err::BadArgument);
    on_[channel] = true;
    return ok();
}

void MockDac::disable(u8 channel) {
    if (channel < kChannels) on_[channel] = false;
}

Status MockDac::write(u8 channel, u16 value) {
    if (channel >= kChannels) return fail(Err::BadArgument);
    // Zapis do wyłączonego kanału jest błędem, a nie ciszą: na sprzęcie
    // wyjście zostaje w stanie wysokiej impedancji i „nic nie słychać".
    if (!on_[channel]) return fail(Err::NotInitialized);
    value_[channel] = value;
    ++writes_[channel];
    return ok();
}

void MockDac::clear() {
    for (u8 i = 0; i < kChannels; ++i) { value_[i] = 0; writes_[i] = 0; on_[i] = false; }
}

// ---------------------------------------------------------------------------
// MockCamera
// ---------------------------------------------------------------------------

Status MockCamera::begin(const CameraConfig& cfg) {
    const u32 bytes = static_cast<u32>(widthOf(cfg.resolution)) *
                      heightOf(cfg.resolution) *
                      (cfg.format == CameraFormat::Grayscale ? 1u : 2u);
    // Atrapa ma bufory o stałym rozmiarze; większa rozdzielczość wymagałaby
    // sterty, a tej w Hydrze nie ma. Odmowa jest lepsza niż cicho mniejsza
    // klatka, którą reszta potoku uzna za uszkodzoną.
    if (cfg.format == CameraFormat::Jpeg) return fail(Err::NotSupported);
    if (bytes > kMaxFrameBytes) return fail(Err::OutOfRange);
    if (cfg.bufferCount == 0 || cfg.bufferCount > kMaxBuffers) return fail(Err::BadArgument);

    cfg_ = cfg;
    running_ = true;
    return ok();
}

void MockCamera::end() {
    running_ = false;
    for (auto& slot : slots_) slot.lent = false;
}

/** Pionowe pasy przesuwane o piksel na klatkę — ruch odróżnia potok od zaciętego. */
void MockCamera::paint(u8* buffer, u16 width, u16 height, u8 phase) {
    const bool gray = cfg_.format == CameraFormat::Grayscale;
    for (u16 y = 0; y < height; ++y) {
        for (u16 x = 0; x < width; ++x) {
            const u8 level = static_cast<u8>(((x + phase) * 8 / (width ? width : 1)) * 32);
            if (gray) {
                buffer[static_cast<size_t>(y) * width + x] = level;
            } else {
                // Rgb565 bajtami od najstarszego — układ, w którym Hydra
                // trzyma ten format wszędzie indziej.
                const u16 pixel = static_cast<u16>(((level >> 3) << 11) |
                                                   ((level >> 2) << 5) | (level >> 3));
                const size_t at = (static_cast<size_t>(y) * width + x) * 2;
                buffer[at]     = static_cast<u8>(pixel >> 8);
                buffer[at + 1] = static_cast<u8>(pixel);
            }
        }
    }
}

Result<CameraFrame> MockCamera::capture() {
    if (!running_) return unexpected(Err::NotInitialized);

    for (u8 i = 0; i < cfg_.bufferCount; ++i) {
        if (slots_[i].lent) continue;

        const u16 w = widthOf(cfg_.resolution);
        const u16 h = heightOf(cfg_.resolution);
        paint(slots_[i].data, w, h, phase_++);
        slots_[i].lent = true;
        ++produced_;

        CameraFrame frame;
        frame.data   = slots_[i].data;
        frame.length = static_cast<u32>(w) * h *
                       (cfg_.format == CameraFormat::Grayscale ? 1u : 2u);
        frame.width  = w;
        frame.height = h;
        frame.format = cfg_.format;
        frame.timestampUs = static_cast<u64>(produced_) * 33333;   // ~30 kl./s
        frame.handle = &slots_[i];
        return frame;
    }

    // Wszystkie bufory pożyczone. Na sprzęcie oznacza to klatkę, której sensor
    // nie miał gdzie zapisać — liczymy ją tak samo.
    ++dropped_;
    return unexpected(Err::Busy);
}

void MockCamera::release(CameraFrame& frame) {
    if (frame.handle == nullptr) return;
    static_cast<Slot*>(frame.handle)->lent = false;
    frame = CameraFrame{};
}

u8 MockCamera::borrowed() const {
    u8 count = 0;
    for (u8 i = 0; i < cfg_.bufferCount; ++i) if (slots_[i].lent) ++count;
    return count;
}

void MockCamera::clear() {
    running_ = false;
    produced_ = 0;
    dropped_ = 0;
    phase_ = 0;
    cfg_ = CameraConfig{};
    for (auto& slot : slots_) slot.lent = false;
}

}  // namespace mock
}  // namespace hal
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST
