#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/ProgramReceiver.hpp"

#include <string.h>

namespace hydra {
namespace script {

u16 crc16Ccitt(CByteSpan data) {
    u16 crc = 0xFFFF;
    for (size_t i = 0; i < data.size(); ++i) {
        crc = static_cast<u16>(crc ^ (static_cast<u16>(data.data()[i]) << 8));
        for (u8 bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<u16>((crc << 1) ^ 0x1021)
                                 : static_cast<u16>(crc << 1);
        }
    }
    return crc;
}

Status ProgramReceiver::begin(ByteSpan buffer) {
    if (buffer.empty()) return fail(Err::BadArgument);
    buffer_ = buffer;
    reset();
    return ok();
}

void ProgramReceiver::reset() {
    expected_   = 0;
    crc_        = 0;
    received_   = 0;
    rangeCount_ = 0;
}

Status ProgramReceiver::expect(u32 totalBytes, u16 crc16) {
    if (buffer_.empty()) return fail(Err::NotInitialized);
    if (totalBytes == 0) return fail(Err::BadArgument);

    if (totalBytes > buffer_.size()) {
        // Program większy niż bufor projektu. Odmowa tutaj jest jedynym
        // sensownym momentem: przyjęcie kawałków i zorientowanie się przy
        // ostatnim marnuje cały transfer.
        return fail(Err::OutOfRange);
    }

    // Ponowienie po zerwaniu połączenia: te same parametry to kontynuacja,
    // inne — nowy transfer. Rozróżnienie po zawartości, nie po znaczniku,
    // bo nadawca po restarcie nie wie, co zdążyło dojść.
    if (expected_ == totalBytes && crc_ == crc16) return ok();

    reset();
    expected_ = totalBytes;
    crc_      = crc16;
    return ok();
}

/**
 * Odnotowuje przyjęty zakres.
 *
 * Pokrycie trzymamy jako listę przedziałów, nie mapę bitową: kawałki
 * przychodzą prawie zawsze po kolei, więc lista ma zwykle **jeden** wpis
 * i rośnie tylko przy zmianie kolejności w sieci. Mapa bitowa dla programu
 * 32 KB przy kawałkach po 256 B to 16 bajtów — niewiele, ale wymaga znajomości
 * rozmiaru kawałka z góry, a ten bywa różny w tym samym transferze.
 *
 * Przy wyczerpaniu listy przedziałów odmawiamy zamiast scalać na siłę:
 * transfer tak podziurawiony i tak trzeba powtórzyć.
 */
Status ProgramReceiver::chunk(u32 offset, CByteSpan data) {
    if (buffer_.empty()) return fail(Err::NotInitialized);
    if (expected_ == 0) {
        // Kawałek bez zapowiedzi — nie wiadomo, do czego należy.
        ++stats_.rejected;
        return fail(Err::NotInitialized);
    }
    if (data.empty()) return ok();

    const u64 end = static_cast<u64>(offset) + static_cast<u64>(data.size());
    if (end > static_cast<u64>(expected_)) {
        ++stats_.rejected;
        return fail(Err::OutOfRange);
    }

    memcpy(buffer_.data() + offset, data.data(), data.size());
    ++stats_.chunks;

    const u32 from = offset;
    const u32 to   = static_cast<u32>(end);

    // Doklejenie do istniejącego przedziału albo nowy wpis.
    for (u8 i = 0; i < rangeCount_; ++i) {
        Range& r = covered_[i];
        if (from >= r.from && to <= r.to) {
            ++stats_.duplicates;
            return ok();
        }
        if (from <= r.to && to >= r.from) {
            const u32 before = r.to - r.from;
            r.from = from < r.from ? from : r.from;
            r.to   = to > r.to ? to : r.to;
            received_ += (r.to - r.from) - before;

            // Sąsiedni przedział mógł się właśnie zetknąć z rozszerzonym.
            for (u8 j = 0; j < rangeCount_;) {
                if (j != i && covered_[j].from <= r.to && covered_[j].to >= r.from) {
                    const u32 grown = r.to - r.from;
                    r.from = covered_[j].from < r.from ? covered_[j].from : r.from;
                    r.to   = covered_[j].to > r.to ? covered_[j].to : r.to;
                    received_ += (r.to - r.from) - grown - (covered_[j].to - covered_[j].from);
                    covered_[j] = covered_[--rangeCount_];
                    if (j < i) --i;
                    continue;
                }
                ++j;
            }
            return ok();
        }
    }

    if (rangeCount_ >= kMaxRanges) {
        ++stats_.rejected;
        return fail(Err::OutOfMemory);
    }

    covered_[rangeCount_++] = Range{from, to};
    received_ += to - from;
    return ok();
}

Status ProgramReceiver::feed(CByteSpan frame) {
    if (frame.size() < kProgramFrameHeader) {
        ++stats_.rejected;
        return fail(Err::Protocol);
    }

    const u8* p = frame.data();
    if (p[0] != 'H' || p[1] != 'W' || p[2] != 'A' || p[3] != 'M') {
        // Nie nasza wiadomość. Temat MQTT bywa dzielony, a cudzy ładunek
        // wzięty za program dałby błąd sumy kontrolnej zamiast odrzucenia.
        ++stats_.rejected;
        return fail(Err::Protocol);
    }

    const u32 total = (static_cast<u32>(p[4]) << 24) | (static_cast<u32>(p[5]) << 16) |
                      (static_cast<u32>(p[6]) << 8) | static_cast<u32>(p[7]);
    const u16 crc    = static_cast<u16>((static_cast<u16>(p[8]) << 8) | p[9]);
    const u16 offset = static_cast<u16>((static_cast<u16>(p[10]) << 8) | p[11]);

    if (total == 0 || total > kProgramMaxBytes) {
        ++stats_.rejected;
        return fail(Err::OutOfRange);
    }

    HYDRA_CHECK(expect(total, crc));
    return chunk(offset, CByteSpan{p + kProgramFrameHeader,
                                   frame.size() - kProgramFrameHeader});
}

u16 ProgramReceiver::checksum() const {
    if (expected_ == 0) return 0;
    return crc16Ccitt(CByteSpan{buffer_.data(), expected_});
}

bool ProgramReceiver::complete() const {
    if (expected_ == 0 || received_ != expected_) return false;

    // Suma kontrolna liczona dopiero po skompletowaniu bajtów, nie przy każdym
    // kawałku: przejście po całym obrazie kosztuje, a wynik przed końcem
    // transferu i tak niczego nie znaczy.
    return checksum() == crc_;
}

CByteSpan ProgramReceiver::image() const {
    if (!complete()) return CByteSpan{};
    return CByteSpan{buffer_.data(), expected_};
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
