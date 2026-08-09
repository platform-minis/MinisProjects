/** Hydra — implementacja ramkowania szeregowego. */

#include "hydra/minis/SerialCodec.hpp"

#if HYDRA_ENABLE_MINIS

#include <string.h>

namespace hydra {
namespace minis {

// ---------------------------------------------------------------------------
// CRC
// ---------------------------------------------------------------------------

u16 crc16(CByteSpan data, u16 seed) {
    u16 crc = seed;
    // Wariant bitowy, nie tablicowy: tablica to 512 bajtów flashu na układzie,
    // który ma ich 264 kB, a ramki są krótkie i liczone raz. Gdyby kiedyś
    // magistrala poszła na 1 Mbaud, warto to zamienić na tablicę nibblową.
    for (size_t i = 0; i < data.size(); ++i) {
        crc = static_cast<u16>(crc ^ (static_cast<u16>(data.data()[i]) << 8));
        for (u8 bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? static_cast<u16>((crc << 1) ^ 0x1021u)
                                  : static_cast<u16>(crc << 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// COBS
// ---------------------------------------------------------------------------

Result<size_t> cobsEncode(CByteSpan in, ByteSpan out) {
    if (out.size() < in.size() + in.size() / 254 + 1) return unexpected(Err::OutOfRange);

    const u8* src = in.data();
    u8*       dst = out.data();

    size_t codeAt = 0;   // miejsce na licznik bieżącego bloku
    size_t w      = 1;   // pierwszy bajt to licznik, wypełniany na końcu bloku
    u8     code   = 1;

    for (size_t i = 0; i < in.size(); ++i) {
        if (src[i] != 0) {
            dst[w++] = src[i];
            ++code;
            if (code != 0xFF) continue;
        }
        // Blok zamyka albo zero w danych, albo 254 niezerowe bajty.
        dst[codeAt] = code;
        codeAt = w++;
        code = 1;
    }
    dst[codeAt] = code;
    return w;
}

Result<size_t> cobsDecode(CByteSpan in, ByteSpan out) {
    const u8* src = in.data();
    size_t    r   = 0;
    size_t    w   = 0;

    while (r < in.size()) {
        const u8 code = src[r++];
        // Zero w strumieniu zakodowanym jest niemożliwe — jego obecność
        // oznacza, że trafiliśmy w środek ramki albo w śmieci na magistrali.
        if (code == 0) return unexpected(Err::Protocol);

        for (u8 i = 1; i < code; ++i) {
            if (r >= in.size()) return unexpected(Err::Protocol);
            if (w >= out.size()) return unexpected(Err::OutOfRange);
            out.data()[w++] = src[r++];
        }
        // Blok krótszy niż 255 kończy się zerem — poza ostatnim blokiem ramki.
        if (code != 0xFF && r < in.size()) {
            if (w >= out.size()) return unexpected(Err::OutOfRange);
            out.data()[w++] = 0;
        }
    }
    return w;
}

// ---------------------------------------------------------------------------
// Ramka
// ---------------------------------------------------------------------------

namespace {

/** Dopisuje bajt; `false` przy przepełnieniu. */
bool push(ByteSpan buf, size_t& at, u8 value) {
    if (at >= buf.size()) return false;
    buf.data()[at++] = value;
    return true;
}

bool pushBytes(ByteSpan buf, size_t& at, const void* data, size_t n) {
    if (at + n > buf.size()) return false;
    memcpy(buf.data() + at, data, n);
    at += n;
    return true;
}

/** Napis poprzedzony długością — format wszystkich pól zmiennych w ramce. */
bool pushString(ByteSpan buf, size_t& at, const char* text, size_t limit) {
    size_t n = 0;
    while (n < limit && text[n] != '\0') ++n;
    return push(buf, at, static_cast<u8>(n)) && pushBytes(buf, at, text, n);
}

bool takeString(CByteSpan buf, size_t& at, char* out, size_t capacity) {
    if (at >= buf.size()) return false;
    const size_t n = buf.data()[at++];
    if (at + n > buf.size() || n + 1 > capacity) return false;
    memcpy(out, buf.data() + at, n);
    out[n] = 0;
    at += n;
    return true;
}

}  // namespace

Result<size_t> encodeSerial(const SerialFrame& frame, ByteSpan out) {
    // Ramka jest najpierw składana w buforze roboczym, a dopiero potem
    // kodowana. Kodowanie w locie wymagałoby cofania się po licznik bloku
    // COBS, czyli tego samego bufora i dwóch dodatkowych stanów.
    u8       body[serialWorstCase(HYDRA_MINIS_SERIAL_MTU)];
    ByteSpan work{body, sizeof(body)};
    size_t   at = 0;

    const u8 flags = frame.flags;

    if (!push(work, at, static_cast<u8>((kSerialVersion << 4) | (flags & 0x0F)))) {
        return unexpected(Err::OutOfRange);
    }
    if (!push(work, at, static_cast<u8>(frame.kind))) return unexpected(Err::OutOfRange);
    if (!push(work, at, frame.src))  return unexpected(Err::OutOfRange);
    if (!push(work, at, frame.dst))  return unexpected(Err::OutOfRange);
    if (!push(work, at, frame.hops)) return unexpected(Err::OutOfRange);

    const u16 len = static_cast<u16>(frame.payload.size());
    if (!push(work, at, static_cast<u8>(len & 0xFF)))  return unexpected(Err::OutOfRange);
    if (!push(work, at, static_cast<u8>(len >> 8)))    return unexpected(Err::OutOfRange);

    if (flags & kFlagIdent) {
        if (!pushString(work, at, frame.addr.user, kUserMax - 1))     return unexpected(Err::OutOfRange);
        if (!pushString(work, at, frame.addr.device, kDeviceMax - 1)) return unexpected(Err::OutOfRange);
    }
    if (flags & kFlagExt) {
        if (!pushString(work, at, frame.extType, kExtTypeMax - 1)) return unexpected(Err::OutOfRange);
    }
    if (!pushBytes(work, at, frame.payload.data(), frame.payload.size())) {
        return unexpected(Err::OutOfRange);
    }

    const u16 crc = crc16(CByteSpan{body, at});
    if (!push(work, at, static_cast<u8>(crc & 0xFF))) return unexpected(Err::OutOfRange);
    if (!push(work, at, static_cast<u8>(crc >> 8)))   return unexpected(Err::OutOfRange);

    HYDRA_TRY(size_t encoded, cobsEncode(CByteSpan{body, at}, out));
    if (encoded >= out.size()) return unexpected(Err::OutOfRange);
    out.data()[encoded++] = 0;   // granica ramki
    return encoded;
}

Status decodeSerial(ByteSpan raw, SerialFrame& out) {
    out = SerialFrame{};

    // Dekodowanie w miejscu jest bezpieczne: COBS czyta zawsze przed pozycją
    // zapisu, bo postać zakodowana jest dłuższa od rozkodowanej.
    HYDRA_TRY(size_t n, cobsDecode(CByteSpan{raw.data(), raw.size()}, raw));
    if (n < kSerialHeaderSize + kSerialCrcSize) return fail(Err::Protocol);

    const CByteSpan body{raw.data(), n - kSerialCrcSize};
    const u16 expected = static_cast<u16>(raw.data()[n - 2] | (raw.data()[n - 1] << 8));
    if (crc16(body) != expected) return fail(Err::Protocol);

    size_t at = 0;
    const u8 head = raw.data()[at++];
    if ((head >> 4) != kSerialVersion) return fail(Err::NotSupported);
    out.flags = static_cast<u8>(head & 0x0F);

    out.kind = static_cast<MsgKind>(raw.data()[at++]);
    out.src  = raw.data()[at++];
    out.dst  = raw.data()[at++];
    out.hops = raw.data()[at++];

    const u16 len = static_cast<u16>(raw.data()[at] | (raw.data()[at + 1] << 8));
    at += 2;

    if (out.flags & kFlagIdent) {
        if (!takeString(body, at, out.addr.user, kUserMax))     return fail(Err::Protocol);
        if (!takeString(body, at, out.addr.device, kDeviceMax)) return fail(Err::Protocol);
    }
    if (out.flags & kFlagExt) {
        if (!takeString(body, at, out.extType, kExtTypeMax)) return fail(Err::Protocol);
    }

    // Deklarowana długość musi się zgadzać z tym, co zostało. Ramka, w której
    // się nie zgadza, przeszła CRC — czyli ktoś nadaje innym formatem, a nie
    // kabel przekłamał bit.
    if (at + len != body.size()) return fail(Err::Protocol);

    out.payload = CByteSpan{raw.data() + at, len};
    return ok();
}

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
