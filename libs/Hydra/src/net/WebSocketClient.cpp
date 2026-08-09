/** Hydra — implementacja klienta WebSocket. */

#include "hydra/net/WebSocketClient.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"

#include <stdio.h>
#include <string.h>

HYDRA_LOG_MODULE("ws")

namespace hydra {
namespace net {
namespace {

constexpr u8 kOpContinuation = 0x0;
constexpr u8 kOpText         = 0x1;
constexpr u8 kOpBinary       = 0x2;
constexpr u8 kOpClose        = 0x8;
constexpr u8 kOpPing         = 0x9;
constexpr u8 kOpPong         = 0xA;

constexpr char kBase64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** Base64 dla dokładnie 16 bajtów klucza — wychodzą 24 znaki z jednym „=". */
void base64Key(const u8 in[16], char out[25]) {
    size_t w = 0;
    for (size_t i = 0; i < 15; i += 3) {
        const u32 v = (static_cast<u32>(in[i]) << 16) |
                      (static_cast<u32>(in[i + 1]) << 8) | in[i + 2];
        out[w++] = kBase64[(v >> 18) & 0x3F];
        out[w++] = kBase64[(v >> 12) & 0x3F];
        out[w++] = kBase64[(v >> 6) & 0x3F];
        out[w++] = kBase64[v & 0x3F];
    }
    const u32 v = static_cast<u32>(in[15]) << 16;
    out[w++] = kBase64[(v >> 18) & 0x3F];
    out[w++] = kBase64[(v >> 12) & 0x3F];
    out[w++] = '=';
    out[w++] = '=';
    out[w] = 0;
}

}  // namespace

u32 WebSocketClient::nextMask() {
    // xorshift32 — trzy przesunięcia, żadnego stanu poza jednym słowem.
    maskSeed_ ^= maskSeed_ << 13;
    maskSeed_ ^= maskSeed_ >> 17;
    maskSeed_ ^= maskSeed_ << 5;
    return maskSeed_;
}

// ---------------------------------------------------------------------------
// Uzgodnienie połączenia
// ---------------------------------------------------------------------------

Status WebSocketClient::connect(const char* host, u16 port, u32 timeoutMs) {
    stop();
    HYDRA_CHECK(inner_.connect(host, port, timeoutMs));

    // Ziarno maski z zegara: nie musi być nieprzewidywalne, ma się różnić
    // między połączeniami. Stała wartość dawałaby ten sam klucz maskujący
    // po każdym restarcie, co jest dokładnie tym, przed czym maskowanie
    // ma chronić pośredniki.
    maskSeed_ ^= rtos::nowMs() | 1u;

    if (auto r = handshake(host, port); !r) {
        inner_.stop();
        return r;
    }
    state_ = State::Open;
    return ok();
}

Status WebSocketClient::handshake(const char* host, u16 port) {
    u8 keyBytes[16];
    for (size_t i = 0; i < sizeof(keyBytes); i += 4) {
        const u32 word = nextMask();
        keyBytes[i]     = static_cast<u8>(word);
        keyBytes[i + 1] = static_cast<u8>(word >> 8);
        keyBytes[i + 2] = static_cast<u8>(word >> 16);
        keyBytes[i + 3] = static_cast<u8>(word >> 24);
    }
    char key[25];
    base64Key(keyBytes, key);

    char request[384];
    int written = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n",
        cfg_.path, host, static_cast<unsigned>(port), key);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(request)) {
        return fail(Err::OutOfRange);
    }
    size_t at = static_cast<size_t>(written);

    auto append = [&](const char* fmt, const char* value) -> bool {
        const int n = snprintf(request + at, sizeof(request) - at, fmt, value);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(request) - at) return false;
        at += static_cast<size_t>(n);
        return true;
    };
    if (cfg_.subprotocol && !append("Sec-WebSocket-Protocol: %s\r\n", cfg_.subprotocol)) {
        return fail(Err::OutOfRange);
    }
    if (cfg_.origin && !append("Origin: %s\r\n", cfg_.origin)) return fail(Err::OutOfRange);
    if (at + 2 >= sizeof(request)) return fail(Err::OutOfRange);
    request[at++] = '\r';
    request[at++] = '\n';

    if (inner_.write(CByteSpan{reinterpret_cast<const u8*>(request), at}) != at) {
        return fail(Err::IoError);
    }

    // Odpowiedź czytamy do pustego wiersza. Nagłówki bywają rozbite na kilka
    // segmentów TCP, więc pojedynczy odczyt nie wystarcza.
    char     response[512];
    size_t   len = 0;
    const Millis deadline = rtos::nowMs() + cfg_.handshakeTimeoutMs;

    while (static_cast<i32>(rtos::nowMs() - deadline) < 0) {
        if (inner_.available() == 0) {
            // Bez oddania sterowania pętla zjada rdzeń na czas uzgodnienia,
            // a na jednordzeniowym układzie blokuje stos sieciowy, na którego
            // odpowiedź czeka.
            if (!inner_.connected()) return fail(Err::IoError);
            rtos::delayMs(1);
            continue;
        }

        u8 byte;
        if (inner_.read(ByteSpan{&byte, 1}) != 1) continue;
        if (len + 1 < sizeof(response)) response[len++] = static_cast<char>(byte);

        if (len >= 4 && response[len - 4] == '\r' && response[len - 3] == '\n' &&
            response[len - 2] == '\r' && response[len - 1] == '\n') {
            response[len] = 0;

            // „101 Switching Protocols" i nic innego. Kod 200 oznacza serwer,
            // który odpowiedział stroną zamiast uaktualnić połączenie — częsty
            // objaw pomylonej ścieżki albo portu.
            if (strncmp(response, "HTTP/1.1 101", 12) != 0) {
                HYDRA_LOGE("serwer odmówił uaktualnienia: %.32s", response);
                ++stats_.protocolErrors;
                return fail(Err::Protocol);
            }
            if (strstr(response, "Upgrade: websocket") == nullptr &&
                strstr(response, "Upgrade: WebSocket") == nullptr) {
                ++stats_.protocolErrors;
                return fail(Err::Protocol);
            }
            return ok();
        }
    }
    return fail(Err::Timeout);
}

void WebSocketClient::stop() {
    if (state_ == State::Open) {
        // Zamknięcie od strony klienta: bez niego broker trzyma sesję do
        // przekroczenia własnego czasu i przy szybkim restarcie urządzenia
        // odrzuca nowe połączenie o tym samym identyfikatorze klienta.
        (void)sendFrame(kOpClose, CByteSpan{});
    }
    state_ = State::Idle;
    rxLen_ = 0;
    rxRead_ = 0;
    inner_.stop();
}

bool WebSocketClient::connected() const {
    return state_ == State::Open && inner_.connected();
}

// ---------------------------------------------------------------------------
// Ramki
// ---------------------------------------------------------------------------

Status WebSocketClient::sendFrame(u8 opcode, CByteSpan payload) {
    u8 header[14];
    size_t at = 0;

    header[at++] = static_cast<u8>(0x80 | opcode);   // FIN + opcode

    const size_t n = payload.size();
    // Bit maski zawsze ustawiony: RFC 6455 nakazuje klientowi maskować każdą
    // ramkę, a brokery zrywają połączenie na niezamaskowanej.
    if (n < 126) {
        header[at++] = static_cast<u8>(0x80 | n);
    } else if (n <= 0xFFFF) {
        header[at++] = 0x80 | 126;
        header[at++] = static_cast<u8>(n >> 8);
        header[at++] = static_cast<u8>(n);
    } else {
        header[at++] = 0x80 | 127;
        for (int shift = 56; shift >= 0; shift -= 8) {
            header[at++] = static_cast<u8>(static_cast<u64>(n) >> shift);
        }
    }

    const u32 mask = nextMask();
    u8 maskBytes[4] = {static_cast<u8>(mask), static_cast<u8>(mask >> 8),
                       static_cast<u8>(mask >> 16), static_cast<u8>(mask >> 24)};
    for (u8 b : maskBytes) header[at++] = b;

    if (inner_.write(CByteSpan{header, at}) != at) return fail(Err::IoError);

    // Maskujemy porcjami, żeby nie potrzebować bufora wielkości ładunku —
    // publikacja MQTT bywa większa niż wszystko, co ten obiekt ma w polach.
    u8 chunk[64];
    size_t done = 0;
    while (done < n) {
        const size_t take = (n - done) < sizeof(chunk) ? (n - done) : sizeof(chunk);
        for (size_t i = 0; i < take; ++i) {
            chunk[i] = static_cast<u8>(payload.data()[done + i] ^ maskBytes[(done + i) & 3]);
        }
        if (inner_.write(CByteSpan{chunk, take}) != take) return fail(Err::IoError);
        done += take;
    }

    ++stats_.framesSent;
    return ok();
}

size_t WebSocketClient::write(CByteSpan data) {
    if (!connected()) return 0;
    // MQTT po WebSockecie idzie ramkami binarnymi — tekstowe wymagałyby
    // poprawnego UTF-8, a pakiet MQTT nim nie jest.
    return sendFrame(kOpBinary, data) ? data.size() : 0;
}

void WebSocketClient::compact() {
    if (rxRead_ == 0) return;
    if (rxRead_ >= rxLen_) { rxLen_ = 0; rxRead_ = 0; return; }
    memmove(rx_, rx_ + rxRead_, rxLen_ - rxRead_);
    rxLen_ -= rxRead_;
    rxRead_ = 0;
}

/**
 * Wciąga ramki z gniazda.
 *
 * Nagłówek czytamy bajt po bajcie, bo jego długość zależy od pierwszych
 * dwóch bajtów. Kosztuje to kilka wywołań na ramkę, ale pozwala obejść się
 * bez drugiego bufora i bez stanu „mam pół nagłówka" — a to jest ten rodzaj
 * stanu, w którym błędy siedzą latami.
 */
void WebSocketClient::pump() {
    compact();

    while (inner_.available() >= 2) {
        u8 head[2];
        if (inner_.read(ByteSpan{head, 2}) != 2) return;

        const u8 opcode = static_cast<u8>(head[0] & 0x0F);
        const bool masked = (head[1] & 0x80) != 0;
        u64 length = static_cast<u64>(head[1] & 0x7F);

        if (length == 126) {
            u8 ext[2];
            if (inner_.read(ByteSpan{ext, 2}) != 2) return;
            length = (static_cast<u64>(ext[0]) << 8) | ext[1];
        } else if (length == 127) {
            u8 ext[8];
            if (inner_.read(ByteSpan{ext, 8}) != 8) return;
            length = 0;
            for (u8 b : ext) length = (length << 8) | b;
        }

        u8 maskBytes[4] = {};
        // Serwer nie ma prawa maskować, ale bity trzeba odczytać, żeby nie
        // przesunąć się w strumieniu o cztery bajty.
        if (masked && inner_.read(ByteSpan{maskBytes, 4}) != 4) return;

        // Ramka większa niż bufor. Wyrzucamy jej treść i zgłaszamy — cichy
        // pominięty pakiet MQTT objawiłby się jako zawieszona subskrypcja.
        if (length > sizeof(rx_) - rxLen_) {
            ++stats_.overflows;
            HYDRA_LOGW("ramka %lu B nie mieści się w buforze %u B",
                       static_cast<unsigned long>(length),
                       static_cast<unsigned>(sizeof(rx_)));
            u8 sink[64];
            while (length > 0) {
                const size_t take = length < sizeof(sink) ? static_cast<size_t>(length)
                                                          : sizeof(sink);
                if (inner_.read(ByteSpan{sink, take}) != take) return;
                length -= take;
            }
            continue;
        }

        const size_t n = static_cast<size_t>(length);
        u8* dst = rx_ + rxLen_;
        if (n > 0 && inner_.read(ByteSpan{dst, n}) != n) return;
        if (masked) {
            for (size_t i = 0; i < n; ++i) dst[i] = static_cast<u8>(dst[i] ^ maskBytes[i & 3]);
        }
        ++stats_.framesReceived;

        switch (opcode) {
            case kOpBinary:
            case kOpText:
            case kOpContinuation:
                // Fragmenty składamy przez zwykłe dopisanie: dla warstwy wyżej
                // WebSocket jest strumieniem, więc granice ramek nie niosą
                // informacji, której MQTT by potrzebował.
                rxLen_ += n;
                break;

            case kOpPing:
                // Odpowiedź jest obowiązkowa — bez niej broker uzna połączenie
                // za martwe, mimo że ruch płynie.
                ++stats_.pings;
                (void)sendFrame(kOpPong, CByteSpan{dst, n});
                break;

            case kOpPong:
                break;

            case kOpClose:
                HYDRA_LOGI("serwer zamknął połączenie");
                state_ = State::Idle;
                inner_.stop();
                return;

            default:
                ++stats_.protocolErrors;
                break;
        }
    }
}

size_t WebSocketClient::available() {
    if (state_ != State::Open) return 0;
    pump();
    return rxLen_ - rxRead_;
}

size_t WebSocketClient::read(ByteSpan out) {
    if (state_ != State::Open || out.size() == 0) return 0;
    pump();

    const size_t ready = rxLen_ - rxRead_;
    const size_t take = ready < out.size() ? ready : out.size();
    if (take == 0) return 0;

    memcpy(out.data(), rx_ + rxRead_, take);
    rxRead_ += take;
    if (rxRead_ >= rxLen_) { rxLen_ = 0; rxRead_ = 0; }
    return take;
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
