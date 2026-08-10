#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/net/HttpClient.hpp"

#include <stdio.h>
#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"

HYDRA_LOG_MODULE("net.http")

namespace hydra {
namespace net {

namespace {

/** Ile czasu zostało do terminu; 0 oznacza „już po". */
u32 remaining(u32 deadlineMs) {
    const i32 left = static_cast<i32>(deadlineMs - static_cast<u32>(rtos::nowMs()));
    return left > 0 ? static_cast<u32>(left) : 0;
}

char toLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (toLower(*a++) != toLower(*b++)) return false;
    }
    return *a == *b;
}

/** Zapisuje tekst do gniazda. Zwraca błąd przy zapisie częściowym. */
Status writeAll(IClient& client, const char* text) {
    const size_t length = strlen(text);
    if (length == 0) return ok();
    const size_t written = client.write(CByteSpan{reinterpret_cast<const u8*>(text), length});
    return written == length ? ok() : fail(Err::IoError);
}

/** Parsuje liczbę dziesiętną; zatrzymuje się na pierwszym nie-cyfrze. */
i32 parseDecimal(const char* text) {
    i32 value = 0;
    while (*text == ' ') ++text;
    if (*text < '0' || *text > '9') return -1;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
    }
    return value;
}

/** Parsuje liczbę szesnastkową — długość kawałka w kodowaniu chunked. */
i32 parseHex(const char* text) {
    i32 value = 0;
    bool any = false;
    while (*text != '\0') {
        const char c = toLower(*text);
        i32 digit;
        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else break;
        value = value * 16 + digit;
        any = true;
        ++text;
    }
    return any ? value : -1;
}

}  // namespace

const char* toString(HttpClient::Method method) {
    switch (method) {
        case HttpClient::Method::Get:    return "GET";
        case HttpClient::Method::Head:   return "HEAD";
        case HttpClient::Method::Post:   return "POST";
        case HttpClient::Method::Put:    return "PUT";
        case HttpClient::Method::Patch:  return "PATCH";
        case HttpClient::Method::Delete: return "DELETE";
    }
    return "GET";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Adresy
// ═══════════════════════════════════════════════════════════════════════════

Result<Url> parseUrl(const char* url, ByteSpan scratch) {
    if (url == nullptr || scratch.size() < 8) return unexpected(Err::BadArgument);

    Url result;
    const char* cursor = url;

    if (strncmp(cursor, "https://", 8) == 0) {
        result.secure = true;
        result.port   = 443;
        cursor += 8;
    } else if (strncmp(cursor, "http://", 7) == 0) {
        result.port = 80;
        cursor += 7;
    } else {
        // Brak schematu to nie „domyślnie http". Adres bez schematu bywa
        // ścieżką względną i zgadywanie skończyłoby się połączeniem
        // z przypadkowym hostem.
        return unexpected(Err::BadArgument);
    }

    // Kopiujemy do bufora, bo host i ścieżka muszą być osobnymi łańcuchami
    // zakończonymi zerem, a w adresie stoją sklejone.
    char* out = reinterpret_cast<char*>(scratch.data());
    const size_t capacity = scratch.size();

    size_t used = 0;
    result.host = out;

    // Host trwa do '/', ':' albo końca.
    while (*cursor != '\0' && *cursor != '/' && *cursor != ':') {
        if (used + 2 >= capacity) return unexpected(Err::OutOfRange);
        out[used++] = *cursor++;
    }
    if (used == 0) return unexpected(Err::BadArgument);
    out[used++] = '\0';

    if (*cursor == ':') {
        ++cursor;
        const i32 port = parseDecimal(cursor);
        if (port <= 0 || port > 65535) return unexpected(Err::BadArgument);
        result.port = static_cast<u16>(port);
        while (*cursor >= '0' && *cursor <= '9') ++cursor;
    }

    result.path = out + used;
    if (*cursor != '/') {
        // Pusta ścieżka to "/" — serwer odpowiadający na "GET " bez ścieżki
        // zwróciłby 400.
        if (used + 2 >= capacity) return unexpected(Err::OutOfRange);
        out[used++] = '/';
    }
    while (*cursor != '\0') {
        if (used + 2 >= capacity) return unexpected(Err::OutOfRange);
        out[used++] = *cursor++;
    }
    out[used++] = '\0';

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Cykl życia
// ═══════════════════════════════════════════════════════════════════════════

Status HttpClient::begin(IClient& transport, ByteSpan scratch) {
    // 256 bajtów to dolna granica sensu: sam nagłówek Location bywa dłuższy,
    // a obcięty wiersz nagłówka daje błąd parsowania w miejscu, które niczego
    // nie tłumaczy.
    if (scratch.size() < 256) return fail(Err::BadArgument);

    transport_     = &transport;
    scratch_       = scratch;
    connectedHost_ = nullptr;
    connectedPort_ = 0;
    return ok();
}

void HttpClient::end() {
    if (transport_ != nullptr) transport_->stop();
    connectedHost_ = nullptr;
    connectedPort_ = 0;
}

bool HttpClient::connected() const {
    return transport_ != nullptr && transport_->connected();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Żądanie
// ═══════════════════════════════════════════════════════════════════════════

Status HttpClient::sendRequest(const Request& request) {
    IClient& client = *transport_;
    char line[320];

    // Wiersz żądania. Ścieżka bezwzględna i nagłówek Host — bez niego serwer
    // z wirtualnymi hostami nie wie, o co go pytamy, i odpowiada 400.
    snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\n", toString(request.method), request.path);
    HYDRA_CHECK(writeAll(client, line));

    if (request.port == 80 || request.port == 443) {
        snprintf(line, sizeof(line), "Host: %s\r\n", request.host);
    } else {
        snprintf(line, sizeof(line), "Host: %s:%u\r\n", request.host,
                 static_cast<unsigned>(request.port));
    }
    HYDRA_CHECK(writeAll(client, line));

    HYDRA_CHECK(writeAll(client, request.keepAlive ? "Connection: keep-alive\r\n"
                                                   : "Connection: close\r\n"));

    // Bez tego nagłówka serwer ma prawo przysłać treść spakowaną, a rozpakować
    // jej nie mamy czym — patrz uwaga w nagłówku klasy.
    HYDRA_CHECK(writeAll(client, "Accept-Encoding: identity\r\n"));

    for (const Header& header : request.headers) {
        if (header.name == nullptr || header.value == nullptr) continue;
        snprintf(line, sizeof(line), "%s: %s\r\n", header.name, header.value);
        HYDRA_CHECK(writeAll(client, line));
    }

    if (!request.body.empty()) {
        snprintf(line, sizeof(line), "Content-Type: %s\r\nContent-Length: %lu\r\n",
                 request.contentType != nullptr ? request.contentType
                                                : "application/octet-stream",
                 static_cast<unsigned long>(request.body.size()));
        HYDRA_CHECK(writeAll(client, line));
    }

    HYDRA_CHECK(writeAll(client, "\r\n"));

    if (!request.body.empty()) {
        const size_t written = client.write(request.body);
        if (written != request.body.size()) return fail(Err::IoError);
    }
    return ok();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Odpowiedź
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Wczytuje wiersz zakończony CRLF.
 *
 * Bajt po bajcie, i to jest świadome: nagłówków nie da się czytać porcjami
 * bez bufora, do którego wpadłby początek treści, a wtedy trzeba by go potem
 * oddać z powrotem strumieniowi. Wiersz nagłówka ma kilkadziesiąt bajtów,
 * a treść czytamy już blokami.
 */
Result<size_t> HttpClient::readLine(u32 deadlineMs) {
    char* out = reinterpret_cast<char*>(scratch_.data());
    size_t length = 0;
    bool sawCr = false;

    for (;;) {
        u8 byte = 0;
        const size_t got = transport_->read(ByteSpan{&byte, 1});

        if (got == 0) {
            if (!transport_->connected()) return unexpected(Err::IoError);
            if (remaining(deadlineMs) == 0) return unexpected(Err::Timeout);
            rtos::delayMs(1);
            continue;
        }

        if (byte == '\n' && sawCr) {
            out[length] = '\0';
            return length;
        }

        if (sawCr) {
            // CR bez LF — nagłówek niezgodny z protokołem. Wstawiamy go do
            // treści wiersza zamiast udawać, że go nie było.
            if (length + 2 >= scratch_.size()) return unexpected(Err::OutOfRange);
            out[length++] = '\r';
            sawCr = false;
        }

        if (byte == '\r') {
            sawCr = true;
            continue;
        }

        if (length + 2 >= scratch_.size()) return unexpected(Err::OutOfRange);
        out[length++] = static_cast<char>(byte);
    }
}

Status HttpClient::readSized(size_t length, BodyFn onBody, u32 deadlineMs, size_t* total) {
    u8 buffer[512];

    while (*total < length) {
        const size_t want = length - *total;
        const size_t take = want < sizeof(buffer) ? want : sizeof(buffer);
        const size_t got  = transport_->read(ByteSpan{buffer, take});

        if (got == 0) {
            if (!transport_->connected()) return fail(Err::IoError);
            if (remaining(deadlineMs) == 0) return fail(Err::Timeout);
            rtos::delayMs(1);
            continue;
        }

        *total += got;
        if (onBody) onBody(CByteSpan{buffer, got});
    }
    return ok();
}

Status HttpClient::readUntilClose(BodyFn onBody, u32 deadlineMs, size_t* total) {
    u8 buffer[512];

    for (;;) {
        const size_t got = transport_->read(ByteSpan{buffer, sizeof(buffer)});
        if (got > 0) {
            *total += got;
            if (onBody) onBody(CByteSpan{buffer, got});
            continue;
        }
        // Koniec treści sygnalizuje tu zamknięcie połączenia przez serwer —
        // jedyny sposób, jaki ma HTTP/1.0 bez Content-Length.
        if (!transport_->connected()) return ok();
        if (remaining(deadlineMs) == 0) return fail(Err::Timeout);
        rtos::delayMs(1);
    }
}

Status HttpClient::readChunked(BodyFn onBody, u32 deadlineMs, size_t* total) {
    for (;;) {
        HYDRA_TRY(const size_t headerLength, readLine(deadlineMs));
        (void)headerLength;

        const char* line = reinterpret_cast<const char*>(scratch_.data());
        const i32 chunkSize = parseHex(line);
        if (chunkSize < 0) return fail(Err::Protocol);

        if (chunkSize == 0) {
            // Kawałek zerowy kończy treść; po nim idą jeszcze nagłówki
            // końcowe, zamknięte pustym wierszem. Trzeba je odczytać,
            // inaczej zostaną w strumieniu i zatrują kolejne żądanie
            // przy połączeniu utrzymywanym.
            for (;;) {
                HYDRA_TRY(const size_t trailerLength, readLine(deadlineMs));
                if (trailerLength == 0) break;
            }
            return ok();
        }

        size_t taken = 0;
        HYDRA_CHECK(readSized(static_cast<size_t>(chunkSize), onBody, deadlineMs, &taken));
        *total += taken;

        // Każdy kawałek kończy własny CRLF, którego nikt nie liczy do treści.
        HYDRA_TRY(const size_t sep, readLine(deadlineMs));
        if (sep != 0) return fail(Err::Protocol);
    }
}

Status HttpClient::readBody(const Response& info, BodyFn onBody, u32 deadlineMs, size_t* total) {
    if (info.chunked) return readChunked(onBody, deadlineMs, total);
    if (info.contentLength > 0) {
        return readSized(static_cast<size_t>(info.contentLength), onBody, deadlineMs, total);
    }
    if (info.contentLength == 0) return ok();
    return readUntilClose(onBody, deadlineMs, total);
}

Result<HttpClient::Response> HttpClient::readResponse(const Request& request, BodyFn onBody,
                                                      HeaderFn onHeader, ByteSpan redirectTo,
                                                      bool* redirected) {
    const u32 deadline = static_cast<u32>(rtos::nowMs()) + request.timeoutMs;
    Response response;
    *redirected = false;

    // ── wiersz statusu ─────────────────────────────────────────────────────
    HYDRA_TRY(const size_t statusLength, readLine(deadline));
    const char* status = reinterpret_cast<const char*>(scratch_.data());

    if (statusLength < 12 || strncmp(status, "HTTP/1.", 7) != 0) return unexpected(Err::Protocol);
    const i32 code = parseDecimal(status + 8);
    if (code < 100 || code > 599) return unexpected(Err::Protocol);
    response.status = static_cast<u16>(code);

    // HTTP/1.0 domyślnie zamyka; HTTP/1.1 domyślnie utrzymuje.
    response.keepAlive = status[7] == '1';

    // ── nagłówki ───────────────────────────────────────────────────────────
    bool wantsRedirect = false;

    for (;;) {
        HYDRA_TRY(const size_t length, readLine(deadline));
        if (length == 0) break;                 // pusty wiersz kończy nagłówki

        char* line = reinterpret_cast<char*>(scratch_.data());
        char* colon = strchr(line, ':');
        if (colon == nullptr) continue;         // wiersz bez dwukropka — pomijamy

        *colon = '\0';
        char* value = colon + 1;
        while (*value == ' ' || *value == '\t') ++value;

        for (char* p = line; *p != '\0'; ++p) *p = toLower(*p);

        if (equalsIgnoreCase(line, "content-length")) {
            response.contentLength = parseDecimal(value);
        } else if (equalsIgnoreCase(line, "transfer-encoding")) {
            // Wartość bywa listą, np. "gzip, chunked" — szukamy słowa,
            // a nie porównujemy całości.
            response.chunked = strstr(value, "chunked") != nullptr;
        } else if (equalsIgnoreCase(line, "connection")) {
            response.keepAlive = strstr(value, "close") == nullptr;
        } else if (equalsIgnoreCase(line, "location") && !redirectTo.empty()) {
            const size_t length2 = strlen(value);
            if (length2 + 1 <= redirectTo.size()) {
                memcpy(redirectTo.data(), value, length2 + 1);
                wantsRedirect = true;
            } else {
                HYDRA_LOGW("adres przekierowania nie miesci sie w buforze");
            }
        }

        if (onHeader) onHeader(line, value);
    }

    const bool isRedirect = response.status == 301 || response.status == 302 ||
                            response.status == 303 || response.status == 307 ||
                            response.status == 308;

    // ── treść ──────────────────────────────────────────────────────────────
    //
    // HEAD nigdy nie ma treści, choć Content-Length bywa podany — czytanie go
    // zawisłoby do końca limitu czasu. Podobnie odpowiedzi 1xx, 204 i 304.
    const bool bodyless = request.method == Method::Head || response.status == 204 ||
                          response.status == 304 || response.status < 200;

    if (!bodyless) {
        // Przy przekierowaniu treść też trzeba odebrać (albo zamknąć
        // połączenie) — inaczej resztki zostają w strumieniu.
        size_t total = 0;
        BodyFn sink = (isRedirect && wantsRedirect) ? BodyFn{} : onBody;
        HYDRA_CHECK(readBody(response, sink, deadline, &total));
        response.bodyBytes = total;
    }

    if (isRedirect && wantsRedirect) *redirected = true;
    return response;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Wysyłka
// ═══════════════════════════════════════════════════════════════════════════

Result<HttpClient::Response> HttpClient::send(const Request& request, BodyFn onBody,
                                              HeaderFn onHeader) {
    if (transport_ == nullptr) return unexpected(Err::NotInitialized);
    if (request.host == nullptr || request.path == nullptr) return unexpected(Err::BadArgument);

    Request current = request;

    // Bufory na przekierowanie. Adres z nagłówka Location i rozłożony wynik
    // muszą przeżyć kolejny obieg, więc leżą tutaj, nie w `readResponse`.
    char location[256];
    u8   urlParts[288];

    for (u8 attempt = 0;; ++attempt) {
        const bool reusable = transport_->connected() && connectedHost_ != nullptr &&
                              connectedPort_ == current.port &&
                              equalsIgnoreCase(connectedHost_, current.host);

        if (!reusable) {
            // Zamykamy tylko to, co jest otwarte.
            //
            // Bezwarunkowe `stop()` na świeżym gnieździe wygląda niewinnie,
            // ale kasuje jego stan — łącznie z danymi, które zdążyły przyjść.
            // Zamykanie czegoś, co nie jest otwarte, nigdy nie jest potrzebne.
            if (transport_->connected()) transport_->stop();
            HYDRA_CHECK(transport_->connect(current.host, current.port, current.timeoutMs));
            connectedHost_ = current.host;
            connectedPort_ = current.port;
        }

        HYDRA_CHECK(sendRequest(current));

        bool redirected = false;
        auto response = readResponse(current, onBody, onHeader,
                                     ByteSpan{reinterpret_cast<u8*>(location), sizeof(location)},
                                     &redirected);
        if (!response) {
            end();
            return unexpected(response.error());
        }

        if (!response->keepAlive) {
            transport_->stop();
            connectedHost_ = nullptr;
        }

        if (!redirected || attempt >= current.maxRedirects) {
            response->redirects = attempt;
            if (redirected) {
                // Limit wyczerpany. Zwracamy odpowiedź przekierowującą taką,
                // jaka jest — wołający zobaczy status 30x i sam zdecyduje.
                HYDRA_LOGW("limit przekierowan (%u) wyczerpany", static_cast<unsigned>(attempt));
            }
            return *response;
        }

        // Location bywa względne ("/nowa/sciezka") albo pełne.
        if (location[0] == '/') {
            static char pathCopy[256];
            const size_t length = strlen(location);
            if (length + 1 > sizeof(pathCopy)) return unexpected(Err::OutOfRange);
            memcpy(pathCopy, location, length + 1);
            current.path = pathCopy;
        } else {
            auto url = parseUrl(location, ByteSpan{urlParts, sizeof(urlParts)});
            if (!url) return unexpected(url.error());

            if (url->secure && current.port != 443) {
                // Przeskok z http na https na tym samym gnieździe nie zadziała:
                // szyfrowanie wnosi `TlsClient`, którego tu nie ma i nie mamy
                // jak podmienić. Lepszy jawny błąd niż połączenie bez TLS-u.
                HYDRA_LOGE("przekierowanie na https wymaga transportu TLS");
                return unexpected(Err::NotSupported);
            }

            current.host = url->host;
            current.port = url->port;
            current.path = url->path;
        }

        // Ciało żądania idzie tylko za pierwszym razem: 303 i historycznie 302
        // zamieniają POST na GET, a powtórzenie treści pod nowy adres bywa
        // powtórzeniem zamówienia.
        if (response->status == 303 || response->status == 302) {
            current.method = Method::Get;
            current.body   = CByteSpan{};
        }
    }
}

Result<HttpClient::Response> HttpClient::get(const char* url, BodyFn onBody, HeaderFn onHeader) {
    u8 parts[288];
    HYDRA_TRY(const Url parsed, parseUrl(url, ByteSpan{parts, sizeof(parts)}));

    Request request;
    request.method = Method::Get;
    request.host   = parsed.host;
    request.port   = parsed.port;
    request.path   = parsed.path;

    return send(request, onBody, onHeader);
}

Result<HttpClient::Response> HttpClient::getToBuffer(const char* url, ByteSpan out,
                                                     size_t* written) {
    size_t used = 0;
    bool overflow = false;

    auto sink = [&](CByteSpan chunk) {
        const size_t room = out.size() - used;
        const size_t take = chunk.size() < room ? chunk.size() : room;
        if (take > 0) memcpy(out.data() + used, chunk.data(), take);
        used += take;
        // Czytamy dalej mimo przepełnienia: porzucenie w połowie zostawiłoby
        // w strumieniu ogon odpowiedzi, przez który następne żądanie na tym
        // samym połączeniu odczytałoby śmieci.
        if (take < chunk.size()) overflow = true;
    };

    auto response = get(url, BodyFn{sink});
    if (written != nullptr) *written = used;
    if (!response) return response;
    if (overflow) return unexpected(Err::OutOfRange);
    return response;
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
