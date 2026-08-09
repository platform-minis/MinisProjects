#pragma once
/**
 * Hydra — klient WebSocket (RFC 6455) jako strumień bajtów.
 *
 * Kluczowa decyzja projektowa mieści się w deklaracji klasy: `WebSocketClient`
 * **jest** `IClient` i **opakowuje** `IClient`. Nie jest więc rodzeństwem MQTT,
 * tylko warstwą pod nim — dokładnie tym, czym jest w rzeczywistości.
 *
 *     WiFiClient           tcp;        // gniazdo
 *     WebSocketClient      ws{tcp};    // ramkowanie RFC 6455
 *     MqttClient           mqtt{ws};   // ten sam klient MQTT, bez zmian
 *
 * Gdyby zamiast tego powstał osobny `WebSocketLink` obok `MqttLink`, cała
 * obsługa MQTT — CONNECT, subskrypcje, QoS 1, retransmisje, keep-alive —
 * musiałaby istnieć w dwóch kopiach. Broker MyCastle (Aedes) słucha na
 * `ws://{host}:1902/mqtt`, więc to nie jest wariant egzotyczny, tylko główna
 * droga do platformy.
 *
 * **Zakres.** Tyle, ile potrzebuje MQTT po WebSockecie, i ani jednej rzeczy
 * więcej: uzgodnienie połączenia, ramki binarne z maskowaniem, składanie
 * fragmentów, odpowiedź na ping, zamknięcie. Bez rozszerzeń, bez kompresji
 * (permessage-deflate), bez trybu serwera i bez TLS — `wss://` powstaje przez
 * opakowanie `TlsClient` zamiast gniazda, nie przez zmianę tutaj.
 *
 * **Czego nie sprawdzamy.** Nagłówka `Sec-WebSocket-Accept` nie weryfikujemy:
 * wymagałby SHA-1, którego Hydra nie ma i którego nie warto dokładać dla
 * jednego zastosowania. Chroni on przed serwerem, który przez przypadek
 * odpowie kodem 101 na żądanie uaktualnienia — sytuacją, której w praktyce
 * nie ma. Kod odpowiedzi i obecność nagłówka `Upgrade` sprawdzamy.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/net/ITransport.hpp"

/** Bufor na rozpakowaną treść ramek czekającą na odczyt przez warstwę wyżej. */
#ifndef HYDRA_WS_BUFFER
#  define HYDRA_WS_BUFFER 512
#endif

namespace hydra {
namespace net {

class WebSocketClient : public IClient {
public:
    struct Config {
        /** Ścieżka zasobu; dla brokera MyCastle „/mqtt". */
        const char* path = "/mqtt";
        /**
         * Podprotokół ogłaszany w uzgodnieniu.
         *
         * Brokery MQTT po WebSockecie wymagają „mqtt"; bez tego Aedes
         * przyjmuje połączenie i zamyka je po pierwszej ramce, bo nie wie,
         * czym jest to, co dostał.
         */
        const char* subprotocol = "mqtt";
        /** Nagłówek Origin; niektóre pośredniki go wymagają. */
        const char* origin = nullptr;
        u32 handshakeTimeoutMs = 5000;
    };

    struct Stats {
        u32 framesSent     = 0;
        u32 framesReceived = 0;
        u32 pings          = 0;
        u32 protocolErrors = 0;
        u32 overflows      = 0;   ///< ramka większa niż bufor odbiorczy
    };

    explicit WebSocketClient(IClient& transport) : inner_(transport) {}

    void configure(const Config& cfg) { cfg_ = cfg; }

    // --- IClient -------------------------------------------------------------

    Status connect(const char* host, u16 port, u32 timeoutMs = 5000) override;
    void   stop() override;
    bool   connected() const override;

    size_t write(CByteSpan data) override;
    size_t read(ByteSpan out) override;
    size_t available() override;

    Stats stats() const { return stats_; }

private:
    enum class State : u8 { Idle, Open };

    Status handshake(const char* host, u16 port);
    /** Wysyła jedną ramkę z maską; klient RFC 6455 musi maskować zawsze. */
    Status sendFrame(u8 opcode, CByteSpan payload);
    /** Wciąga z gniazda tyle kompletnych ramek, ile się zmieści w buforze. */
    void   pump();
    /** Zwalnia miejsce zajęte przez już odczytane bajty. */
    void   compact();

    IClient& inner_;
    Config   cfg_{};
    State    state_ = State::Idle;

    /** Rozpakowana treść czekająca na odczyt. */
    u8     rx_[HYDRA_WS_BUFFER] = {};
    size_t rxLen_ = 0;
    size_t rxRead_ = 0;

    /**
     * Ziarno maski.
     *
     * Maskowanie w RFC 6455 nie jest zabezpieczeniem kryptograficznym —
     * chroni pośredniki przed zatruciem pamięci podręcznej ramką wyglądającą
     * jak żądanie HTTP. Wystarczy, żeby wartość się zmieniała; generator
     * kryptograficzny byłby tu kosztem bez pokrycia.
     */
    u32   maskSeed_ = 0x2545F491u;
    u32   nextMask();

    Stats stats_{};
};

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
