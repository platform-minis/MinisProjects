#pragma once
/**
 * Hydra — atrapy transportu sieciowego dla buildu hostowego.
 *
 * Pozwalają przetestować maszynę stanów połączenia i cały protokół MQTT bez
 * sieci i bez brokera: test wstrzykuje ramki, które „przyszły z serwera",
 * i ogląda bajty, które klient wysłał. Ponieważ ConnectionManager przyjmuje
 * czas argumentem, backoff i przełączanie sieci zapasowych sprawdza się
 * w mikrosekundach zamiast czekać realne minuty.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_PLAT_HOST && HYDRA_ENABLE_NET

#include "hydra/net/ITransport.hpp"

namespace hydra {
namespace net {
namespace mock {

class MockClient : public IClient {
public:
    /** Bufor mieści realistyczny obraz testowy wraz z nagłówkami odpowiedzi. */
    static constexpr size_t kBufSize = 8192;

    Status connect(const char* host, u16 port, u32 timeoutMs) override;
    void   stop() override;
    bool   connected() const override { return connected_; }
    size_t write(CByteSpan data) override;
    size_t read(ByteSpan out) override;
    size_t available() override { return rxLen_ - rxPos_; }

    // --- sterowanie atrapą ---
    /** Wstawia bajty tak, jakby przysłał je serwer. */
    void injectRx(CByteSpan data);
    /**
     * Ile bajtów nie zmieściło się w buforze. Ciche obcięcie w atrapie daje
     * niepowodzenia testów wskazujące na kod, który jest poprawny — licznik
     * pozwala je natychmiast odróżnić.
     */
    u32 injectDropped() const { return injectDropped_; }
    /** Wszystko, co klient wysłał od ostatniego clearSent(). */
    CByteSpan sent() const { return CByteSpan{txBuf_, txLen_}; }
    void clearSent() { txLen_ = 0; }

    /** Wymusza błąd przy kolejnym connect(). */
    void failNextConnect(Err error) { connectError_ = error; }
    /** Zrywa połączenie tak, jak zrobiłby to zdalny koniec. */
    void forceDisconnect() { connected_ = false; }
    /** Ogranicza liczbę bajtów przyjmowanych w jednym write() — test zapisu częściowego. */
    void limitWrite(size_t maxBytes) { writeLimit_ = maxBytes; }

    const char* lastHost() const { return host_; }
    u16         lastPort() const { return port_; }
    u32         connectCalls() const { return connectCalls_; }

    void clear();

private:
    char   host_[64] = {};
    u16    port_     = 0;
    bool   connected_ = false;
    Err    connectError_ = Err::None;
    u32    connectCalls_ = 0;
    size_t writeLimit_   = 0;

    u8     txBuf_[kBufSize] = {};
    size_t txLen_ = 0;
    u8     rxBuf_[kBufSize] = {};
    size_t rxLen_ = 0;
    size_t rxPos_ = 0;
    u32    injectDropped_ = 0;
};

class MockNetwork : public INetworkInterface {
public:
    const char* name() const override { return "mock"; }
    Status begin() override;
    Status connect(const NetworkCredentials& creds) override;
    void   disconnect() override;
    bool   linkUp() const override { return linkUp_; }
    u32    localIpV4() const override { return ip_; }
    i8     rssiDbm() const override { return rssi_; }
    IClient* createClient() override { return &client; }

    // --- sterowanie atrapą ---
    /** Symuluje uzyskanie albo utratę łącza. */
    void setLinkUp(bool up, u32 ip = 0xC0A80102u);
    void setRssi(i8 dbm) { rssi_ = dbm; }
    /** Wymusza odrzucenie kolejnej próby połączenia przez interfejs. */
    void failNextConnect(Err error) { connectError_ = error; }

    u32         connectCalls() const { return connectCalls_; }
    const char* lastSsid() const { return lastSsid_; }
    bool        begun() const { return begun_; }

    void clear();

    MockClient client;

private:
    bool linkUp_ = false;
    bool begun_  = false;
    i8   rssi_   = -55;
    u32  ip_     = 0;
    Err  connectError_ = Err::None;
    u32  connectCalls_ = 0;
    char lastSsid_[kSsidMax] = {};
};

class MockMdns : public IMdns {
public:
    Status begin(const char* hostname) override;
    Status addService(const char* service, const char* proto, u16 port) override;
    void   end() override { active_ = false; }

    const char* hostname() const { return hostname_; }
    bool        active() const { return active_; }
    u8          serviceCount() const { return services_; }
    void        clear();

private:
    char hostname_[64] = {};
    bool active_   = false;
    u8   services_ = 0;
};

}  // namespace mock
}  // namespace net
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST && HYDRA_ENABLE_NET
