#pragma once
/**
 * @file HttpClient.hpp
 * @brief Klient HTTP/1.1 nad dowolnym strumieniem `IClient`.
 *
 * ## HTTPS wychodzi za darmo
 *
 * Klient nie wie, czym jest szyfrowanie — dostaje `IClient` i pisze do niego
 * bajty. Podaj `TlsClient`, a to samo wywołanie idzie po HTTPS:
 *
 *     // http://
 *     http.begin(*net.createClient(), scratch);
 *
 *     // https:// — ten sam klient, inny transport
 *     TlsClient<WiFiClientSecure> tls(secure);
 *     TlsClient<WiFiClientSecure>::Config tlsCfg;
 *     tlsCfg.caCertificate = kRootCa;
 *     HYDRA_CHECK(tls.configure(tlsCfg));
 *     http.begin(tls, scratch);
 *
 * To jest powód, dla którego protokół i transport są tu rozdzielone. Klient
 * z wbudowanym TLS-em musiałby mieć drugi zestaw metod i drugi zestaw błędów.
 *
 * ## Treść płynie strumieniem, nie do bufora
 *
 * Odpowiedź trafia do wołającego przez wywołanie zwrotne, kawałek po kawałku.
 * Pobranie wsadu OTA o rozmiarze 2 MB działa więc na buforze 512-bajtowym —
 * a wariant „zwróć całość w tablicy" nie działałby w ogóle, bo na urządzeniu
 * nie ma gdzie tego położyć. Kto chce małą odpowiedź w pamięci, dostaje
 * `getToBuffer()`, które robi to jawnie i z limitem.
 *
 * ## Co jest obsłużone
 *
 *  - metody GET, HEAD, POST, PUT, PATCH, DELETE,
 *  - `Content-Length` **i** `Transfer-Encoding: chunked` (serwery HTTP/1.1
 *    używają go bez pytania, gdy nie znają długości z góry),
 *  - nagłówki własne wołającego,
 *  - przekierowania 301/302/303/307/308 — do `maxRedirects`,
 *  - `Connection: keep-alive` i ponowne użycie połączenia.
 *
 * ## Czego nie ma i dlaczego
 *
 *  - **Kompresji treści.** Nie wysyłamy `Accept-Encoding: gzip`, więc serwer
 *    przysyła tekst bez pakowania. Rozpakowanie wymagałoby okna słownika
 *    32 KB — na urządzeniu z 64 KB RAM to nie jest kompromis, tylko koniec.
 *  - **HTTP/2.** Wymaga wielostrumieniowości i HPACK; do rozmowy z API
 *    urządzenia nie wnosi nic, czego nie da HTTP/1.1 z keep-alive.
 *  - **Ciasteczek.** Klient nie ma sesji ani miejsca na ich trzymanie.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/Delegate.hpp"
#include "hydra/net/ITransport.hpp"

namespace hydra {
namespace net {

/** Rozłożony adres zasobu. Wskaźniki pokazują w bufor podany do `parseUrl`. */
struct Url {
    bool        secure = false;   ///< true dla https
    const char* host   = nullptr;
    u16         port   = 0;
    const char* path   = nullptr; ///< zawsze zaczyna się od '/'
};

/**
 * Rozkłada adres na części.
 *
 * Zapisuje do `scratch`, bo wynik to wskaźniki na łańcuchy zakończone zerem,
 * a w adresie ich nie ma — trzeba je wstawić w kopii. Bufor musi przeżyć
 * cały czas korzystania z wyniku.
 */
Result<Url> parseUrl(const char* url, ByteSpan scratch);

class HttpClient {
public:
    enum class Method : u8 { Get, Head, Post, Put, Patch, Delete };

    struct Header {
        const char* name  = nullptr;
        const char* value = nullptr;
    };

    struct Request {
        Method            method = Method::Get;
        const char*       host   = nullptr;
        u16               port   = 80;
        const char*       path   = "/";
        Span<const Header> headers{};
        /** Treść żądania; pusta dla GET i HEAD. */
        CByteSpan         body{};
        /** Typ treści; przy niepustym `body` domyślnie octet-stream. */
        const char*       contentType = nullptr;
        u32               timeoutMs   = 10000;
        /** Ile przekierowań wolno pójść. 0 wyłącza podążanie za nimi. */
        u8                maxRedirects = 3;
        /** Czy prosić o utrzymanie połączenia do kolejnego żądania. */
        bool              keepAlive = false;
    };

    struct Response {
        u16    status        = 0;
        /** -1, gdy serwer nie podał długości (treść kodowana kawałkami). */
        i32    contentLength = -1;
        bool   chunked       = false;
        bool   keepAlive     = false;
        /** Ile bajtów treści faktycznie odebrano. */
        size_t bodyBytes     = 0;
        /** Ile przekierowań przeszliśmy, zanim doszło do tej odpowiedzi. */
        u8     redirects     = 0;

        constexpr bool ok() const { return status >= 200 && status < 300; }
    };

    /** Nagłówek odpowiedzi. Nazwa jest już zamieniona na małe litery. */
    using HeaderFn = Delegate<void(const char* name, const char* value)>;
    /** Kawałek treści. Może przyjść wiele razy; pusty oznacza koniec. */
    using BodyFn   = Delegate<void(CByteSpan chunk)>;

    HttpClient() = default;

    /**
     * @param transport gniazdo; `TlsClient` dla HTTPS
     * @param scratch   bufor na wiersz statusu i nagłówki — co najmniej 256 B,
     *                  praktycznie 1 KB, bo nagłówki bywają długie
     */
    Status begin(IClient& transport, ByteSpan scratch);

    /** Wykonuje żądanie i przepuszcza treść przez `onBody`. */
    Result<Response> send(const Request& request, BodyFn onBody = {}, HeaderFn onHeader = {});

    /** Skrót: GET pod adres, treść do wywołania zwrotnego. */
    Result<Response> get(const char* url, BodyFn onBody, HeaderFn onHeader = {});

    /**
     * Skrót: GET z treścią do bufora wołającego.
     *
     * Treść dłuższa niż bufor kończy się `Err::OutOfRange` **po** odebraniu
     * całości — połączenie zostaje w stanie zdatnym do dalszej pracy, a nie
     * porzucone w połowie odpowiedzi.
     */
    Result<Response> getToBuffer(const char* url, ByteSpan out, size_t* written);

    /** Zamyka połączenie. */
    void end();

    /** Czy strumień nadal stoi otwarty po ostatniej odpowiedzi. */
    bool connected() const;

private:
    Status sendRequest(const Request& request);
    Result<Response> readResponse(const Request& request, BodyFn onBody, HeaderFn onHeader,
                                  ByteSpan redirectTo, bool* redirected);
    /** Wczytuje wiersz zakończony CRLF do `scratch_`. Bez CRLF w wyniku. */
    Result<size_t> readLine(u32 deadlineMs);
    Status readBody(const Response& info, BodyFn onBody, u32 deadlineMs, size_t* total);
    Status readChunked(BodyFn onBody, u32 deadlineMs, size_t* total);
    Status readSized(size_t length, BodyFn onBody, u32 deadlineMs, size_t* total);
    Status readUntilClose(BodyFn onBody, u32 deadlineMs, size_t* total);

    IClient* transport_ = nullptr;
    ByteSpan scratch_{};
    /** Ostatni host, z którym połączono — do ponownego użycia strumienia. */
    const char* connectedHost_ = nullptr;
    u16         connectedPort_ = 0;
};

/** Nazwa metody do wiersza żądania. */
const char* toString(HttpClient::Method method);

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
