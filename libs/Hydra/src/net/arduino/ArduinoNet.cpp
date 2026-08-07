/**
 * Hydra — backend sieciowy na Arduino (rozdz. 7).
 *
 * Drugi po HAL katalog backendu, w którym wolno włączać nagłówki Arduino.
 * Reguła 2 ze specyfikacji wymienia `src/hal/arduino/`, bo w chwili jej
 * spisania był to jedyny backend; zasada mówi jednak o *plikach backendu*,
 * a Wi-Fi jest peryferiem jak każde inne. Test w CI dopuszcza więc każdy
 * katalog backendu postaci src/<warstwa>/arduino/ — reszta drzewa pozostaje czysta.
 *
 * Cała różnica między ekosystemami zamyka się w tym pliku:
 *   - ESP32 ma WiFi.h z ESP-IDF pod spodem i ESPmDNS,
 *   - arduino-pico ma WiFi.h dla układu CYW43 i LEAmDNS,
 *   - STM32 zwykle pracuje po Ethernecie (W5500), gdzie „łączenie z siecią"
 *     sprowadza się do uzyskania adresu z DHCP.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST && HYDRA_ENABLE_NET

#include <Arduino.h>

#if HYDRA_PLAT_ESP32
#  include <ESPmDNS.h>
#  include <WiFi.h>
#elif HYDRA_PLAT_RP2
#  include <LEAmDNS.h>
#  include <WiFi.h>
#elif __has_include(<Ethernet.h>)
#  include <Ethernet.h>
#else
/*
 * Pozostałe platformy nie mają domyślnego transportu w rdzeniu. STM32 bywa
 * bezsieciowy z definicji — Nucleo-G474RE nie ma ani Wi-Fi, ani warstwy
 * fizycznej Ethernetu. Komunikat zamiast błędu o brakującym nagłówku, bo
 * ten drugi wygląda na usterkę frameworka, a jest wyborem płytki.
 */
#  error "HYDRA_ENABLE_NET na tej platformie wymaga biblioteki Ethernet " \
         "(np. stm32duino/STM32Ethernet) i płytki z warstwą fizyczną. " \
         "Płytka bez sprzętu sieciowego: zostaw moduł wyłączony."
#endif

#include "hydra/net/ITransport.hpp"

namespace hydra {
namespace net {
namespace arduino {

// ---------------------------------------------------------------------------
// Gniazdo
// ---------------------------------------------------------------------------

#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
using PlatformClient = WiFiClient;
#else
using PlatformClient = EthernetClient;
#endif

class ArduinoClient : public IClient {
public:
    Status connect(const char* host, u16 port, u32 timeoutMs) override {
        if (!host) return fail(Err::BadArgument);
#if HYDRA_PLAT_ESP32
        // Tylko core ESP32 przyjmuje limit czasu — na pozostałych obowiązuje
        // wartość wbudowana w stos sieciowy.
        if (!sock_.connect(host, port, static_cast<int32_t>(timeoutMs))) {
            return fail(Err::Timeout);
        }
#else
        HYDRA_UNUSED(timeoutMs);
        if (!sock_.connect(host, port)) return fail(Err::Timeout);
#endif
        return ok();
    }

    void stop() override { sock_.stop(); }
    bool connected() const override {
        // connected() nie jest w Arduino metodą stałą, a stan gniazda i tak
        // zmienia się poza naszą kontrolą.
        return const_cast<PlatformClient&>(sock_).connected();
    }

    size_t write(CByteSpan data) override {
        if (data.empty()) return 0;
        return sock_.write(data.data(), data.size());
    }

    size_t read(ByteSpan out) override {
        if (out.empty()) return 0;
        const int n = sock_.read(out.data(), out.size());
        return n > 0 ? static_cast<size_t>(n) : 0;
    }

    size_t available() override {
        const int n = sock_.available();
        return n > 0 ? static_cast<size_t>(n) : 0;
    }

private:
    PlatformClient sock_;
};

// ---------------------------------------------------------------------------
// Łącze
// ---------------------------------------------------------------------------

class ArduinoWifi : public INetworkInterface {
public:
    const char* name() const override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        return "wifi";
#else
        return "eth";
#endif
    }

    Status begin() override {
#if HYDRA_PLAT_ESP32
        WiFi.mode(WIFI_STA);
        // Automatyczne ponawianie po stronie core'a kolidowałoby z maszyną
        // stanów Hydry — backoff i wybór sieci są naszą sprawą.
        WiFi.setAutoReconnect(false);
#endif
        return ok();
    }

    Status connect(const NetworkCredentials& creds) override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        WiFi.disconnect();
        WiFi.begin(creds.ssid, creds.psk.reveal());
        return ok();
#else
        // Na Ethernecie nie ma poświadczeń — adres przychodzi z DHCP.
        HYDRA_UNUSED(creds);
        static u8 mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
        return Ethernet.begin(mac) == 1 ? ok() : fail(Err::IoError);
#endif
    }

    void disconnect() override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        WiFi.disconnect();
#endif
    }

    bool linkUp() const override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        return WiFi.status() == WL_CONNECTED;
#else
        return Ethernet.linkStatus() == LinkON && Ethernet.localIP() != INADDR_NONE;
#endif
    }

    u32 localIpV4() const override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        return static_cast<u32>(WiFi.localIP());
#else
        return static_cast<u32>(Ethernet.localIP());
#endif
    }

    i8 rssiDbm() const override {
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        return static_cast<i8>(WiFi.RSSI());
#else
        return 0;  // łącze przewodowe nie ma siły sygnału
#endif
    }

    IClient* createClient() override { return &client_; }

private:
    ArduinoClient client_;
};

// ---------------------------------------------------------------------------
// mDNS
// ---------------------------------------------------------------------------

class ArduinoMdns : public IMdns {
public:
    Status begin(const char* hostname) override {
        if (!hostname) return fail(Err::BadArgument);
#if HYDRA_PLAT_ESP32 || HYDRA_PLAT_RP2
        return MDNS.begin(hostname) ? ok() : fail(Err::IoError);
#else
        HYDRA_UNUSED(hostname);
        return fail(Err::NotSupported);
#endif
    }

    Status addService(const char* service, const char* proto, u16 port) override {
#if HYDRA_PLAT_ESP32
        MDNS.addService(service, proto, port);
        return ok();
#elif HYDRA_PLAT_RP2
        return MDNS.addService(service, proto, port) ? ok() : fail(Err::IoError);
#else
        HYDRA_UNUSED(service);
        HYDRA_UNUSED(proto);
        HYDRA_UNUSED(port);
        return fail(Err::NotSupported);
#endif
    }

    void end() override {
#if HYDRA_PLAT_ESP32
        MDNS.end();
#endif
    }
};

}  // namespace arduino

// ---------------------------------------------------------------------------
// Dostęp dla aplikacji
// ---------------------------------------------------------------------------

INetworkInterface& defaultNetworkInterface() {
    static arduino::ArduinoWifi iface;
    return iface;
}

IMdns& defaultMdns() {
    static arduino::ArduinoMdns mdns;
    return mdns;
}

}  // namespace net
}  // namespace hydra

#endif  // !HYDRA_PLAT_HOST && HYDRA_ENABLE_NET
