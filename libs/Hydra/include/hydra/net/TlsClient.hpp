#pragma once
/**
 * Hydra — gniazdo szyfrowane TLS (rozdz. 7.3).
 *
 * Nakładka na gniazdo bezpieczne dostarczane przez platformę: mbedTLS na
 * ESP32 i RP2350, BearSSL na RP2040. Hydra nie implementuje TLS i nie ma
 * takiego zamiaru — implementacja protokołu kryptograficznego to nie jest
 * coś, co pisze się przy okazji frameworka.
 *
 * Adapter jest szablonem, tak jak adaptery bibliotek graficznych: dzięki temu
 * Hydra nie włącza nagłówków stosu sieciowego, reguła zależności pozostaje
 * nienaruszona, a logika konfiguracji daje się przetestować na hoście atrapą.
 *
 *     #include <WiFiClientSecure.h>
 *     #include <hydra/net/TlsClient.hpp>
 *
 *     WiFiClientSecure secure;
 *     hydra::net::TlsClient<WiFiClientSecure> client(secure);
 *     client.configure({.caCertificate = kIsrgRootX1});
 *
 * Uwaga o trybie bez weryfikacji: jest dostępny, bo bywa niezbędny przy
 * pierwszym uruchomieniu w sieci lokalnej z certyfikatem własnym. Wymaga
 * jednak jawnego włączenia i zostawia ślad w logu na poziomie ostrzeżenia.
 * TLS bez weryfikacji jest gorszy niż jego brak — wygląda na bezpieczny,
 * a chroni wyłącznie przed biernym podsłuchem.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/Log.hpp"
#include "hydra/net/ITransport.hpp"

namespace hydra {
namespace net {

/** Domyślny port usług szyfrowanych. */
constexpr u16 kTlsDefaultPort = 8883;

template <typename SecureClient>
class TlsClient : public IClient {
public:
    struct Config {
        /** Certyfikat urzędu certyfikacji w formacie PEM. */
        const char* caCertificate = nullptr;

        /** Certyfikat i klucz urządzenia — uwierzytelnianie dwustronne. */
        const char* clientCertificate = nullptr;
        const char* clientKey         = nullptr;

        /**
         * Wyłącza weryfikację tożsamości serwera. Wymaga jawnej decyzji
         * i zostawia ślad w logu — patrz komentarz na górze pliku.
         */
        bool allowInsecure = false;

        /** Limit czasu uzgadniania; szyfrowanie trwa dłużej niż samo TCP. */
        u32 handshakeTimeoutMs = 15000;
    };

    explicit TlsClient(SecureClient& client) : client_(client) {}

    Status configure(const Config& cfg) {
        // Brak jakiegokolwiek sposobu weryfikacji przy wyłączonym trybie
        // niezabezpieczonym to pomyłka konfiguracyjna, nie wybór.
        if (!cfg.caCertificate && !cfg.allowInsecure) return fail(Err::BadArgument);
        if ((cfg.clientCertificate == nullptr) != (cfg.clientKey == nullptr)) {
            return fail(Err::BadArgument);
        }

        cfg_       = cfg;
        configured_ = true;
        return ok();
    }

    Status connect(const char* host, u16 port, u32 timeoutMs) override {
        if (!configured_) return fail(Err::NotInitialized);
        if (!host) return fail(Err::BadArgument);

        if (cfg_.allowInsecure) {
            HYDRA_LOG_AT(::hydra::LogLevel::Warn, "net.tls",
                         "połączenie z %s bez weryfikacji tożsamości serwera", host);
            client_.setInsecure();
        } else {
            client_.setCACert(cfg_.caCertificate);
        }

        if (cfg_.clientCertificate) {
            client_.setCertificate(cfg_.clientCertificate);
            client_.setPrivateKey(cfg_.clientKey);
        }

        // Uzgadnianie TLS trwa dłużej niż nawiązanie samego TCP — limit
        // dobrany pod TCP powodowałby zrywanie poprawnych połączeń.
        const u32 limit = timeoutMs > cfg_.handshakeTimeoutMs ? timeoutMs
                                                              : cfg_.handshakeTimeoutMs;
        if (!client_.connect(host, port, static_cast<i32>(limit))) {
            return fail(Err::Timeout);
        }
        return ok();
    }

    void stop() override { client_.stop(); }

    bool connected() const override {
        return const_cast<SecureClient&>(client_).connected();
    }

    size_t write(CByteSpan data) override {
        if (data.empty()) return 0;
        return client_.write(data.data(), data.size());
    }

    size_t read(ByteSpan out) override {
        if (out.empty()) return 0;
        const int n = client_.read(out.data(), out.size());
        return n > 0 ? static_cast<size_t>(n) : 0;
    }

    size_t available() override {
        const int n = client_.available();
        return n > 0 ? static_cast<size_t>(n) : 0;
    }

    bool verifiesPeer() const { return configured_ && !cfg_.allowInsecure; }
    bool mutualAuth() const { return configured_ && cfg_.clientCertificate != nullptr; }

private:
    SecureClient& client_;
    Config        cfg_{};
    bool          configured_ = false;
};

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
