/** Hydra — implementacja SHA-256 i HMAC-SHA256 (FIPS 180-4, RFC 2104). */

#include "hydra/util/Sha256.hpp"

#include <string.h>

namespace hydra {
namespace util {
namespace {

/** Stałe rundowe: części ułamkowe pierwiastków sześciennych 64 liczb pierwszych. */
constexpr u32 kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr u32 rotr(u32 value, u32 bits) { return (value >> bits) | (value << (32 - bits)); }

}  // namespace

void Sha256::reset() {
    // Wartości początkowe: części ułamkowe pierwiastków kwadratowych
    // ośmiu pierwszych liczb pierwszych.
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;

    bufferLength_ = 0;
    totalBits_    = 0;
    memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::processBlock(const u8* block) {
    u32 w[64];

    for (u32 i = 0; i < 16; ++i) {
        w[i] = (static_cast<u32>(block[i * 4]) << 24) |
               (static_cast<u32>(block[i * 4 + 1]) << 16) |
               (static_cast<u32>(block[i * 4 + 2]) << 8) |
               static_cast<u32>(block[i * 4 + 3]);
    }
    for (u32 i = 16; i < 64; ++i) {
        const u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u32 a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    u32 e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (u32 i = 0; i < 64; ++i) {
        const u32 s1    = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const u32 ch    = (e & f) ^ (~e & g);
        const u32 temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const u32 s0    = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const u32 maj   = (a & b) ^ (a & c) ^ (b & c);
        const u32 temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(CByteSpan data) {
    if (data.empty()) return;
    totalBits_ += static_cast<u64>(data.size()) * 8;

    size_t offset = 0;

    // Domknięcie bloku rozpoczętego poprzednim wywołaniem — dzięki temu
    // obraz można podawać fragmentami dowolnej długości, tak jak przychodzą
    // z gniazda sieciowego.
    if (bufferLength_ > 0) {
        const size_t needed = kSha256Block - bufferLength_;
        const size_t take   = data.size() < needed ? data.size() : needed;
        memcpy(buffer_ + bufferLength_, data.data(), take);
        bufferLength_ += take;
        offset += take;

        if (bufferLength_ < kSha256Block) return;
        processBlock(buffer_);
        bufferLength_ = 0;
    }

    while (offset + kSha256Block <= data.size()) {
        processBlock(data.data() + offset);
        offset += kSha256Block;
    }

    const size_t rest = data.size() - offset;
    if (rest > 0) {
        memcpy(buffer_, data.data() + offset, rest);
        bufferLength_ = rest;
    }
}

void Sha256::finish(u8 out[kSha256Size]) {
    const u64 bits = totalBits_;

    // Dopełnienie: bajt 0x80, zera, na końcu długość w bitach jako 64 bity
    // big-endian.
    const u8 one = 0x80;
    update(CByteSpan{&one, 1});
    totalBits_ = bits;  // dopełnienie nie liczy się do długości

    const u8 zero = 0x00;
    while (bufferLength_ != kSha256Block - 8) {
        update(CByteSpan{&zero, 1});
        totalBits_ = bits;
    }

    for (int i = 7; i >= 0; --i) {
        buffer_[kSha256Block - 8 + (7 - i)] = static_cast<u8>((bits >> (i * 8)) & 0xFF);
    }
    processBlock(buffer_);
    bufferLength_ = 0;

    for (u32 i = 0; i < 8; ++i) {
        out[i * 4]     = static_cast<u8>((state_[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<u8>((state_[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<u8>((state_[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<u8>(state_[i] & 0xFF);
    }
}

void Sha256::hash(CByteSpan data, u8 out[kSha256Size]) {
    Sha256 sha;
    sha.update(data);
    sha.finish(out);
}

void Sha256::toHex(const u8 digest[kSha256Size], char* out, size_t capacity) {
    if (!out || capacity < kSha256Size * 2 + 1) {
        if (out && capacity > 0) out[0] = '\0';
        return;
    }
    static const char kDigits[] = "0123456789abcdef";
    for (size_t i = 0; i < kSha256Size; ++i) {
        out[i * 2]     = kDigits[(digest[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kDigits[digest[i] & 0x0F];
    }
    out[kSha256Size * 2] = '\0';
}

bool Sha256::fromHex(const char* text, u8 out[kSha256Size]) {
    if (!text) return false;

    for (size_t i = 0; i < kSha256Size; ++i) {
        u8 value = 0;
        for (int nibble = 0; nibble < 2; ++nibble) {
            const char c = text[i * 2 + nibble];
            u8         digit;
            if (c >= '0' && c <= '9')      digit = static_cast<u8>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<u8>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<u8>(c - 'A' + 10);
            else return false;
            value = static_cast<u8>((value << 4) | digit);
        }
        out[i] = value;
    }
    return text[kSha256Size * 2] == '\0';
}

bool Sha256::equal(const u8 a[kSha256Size], const u8 b[kSha256Size]) {
    // Suma różnic zamiast wcześniejszego wyjścia: czas wykonania nie zależy
    // od tego, ile początkowych bajtów się zgadza.
    u8 difference = 0;
    for (size_t i = 0; i < kSha256Size; ++i) difference |= static_cast<u8>(a[i] ^ b[i]);
    return difference == 0;
}

// ---------------------------------------------------------------------------
// HMAC
// ---------------------------------------------------------------------------

void HmacSha256::begin(CByteSpan key) {
    u8 block[kSha256Block] = {};

    // Klucz dłuższy niż blok zastępuje się własnym skrótem — tak wymaga RFC.
    if (key.size() > kSha256Block) {
        u8 digest[kSha256Size];
        Sha256::hash(key, digest);
        memcpy(block, digest, kSha256Size);
    } else if (!key.empty()) {
        memcpy(block, key.data(), key.size());
    }

    u8 innerKey[kSha256Block];
    for (size_t i = 0; i < kSha256Block; ++i) {
        innerKey[i]    = static_cast<u8>(block[i] ^ 0x36);
        outerKey_[i]   = static_cast<u8>(block[i] ^ 0x5C);
    }

    inner_.reset();
    inner_.update(CByteSpan{innerKey, kSha256Block});
}

void HmacSha256::update(CByteSpan data) { inner_.update(data); }

void HmacSha256::finish(u8 out[kSha256Size]) {
    u8 innerDigest[kSha256Size];
    inner_.finish(innerDigest);

    Sha256 outer;
    outer.update(CByteSpan{outerKey_, kSha256Block});
    outer.update(CByteSpan{innerDigest, kSha256Size});
    outer.finish(out);
}

void HmacSha256::compute(CByteSpan key, CByteSpan data, u8 out[kSha256Size]) {
    HmacSha256 hmac;
    hmac.begin(key);
    hmac.update(data);
    hmac.finish(out);
}

}  // namespace util
}  // namespace hydra
