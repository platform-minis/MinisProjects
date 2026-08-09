/** Hydra — implementacja elementów plikowych potoku. */

#include "hydra/media/elements/Files.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/core/Log.hpp"

#include <string.h>

HYDRA_LOG_MODULE("media.file")

namespace hydra {
namespace media {
namespace {

/** Nagłówek WAV jest little-endian niezależnie od procesora. */
u32 le32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
u16 le16(const u8* p) {
    return static_cast<u16>(static_cast<u16>(p[0]) | (static_cast<u16>(p[1]) << 8));
}
void put32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v);        p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16);  p[3] = static_cast<u8>(v >> 24);
}
void put16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v);  p[1] = static_cast<u8>(v >> 8);
}

bool tagIs(const u8* p, const char* tag) { return memcmp(p, tag, 4) == 0; }

/** Przesunięcia pól, które trzeba uzupełnić po zamknięciu nagrania. */
constexpr u32 kRiffSizeOffset = 4;
constexpr u32 kDataSizeOffset = 40;
constexpr u32 kWavHeaderSize  = 44;

}  // namespace

// ---------------------------------------------------------------------------
// Nagłówek WAV
// ---------------------------------------------------------------------------

Result<WavInfo> parseWavHeader(CByteSpan header) {
    const u8* p = header.data();
    const size_t n = header.size();
    if (n < 12) return unexpected(Err::Protocol);
    if (!tagIs(p, "RIFF") || !tagIs(p + 8, "WAVE")) return unexpected(Err::Protocol);

    WavInfo info;
    bool haveFmt = false;

    // Przechodzimy listę fragmentów. Zakładanie, że `data` leży pod stałym
    // przesunięciem 44, działa wyłącznie z plikami wyprodukowanymi przez ten
    // sam kod — edytory wstawiają między `fmt ` a `data` fragment `LIST`
    // z nazwą programu, a rekordery `fact`.
    size_t at = 12;
    while (at + 8 <= n) {
        const u8*  tag  = p + at;
        const u32  size = le32(p + at + 4);
        const size_t body = at + 8;

        if (tagIs(tag, "fmt ") && body + 16 <= n) {
            const u16 audioFormat = le16(p + body);
            const u16 channels    = le16(p + body + 2);
            const u32 rate        = le32(p + body + 4);
            const u16 bits        = le16(p + body + 14);

            // Tylko PCM. Formaty skompresowane (ADPCM, μ-law) mają ten sam
            // nagłówek i inny ładunek — odtworzone jako PCM dają szum
            // o pełnej głośności, czyli najgorszy możliwy sposób na
            // powiadomienie użytkownika, że plik jest nieobsługiwany.
            if (audioFormat != 1) return unexpected(Err::NotSupported);

            SampleFormat sample = SampleFormat::None;
            switch (bits) {
                case 8:  sample = SampleFormat::U8;  break;
                case 16: sample = SampleFormat::S16; break;
                case 24: sample = SampleFormat::S24; break;
                case 32: sample = SampleFormat::S32; break;
                default: return unexpected(Err::NotSupported);
            }
            info.format = MediaFormat::audio(rate, sample, static_cast<u8>(channels));
            haveFmt = true;
        } else if (tagIs(tag, "data")) {
            info.dataOffset = static_cast<u32>(body);
            info.dataBytes  = size;
            // `data` jest ostatnim, co nas interesuje — reszta pliku to on sam.
            break;
        }

        // Fragmenty są wyrównane do parzystej długości; nieparzysty rozmiar
        // niesie bajt dopełnienia, którego nie ma w polu długości.
        at = body + size + (size & 1u);
    }

    if (!haveFmt || info.dataOffset == 0) return unexpected(Err::Protocol);
    return info;
}

size_t buildWavHeader(ByteSpan out, const MediaFormat& format, u32 dataBytes) {
    if (out.size() < kWavHeaderSize || format.kind != MediaKind::Audio) return 0;

    u8* p = out.data();
    const u16 bits      = static_cast<u16>(bytesPerSample(format.sampleFormat) * 8);
    const u16 blockAlign = static_cast<u16>(format.unitBytes());
    const u32 byteRate   = format.sampleRate * blockAlign;

    memcpy(p, "RIFF", 4);
    put32(p + 4, 36 + dataBytes);
    memcpy(p + 8, "WAVE", 4);
    memcpy(p + 12, "fmt ", 4);
    put32(p + 16, 16);
    put16(p + 20, 1);                    // PCM
    put16(p + 22, format.channels);
    put32(p + 24, format.sampleRate);
    put32(p + 28, byteRate);
    put16(p + 32, blockAlign);
    put16(p + 34, bits);
    memcpy(p + 36, "data", 4);
    put32(p + 40, dataBytes);
    return kWavHeaderSize;
}

// ---------------------------------------------------------------------------
// FileSource
// ---------------------------------------------------------------------------

Status FileSource::configure(const Config& cfg) {
    if (cfg.path == nullptr || cfg.framesPerBlock == 0) return fail(Err::BadArgument);
    cfg_ = cfg;
    if (cfg_.blocksPerStep == 0) cfg_.blocksPerStep = 1;
    return ok();
}

Status FileSource::openAndParse() {
    auto opened = fs_.open(cfg_.path, hal::OpenMode::Read);
    if (!opened) return fail(opened.error());
    file_ = *opened;

    u8 header[128];
    auto got = file_->read(ByteSpan{header, sizeof(header)});
    if (!got) { file_->close(); file_ = nullptr; return fail(got.error()); }

    if (auto parsed = parseWavHeader(CByteSpan{header, *got}); parsed) {
        info_ = *parsed;
    } else if (cfg_.rawFormat.valid()) {
        // Nie WAV — bierzemy format z konfiguracji i całą zawartość za dane.
        info_.format     = cfg_.rawFormat;
        info_.dataOffset = 0;
        info_.dataBytes  = static_cast<u32>(file_->size());
    } else {
        HYDRA_LOGE("%s: nie jest plikiem WAV, a nie podano formatu surowego",
                   cfg_.path);
        file_->close();
        file_ = nullptr;
        return fail(Err::Protocol);
    }

    // Deklarowany rozmiar bywa większy od pliku — tak wygląda nagranie
    // przerwane zanikiem zasilania. Odtwarzamy tyle, ile naprawdę jest,
    // zamiast czytać poza koniec.
    const u32 available = static_cast<u32>(file_->size()) - info_.dataOffset;
    if (info_.dataBytes > available) info_.dataBytes = available;

    HYDRA_CHECK(file_->seek(info_.dataOffset));
    consumed_ = 0;
    finished_ = false;
    return ok();
}

Result<MediaFormat> FileSource::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    HYDRA_UNUSED(in);

    // Format musi być znany **przed** startem, bo od niego zależy negocjacja
    // reszty potoku. Otwieramy więc plik już tutaj i zamykamy od razu —
    // sam nagłówek to jeden odczyt.
    if (!info_.format.valid()) {
        HYDRA_CHECK(openAndParse());
        file_->close();
        file_ = nullptr;
    }
    return info_.format;
}

MemReq FileSource::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(cfg_.framesPerBlock) * info_.format.unitBytes();
    req.count = 3;
    return req;
}

Status FileSource::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    return pool_ != nullptr ? ok() : fail(Err::OutOfMemory);
}

Status FileSource::onStart() { return openAndParse(); }

void FileSource::onStop() {
    if (file_ != nullptr) { file_->close(); file_ = nullptr; }
}

void FileSource::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (file_ == nullptr || finished_) return;

    // Ograniczona liczba bloków na krok. Pętla do końca pliku zabrałaby cały
    // czas domeny — a karta SD potrafi zamilknąć na sto milisekund przy
    // kasowaniu sektora.
    for (u8 i = 0; i < cfg_.blocksPerStep; ++i) {
        if (consumed_ >= info_.dataBytes) {
            if (!cfg_.loop) {
                // Pusty blok z flagą końca: ujście musi się dowiedzieć, że to
                // koniec, a nie chwilowy brak danych.
                Block last = pool_->acquire();
                if (last.valid()) {
                    last.length = 0;
                    last.set(kBlockEos);
                    Block evicted;
                    if (!emit(0, last, evicted)) pool_->release(last);
                    else if (evicted.valid()) pool_->release(evicted);
                }
                finished_ = true;
                return;
            }
            if (!file_->seek(info_.dataOffset)) return;
            consumed_ = 0;
        }

        Block block = pool_->acquire();
        if (!block.valid()) {
            pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
            return;
        }

        const u32 left = info_.dataBytes - consumed_;
        const u32 want = block.capacity < left ? block.capacity : left;

        auto got = file_->read(ByteSpan{block.data, want});
        if (!got || *got == 0) {
            pool_->release(block);
            finished_ = true;
            return;
        }

        block.length = static_cast<u32>(*got);
        block.pts = frames_ * 1000000ull / info_.format.sampleRate;
        consumed_ += block.length;
        frames_ += block.length / info_.format.unitBytes();

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
// FileSink
// ---------------------------------------------------------------------------

Status FileSink::configure(const Config& cfg) {
    if (cfg.path == nullptr) return fail(Err::BadArgument);
    cfg_ = cfg;
    if (cfg_.blocksPerStep == 0) cfg_.blocksPerStep = 1;
    return ok();
}

Result<MediaFormat> FileSink::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    return in;
}

Status FileSink::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    format_ = input(0).format();
    if (cfg_.writeWavHeader && format_.kind != MediaKind::Audio) {
        HYDRA_LOGE("file-out: nagłówek WAV opisuje dźwięk, a na wejściu jest %s",
                   format_.kind == MediaKind::Video ? "obraz" : "nic");
        return fail(Err::NotSupported);
    }
    return ok();
}

Status FileSink::onStart() {
    auto opened = fs_.open(cfg_.path, hal::OpenMode::Write);
    if (!opened) return fail(opened.error());
    file_ = *opened;
    written_ = 0;
    sincePatch_ = 0;

    if (cfg_.writeWavHeader) {
        u8 header[kWavHeaderSize];
        const size_t n = buildWavHeader(ByteSpan{header, sizeof(header)}, format_, 0);
        if (n == 0) { file_->close(); file_ = nullptr; return fail(Err::BadArgument); }
        if (auto w = file_->write(CByteSpan{header, n}); !w) {
            file_->close();
            file_ = nullptr;
            return fail(w.error());
        }
    }
    return ok();
}

void FileSink::patchHeader() {
    if (file_ == nullptr || !cfg_.writeWavHeader) return;

    const size_t at = file_->position();
    u8 field[4];

    put32(field, static_cast<u32>(36 + written_));
    if (file_->seek(kRiffSizeOffset)) (void)file_->write(CByteSpan{field, 4});

    put32(field, static_cast<u32>(written_));
    if (file_->seek(kDataSizeOffset)) (void)file_->write(CByteSpan{field, 4});

    (void)file_->seek(at);
    (void)file_->flush();
}

void FileSink::onStop() {
    if (file_ == nullptr) return;
    patchHeader();
    file_->close();
    file_ = nullptr;
}

void FileSink::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (file_ == nullptr) return;

    Block block;
    for (u8 i = 0; i < cfg_.blocksPerStep && take(0, block); ++i) {
        if (block.length > 0) {
            auto w = file_->write(CByteSpan{block.data, block.length});
            if (!w || *w != block.length) {
                // Karta pełna albo wyjęta. Liczymy i idziemy dalej: przerwanie
                // potoku zabrałoby też dźwięk, który akurat gra z drugiej
                // gałęzi, a nagranie i tak jest już niekompletne.
                ++errors_;
                pipeline_->raise(MediaFault::TooLarge, *this, 0);
            } else {
                written_ += block.length;
            }
        }

        const bool eos = block.has(kBlockEos);
        if (BlockPool* p = pipeline_->pool(block.pool); p != nullptr) p->release(block);

        if (eos) {
            EventBus::publish(MediaEndOfStream{index(), written_});
            patchHeader();
            file_->close();
            file_ = nullptr;
            return;
        }

        // Uzupełnianie rozmiarów co kilka bloków. Nagranie przerwane zanikiem
        // zasilania ma wtedy poprawny nagłówek z dokładnością do ogona,
        // zamiast zer, przy których odtwarzacz mówi „plik uszkodzony".
        if (cfg_.patchEvery > 0 && ++sincePatch_ >= cfg_.patchEvery) {
            sincePatch_ = 0;
            patchHeader();
        }
    }
}

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
