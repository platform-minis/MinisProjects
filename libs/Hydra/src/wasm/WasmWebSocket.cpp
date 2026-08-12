/**
 * Gniazdo WebSocket przeglądarki jako `net::IClient`.
 *
 * ## Dlaczego to nie jest `net::WebSocketClient`
 *
 * Hydra ma już klienta WebSocketu — i w przeglądarce **nie wolno go użyć**.
 * Tamten sam składa ramki RFC 6455 nad strumieniem TCP, bo na układzie nikt
 * tego za niego nie zrobi. Przeglądarka ramkuje sama i nie daje dostępu do
 * gniazda pod spodem, więc nasze ramkowanie opakowałoby ramki w ramki:
 * broker dostałby nagłówek WebSocketu jako treść wiadomości.
 *
 * Ta klasa jest więc rodzeństwem `WebSocketClient`, nie jego użytkownikiem:
 * wystawia ten sam interfejs strumienia bajtów, tylko granice wiadomości
 * oddaje przeglądarce. `MqttClient` nie widzi różnicy — i o to chodzi.
 *
 * ## Odbiór jest asynchroniczny, `read()` nie
 *
 * Przeglądarka dostarcza dane zdarzeniem, a `IClient::read()` ma je oddać
 * natychmiast. Między jednym a drugim stoi bufor pierścieniowy zapisywany
 * z wywołania zwrotnego i czytany z pętli gry. Oba biegną na tym samym
 * wątku — wywołanie zwrotne wykonuje się między klatkami — więc nie ma tu
 * wyścigu i nie ma muteksu.
 *
 * Bufor jest stały. Przepełnienie liczymy i zgłaszamy w logu, zamiast
 * rosnąć w nieskończoność: broker zalewający urządzenie szybciej, niż ono
 * czyta, to sytuacja do zauważenia, a nie do przeczekania.
 */

#include "hydra/core/Config.hpp"

#if defined(HYDRA_PLAT_WASM) && HYDRA_PLAT_WASM && HYDRA_ENABLE_NET

#include <emscripten/websocket.h>
#include <stdio.h>
#include <string.h>

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/net/ITransport.hpp"

HYDRA_LOG_MODULE("net.ws")

namespace hydra {
namespace net {

class WasmWebSocketClient : public IClient {
public:
    /** Bufor odbiorczy; mieści kilka wiadomości MQTT o typowym rozmiarze. */
    static constexpr size_t kRxCapacity = 8192;

    /**
     * @param scheme  "ws" albo "wss"; przy stronie na HTTPS przeglądarka
     *                odrzuci połączenie nieszyfrowane, więc to musi pasować
     *                do tego, skąd stronę podano
     * @param path    ścieżka zasobu u brokera, zwykle "/mqtt"
     */
    WasmWebSocketClient(const char* scheme = "ws", const char* path = "/mqtt")
        : scheme_(scheme), path_(path) {}

    Status connect(const char* host, u16 port, u32 timeoutMs) override {
        if (!emscripten_websocket_is_supported()) return fail(Err::NotSupported);
        stop();

        char url[192];
        snprintf(url, sizeof(url), "%s://%s:%u%s", scheme_, host,
                 static_cast<unsigned>(port), path_);

        EmscriptenWebSocketCreateAttributes attrs;
        emscripten_websocket_init_create_attributes(&attrs);
        attrs.url = url;
        // Podprotokół, którego wymagają brokery MQTT po WebSockecie. Bez niego
        // Aedes przyjmuje połączenie i zamyka je po pierwszej ramce.
        attrs.protocols = "mqtt";

        socket_ = emscripten_websocket_new(&attrs);
        if (socket_ <= 0) return fail(Err::IoError);

        emscripten_websocket_set_onopen_callback(socket_, this, &onOpen);
        emscripten_websocket_set_onmessage_callback(socket_, this, &onMessage);
        emscripten_websocket_set_onclose_callback(socket_, this, &onClose);
        emscripten_websocket_set_onerror_callback(socket_, this, &onError);

        /*
         * Uzgodnienie nie blokuje — i to nie jest uproszczenie.
         *
         * Zdarzenie otwarcia przyjdzie dopiero wtedy, gdy oddamy sterowanie
         * przeglądarce, więc pętla czekająca tutaj czekałaby na coś, czego
         * sama nie dopuszcza. Obejście przez `emscripten_sleep` wymaga
         * ASYNCIFY, które przepisuje cały obraz na maszynę stanów: kilkadziesiąt
         * procent rozmiaru i tyle samo szybkości, dla jednej pętli.
         *
         * Zamiast tego zwracamy `Err::WouldBlock`, dopóki gniazdo się nie
         * otworzy. Pasuje to do reszty Hydry: `ConnectionManager` odpytuje
         * `linkUp()`, a `MqttClient::connect(now)` i tak jest wołane
         * cyklicznie z taska sieciowego. Wołający, który tego nie robi,
         * dostaje jasny kod zamiast zawieszonej karty.
         */
        (void)timeoutMs;
        deadline_ = static_cast<u32>(rtos::nowMs()) + timeoutMs;
        return poll();
    }

    void stop() override {
        if (socket_ > 0) {
            emscripten_websocket_close(socket_, 1000, "koniec");
            emscripten_websocket_delete(socket_);
        }
        socket_ = 0;
        open_ = false;
        failed_ = false;
        head_ = tail_ = 0;
    }

    bool connected() const override { return open_; }

    /**
     * Stan uzgodnienia. Wołać, dopóki zwraca `Err::WouldBlock`.
     *
     * `connect()` woła to raz na wejściu, więc połączenie, które zdążyło się
     * otworzyć wcześniej, wraca od razu jako gotowe.
     */
    Status poll() {
        if (open_) return ok();
        if (failed_) { stop(); return fail(Err::IoError); }
        if (socket_ <= 0) return fail(Err::NotInitialized);
        if (static_cast<i32>(deadline_ - static_cast<u32>(rtos::nowMs())) <= 0) {
            stop();
            return fail(Err::Timeout);
        }
        return fail(Err::WouldBlock);
    }

    size_t write(CByteSpan data) override {
        if (!open_ || data.empty()) return 0;
        // Jedna ramka binarna na wywołanie. MQTT i tak pisze całymi pakietami,
        // a dzielenie ich tutaj rozbiłoby granice wiadomości u brokera.
        const EMSCRIPTEN_RESULT r = emscripten_websocket_send_binary(
            socket_, const_cast<u8*>(data.data()), static_cast<uint32_t>(data.size()));
        return r == EMSCRIPTEN_RESULT_SUCCESS ? data.size() : 0;
    }

    size_t read(ByteSpan out) override {
        size_t taken = 0;
        while (taken < out.size() && tail_ != head_) {
            out.data()[taken++] = rx_[tail_];
            tail_ = (tail_ + 1) % kRxCapacity;
        }
        return taken;
    }

    size_t available() override {
        return (head_ + kRxCapacity - tail_) % kRxCapacity;
    }

    /** Ile bajtów przepadło z powodu pełnego bufora. */
    u32 dropped() const { return dropped_; }

private:
    static EM_BOOL onOpen(int, const EmscriptenWebSocketOpenEvent*, void* user) {
        static_cast<WasmWebSocketClient*>(user)->open_ = true;
        return EM_TRUE;
    }
    static EM_BOOL onClose(int, const EmscriptenWebSocketCloseEvent*, void* user) {
        static_cast<WasmWebSocketClient*>(user)->open_ = false;
        return EM_TRUE;
    }
    static EM_BOOL onError(int, const EmscriptenWebSocketErrorEvent*, void* user) {
        auto* self = static_cast<WasmWebSocketClient*>(user);
        self->failed_ = true;
        self->open_ = false;
        return EM_TRUE;
    }

    static EM_BOOL onMessage(int, const EmscriptenWebSocketMessageEvent* e, void* user) {
        auto* self = static_cast<WasmWebSocketClient*>(user);
        if (e->isText) {
            // MQTT po WebSockecie jest binarne. Ramka tekstowa oznacza, że po
            // drugiej stronie jest coś innego niż broker — lepiej to zgłosić
            // niż wsypać do strumienia bajty, których nikt nie oczekuje.
            HYDRA_LOGW("odrzucono ramke tekstowa (%u B)", static_cast<unsigned>(e->numBytes));
            return EM_TRUE;
        }

        for (uint32_t i = 0; i < e->numBytes; ++i) {
            const size_t next = (self->head_ + 1) % kRxCapacity;
            if (next == self->tail_) {
                self->dropped_ += e->numBytes - i;
                if (self->dropped_ == e->numBytes - i) {
                    HYDRA_LOGE("bufor odbiorczy pelny — %u B przepadlo",
                               static_cast<unsigned>(self->dropped_));
                }
                break;
            }
            self->rx_[self->head_] = e->data[i];
            self->head_ = next;
        }
        return EM_TRUE;
    }

    const char* scheme_;
    const char* path_;

    EMSCRIPTEN_WEBSOCKET_T socket_ = 0;
    bool open_   = false;
    bool failed_ = false;
    u32  deadline_ = 0;

    u8     rx_[kRxCapacity];
    size_t head_ = 0;
    size_t tail_ = 0;
    u32    dropped_ = 0;
};

/** Gniazdo dla `MqttClient` w przeglądarce. */
IClient& browserWebSocket() {
    static WasmWebSocketClient instance;
    return instance;
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_PLAT_WASM && HYDRA_ENABLE_NET
