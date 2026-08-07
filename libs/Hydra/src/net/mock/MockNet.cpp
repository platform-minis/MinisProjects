/** Hydra — implementacja atrap transportu sieciowego (build hostowy). */

#include "hydra/net/Mock.hpp"

#if HYDRA_PLAT_HOST && HYDRA_ENABLE_NET

#include <string.h>

namespace hydra {
namespace net {
namespace mock {

// ---------------------------------------------------------------------------
// MockClient
// ---------------------------------------------------------------------------

Status MockClient::connect(const char* host, u16 port, u32) {
    ++connectCalls_;

    if (connectError_ != Err::None) {
        const Err e  = connectError_;
        connectError_ = Err::None;  // wymuszony błąd jest jednorazowy
        return fail(e);
    }
    if (!host) return fail(Err::BadArgument);

    strncpy(host_, host, sizeof(host_) - 1);
    host_[sizeof(host_) - 1] = '\0';
    port_      = port;
    connected_ = true;
    return ok();
}

void MockClient::stop() {
    connected_ = false;
    rxLen_ = rxPos_ = 0;
}

size_t MockClient::write(CByteSpan data) {
    if (!connected_) return 0;

    size_t limit = data.size();
    if (writeLimit_ > 0 && limit > writeLimit_) limit = writeLimit_;

    size_t n = 0;
    while (n < limit && txLen_ < kBufSize) txBuf_[txLen_++] = data[n++];
    return n;
}

size_t MockClient::read(ByteSpan out) {
    if (!connected_) return 0;

    size_t n = 0;
    while (n < out.size() && rxPos_ < rxLen_) out[n++] = rxBuf_[rxPos_++];
    return n;
}

void MockClient::injectRx(CByteSpan data) {
    // Zwijamy już odczytaną część, żeby długie sesje testowe nie wyczerpały bufora.
    if (rxPos_ > 0 && rxPos_ == rxLen_) rxLen_ = rxPos_ = 0;

    size_t written = 0;
    for (; written < data.size() && rxLen_ < kBufSize; ++written) {
        rxBuf_[rxLen_++] = data[written];
    }
    injectDropped_ += static_cast<u32>(data.size() - written);
}

void MockClient::clear() {
    host_[0]      = '\0';
    port_         = 0;
    connected_    = false;
    connectError_ = Err::None;
    connectCalls_ = 0;
    writeLimit_   = 0;
    injectDropped_ = 0;
    txLen_ = rxLen_ = rxPos_ = 0;
}

// ---------------------------------------------------------------------------
// MockNetwork
// ---------------------------------------------------------------------------

Status MockNetwork::begin() {
    begun_ = true;
    return ok();
}

Status MockNetwork::connect(const NetworkCredentials& creds) {
    ++connectCalls_;
    strncpy(lastSsid_, creds.ssid, sizeof(lastSsid_) - 1);
    lastSsid_[sizeof(lastSsid_) - 1] = '\0';

    if (connectError_ != Err::None) {
        const Err e   = connectError_;
        connectError_ = Err::None;
        return fail(e);
    }
    // Łącze nie pojawia się natychmiast — tak samo jak na prawdziwym Wi-Fi.
    // Test decyduje, kiedy (i czy) wywołać setLinkUp(true).
    return ok();
}

void MockNetwork::disconnect() {
    linkUp_ = false;
    ip_     = 0;
}

void MockNetwork::setLinkUp(bool up, u32 ip) {
    linkUp_ = up;
    ip_     = up ? ip : 0;
}

void MockNetwork::clear() {
    linkUp_ = false;
    begun_  = false;
    rssi_   = -55;
    ip_     = 0;
    connectError_ = Err::None;
    connectCalls_ = 0;
    lastSsid_[0]  = '\0';
    client.clear();
}

// ---------------------------------------------------------------------------
// MockMdns
// ---------------------------------------------------------------------------

Status MockMdns::begin(const char* hostname) {
    if (!hostname) return fail(Err::BadArgument);
    strncpy(hostname_, hostname, sizeof(hostname_) - 1);
    hostname_[sizeof(hostname_) - 1] = '\0';
    active_ = true;
    return ok();
}

Status MockMdns::addService(const char*, const char*, u16) {
    if (!active_) return fail(Err::NotInitialized);
    ++services_;
    return ok();
}

void MockMdns::clear() {
    hostname_[0] = '\0';
    active_      = false;
    services_    = 0;
}

}  // namespace mock

// Na hoście domyślnym interfejsem są atrapy — dzięki temu przykłady sieciowe
// kompilują się i dają uruchomić bez sprzętu.
INetworkInterface& defaultNetworkInterface() {
    static mock::MockNetwork iface;
    return iface;
}

IMdns& defaultMdns() {
    static mock::MockMdns mdns;
    return mdns;
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_PLAT_HOST && HYDRA_ENABLE_NET
