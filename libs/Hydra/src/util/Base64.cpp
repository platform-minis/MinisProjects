/**
 * Hydra — base64 (RFC 4648).
 *
 * Tablica dekodująca zamiast łańcucha porównań: 256 bajtów stałych w pamięci
 * programu przeciwko czterem gałęziom na znak. Przy ćwierćmegabajtowym module
 * to różnica rzędu miliona instrukcji.
 */

#include "hydra/util/Base64.hpp"

namespace hydra {
namespace util {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr u8 kInvalid = 0xFF;

/** Odwrotność alfabetu. 0xFF oznacza znak, którego w base64 nie ma. */
u8 decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<u8>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<u8>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return kInvalid;
}

}  // namespace

Result<size_t> base64Encode(CByteSpan data, char* out, size_t capacity) {
    if (out == nullptr) return unexpected(Err::BadArgument);

    const size_t needed = base64EncodedSize(data.size());
    if (capacity < needed + 1) return unexpected(Err::OutOfRange);

    const u8*    in = data.data();
    const size_t n  = data.size();
    size_t       w  = 0;

    for (size_t i = 0; i < n; i += 3) {
        const u32 b0 = in[i];
        const u32 b1 = (i + 1 < n) ? in[i + 1] : 0;
        const u32 b2 = (i + 2 < n) ? in[i + 2] : 0;
        const u32 triple = (b0 << 16) | (b1 << 8) | b2;

        out[w++] = kAlphabet[(triple >> 18) & 0x3F];
        out[w++] = kAlphabet[(triple >> 12) & 0x3F];
        out[w++] = (i + 1 < n) ? kAlphabet[(triple >> 6) & 0x3F] : '=';
        out[w++] = (i + 2 < n) ? kAlphabet[triple & 0x3F] : '=';
    }

    out[w] = '\0';
    return w;
}

Result<size_t> base64Decode(const char* text, size_t chars, ByteSpan out) {
    if (text == nullptr) return unexpected(Err::BadArgument);
    if (chars == 0) return static_cast<size_t>(0);
    // Bez dopełnienia do czterech znaków nie da się odtworzyć długości —
    // a zgadywanie oznaczałoby ciche gubienie ostatniego bajtu.
    if ((chars % 4) != 0) return unexpected(Err::BadArgument);

    size_t padding = 0;
    if (text[chars - 1] == '=') ++padding;
    if (padding == 1 && chars >= 2 && text[chars - 2] == '=') ++padding;

    const size_t produced = (chars / 4) * 3 - padding;
    if (out.size() < produced) return unexpected(Err::OutOfRange);

    u8*    dst = out.data();
    size_t w   = 0;

    for (size_t i = 0; i < chars; i += 4) {
        u32 group = 0;
        for (size_t k = 0; k < 4; ++k) {
            const char c = text[i + k];

            if (c == '=') {
                // Dopełnienie wolno postawić wyłącznie na dwóch ostatnich
                // miejscach ostatniej czwórki. W środku oznacza sklejone
                // ze sobą dwa dokumenty, a nie jeden obraz.
                const bool lastGroup = (i + 4 == chars);
                const bool tailSlot  = (k >= 2);
                if (!lastGroup || !tailSlot) return unexpected(Err::BadArgument);
                group <<= 6;
                continue;
            }

            const u8 v = decodeChar(c);
            if (v == kInvalid) return unexpected(Err::BadArgument);
            group = (group << 6) | v;
        }

        if (w < produced) dst[w++] = static_cast<u8>((group >> 16) & 0xFF);
        if (w < produced) dst[w++] = static_cast<u8>((group >> 8) & 0xFF);
        if (w < produced) dst[w++] = static_cast<u8>(group & 0xFF);
    }

    return produced;
}

}  // namespace util
}  // namespace hydra
