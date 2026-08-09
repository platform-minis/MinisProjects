/** Hydra — implementacja strumienia multimedialnego przez sieć. */

#include "hydra/media/elements/Net.hpp"

#if HYDRA_ENABLE_MEDIA && HYDRA_ENABLE_NET

#include "hydra/core/Log.hpp"

#include <string.h>

HYDRA_LOG_MODULE("media.net")

namespace hydra {
namespace media {

size_t buildNetHeader(ByteSpan out, u32 length, u64 pts, u8 flags) {
    if (out.size() < kNetHeaderSize) return 0;

    u8* p = out.data();
    p[0] = kNetMagic0;
    p[1] = kNetMagic1;
    p[2] = kNetVersion;
    p[3] = flags;
    for (u8 i = 0; i < 4; ++i) p[4 + i] = static_cast<u8>(length >> (i * 8));
    for (u8 i = 0; i < 8; ++i) p[8 + i] = static_cast<u8>(pts >> (i * 8));
    return kNetHeaderSize;
}

bool parseNetHeader(CByteSpan in, u32& length, u64& pts, u8& flags) {
    if (in.size() < kNetHeaderSize) return false;
    const u8* p = in.data();
    if (p[0] != kNetMagic0 || p[1] != kNetMagic1 || p[2] != kNetVersion) return false;

    flags = p[3];
    length = 0;
    for (u8 i = 0; i < 4; ++i) length |= static_cast<u32>(p[4 + i]) << (i * 8);
    pts = 0;
    for (u8 i = 0; i < 8; ++i) pts |= static_cast<u64>(p[8 + i]) << (i * 8);
    return true;
}

// ---------------------------------------------------------------------------
// NetSink
// ---------------------------------------------------------------------------

Result<MediaFormat> NetSink::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    return in;
}

Status NetSink::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    return ok();
}

void NetSink::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);

    Block block;
    for (u8 i = 0; i < cfg_.blocksPerStep && take(0, block); ++i) {
        BlockPool* pool = pipeline_->pool(block.pool);

        if (!client_.connected()) {
            // Brak połączenia nie zatrzymuje potoku: dźwięk ma dalej grać
            // lokalnie, a strumień wznowi się, gdy gniazdo wróci.
            ++dropped_;
            if (pool != nullptr) pool->release(block);
            continue;
        }

        u8 header[kNetHeaderSize];
        buildNetHeader(ByteSpan{header, sizeof(header)}, block.length, block.pts,
                       block.flags);

        const size_t wroteHeader = client_.write(CByteSpan{header, kNetHeaderSize});
        size_t wroteBody = 0;
        if (wroteHeader == kNetHeaderSize && block.length > 0) {
            wroteBody = client_.write(CByteSpan{block.data, block.length});
        }

        if (wroteHeader != kNetHeaderSize ||
            (block.length > 0 && wroteBody != block.length)) {
            // Gniazdo zapchane. Dosyłanie reszty w pętli zablokowałoby domenę
            // na czas, którego nikt nie ogranicza, a wysłanie połowy bloku
            // rozsypałoby odbiornikowi ramkowanie na resztę połączenia.
            ++dropped_;
            pipeline_->raise(MediaFault::Overrun, *this, 0);
            if (cfg_.dropOnPartialWrite && wroteHeader == kNetHeaderSize) {
                HYDRA_LOGW("net-out: zapisano %u z %lu bajtów — odbiornik "
                           "zsynchronizuje się po następnej magii",
                           static_cast<unsigned>(wroteBody),
                           static_cast<unsigned long>(block.length));
            }
        } else {
            sent_ += kNetHeaderSize + block.length;
        }

        if (pool != nullptr) pool->release(block);
    }
}

// ---------------------------------------------------------------------------
// NetSource
// ---------------------------------------------------------------------------

Status NetSource::configure(const Config& cfg) {
    if (!cfg.format.valid() || cfg.framesPerBlock == 0) return fail(Err::BadArgument);
    cfg_ = cfg;
    if (cfg_.blocksPerStep == 0) cfg_.blocksPerStep = 1;
    return ok();
}

Result<MediaFormat> NetSource::negotiate(u8 outPad, const MediaFormat& in) {
    HYDRA_UNUSED(outPad);
    HYDRA_UNUSED(in);
    if (!cfg_.format.valid()) return unexpected(Err::NotInitialized);
    return cfg_.format;
}

MemReq NetSource::memoryRequest(u8 outPad) const {
    HYDRA_UNUSED(outPad);
    MemReq req;
    req.blockSize = static_cast<u32>(cfg_.framesPerBlock) * cfg_.format.unitBytes();
    req.count = 3;
    return req;
}

Status NetSource::onPrepare(Pipeline& pipeline) {
    pipeline_ = &pipeline;
    pool_ = pipeline.poolFor(*this, 0);
    return pool_ != nullptr ? ok() : fail(Err::OutOfMemory);
}

/**
 * Składa nagłówek bajt po bajcie, szukając magii.
 *
 * Odbiornik, który wszedł w strumień w połowie bloku, czytałby długość ze
 * środka próbek i czekał na dwa gigabajty. Przesuwanie okna o jeden bajt do
 * skutku jest wolne, ale dzieje się wyłącznie po utracie synchronizacji —
 * w normalnym biegu pierwsze dwa bajty pasują od razu.
 */
bool NetSource::findHeader() {
    while (headerLen_ < kNetHeaderSize) {
        u8 byte;
        if (client_.read(ByteSpan{&byte, 1}) != 1) return false;

        header_[headerLen_++] = byte;

        // Sprawdzamy magię tak wcześnie, jak się da: dwa bajty wystarczą, żeby
        // stwierdzić, że jesteśmy nie tam, gdzie trzeba.
        if (headerLen_ >= 1 && header_[0] != kNetMagic0) {
            headerLen_ = 0;
            ++resyncs_;
            continue;
        }
        if (headerLen_ >= 2 && header_[1] != kNetMagic1) {
            // Drugi bajt nie pasuje, ale pierwszy mógł być początkiem
            // właściwego nagłówka — przesuwamy okno zamiast je zerować.
            header_[0] = header_[1];
            headerLen_ = (header_[0] == kNetMagic0) ? 1 : 0;
            ++resyncs_;
            continue;
        }
    }
    return true;
}

void NetSource::process(u64 nowUs) {
    HYDRA_UNUSED(nowUs);
    if (!client_.connected()) return;

    for (u8 i = 0; i < cfg_.blocksPerStep; ++i) {
        if (!findHeader()) return;

        u32 length = 0;
        u64 pts = 0;
        u8  flags = 0;
        if (!parseNetHeader(CByteSpan{header_, kNetHeaderSize}, length, pts, flags)) {
            headerLen_ = 0;
            ++resyncs_;
            continue;
        }

        Block block = pool_->acquire();
        if (!block.valid()) {
            // Nagłówek zostaje złożony — dokończymy w następnym kroku, gdy
            // będzie do czego wczytać ładunek.
            pipeline_->raise(MediaFault::PoolEmpty, *this, 0);
            return;
        }

        if (length > block.capacity) {
            // Nadawca ustawiony na większy blok. Odbieranie tego kawałkami
            // wymagałoby sklejania przez granice bloków, a różnica w rozmiarze
            // i tak oznacza rozjazd konfiguracji, który trzeba naprawić u źródła.
            HYDRA_LOGE("net-in: blok %lu B nie mieści się w buforze %lu B — "
                       "wyrównaj framesPerBlock po obu stronach",
                       static_cast<unsigned long>(length),
                       static_cast<unsigned long>(block.capacity));
            pool_->release(block);
            headerLen_ = 0;
            pipeline_->raise(MediaFault::TooLarge, *this, 0);
            return;
        }

        // Odczyt bez blokowania: gdy ładunek jeszcze nie dotarł w całości,
        // oddajemy blok i wracamy po niego w następnym kroku. Nagłówek zostaje
        // złożony, więc nic nie ginie.
        const size_t got = length > 0 ? client_.read(ByteSpan{block.data, length}) : 0;
        if (got != length) {
            pool_->release(block);
            return;
        }

        headerLen_ = 0;
        block.length = length;
        block.pts    = pts;
        block.flags  = flags;
        received_ += kNetHeaderSize + length;

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

#endif  // HYDRA_ENABLE_MEDIA && HYDRA_ENABLE_NET
