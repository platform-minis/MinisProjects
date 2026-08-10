#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/net/UdpClient.hpp"

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"

HYDRA_LOG_MODULE("net.udp")

namespace hydra {
namespace net {

Status UdpClient::begin(IUdpSocket& socket, u16 localPort) {
    socket_ = &socket;
    peer_   = Endpoint{};
    rejected_  = 0;
    truncated_ = 0;
    broadcastOn_ = false;

    if (socket.isOpen()) return ok();
    return socket.open(localPort);
}

void UdpClient::end() {
    if (socket_ != nullptr) socket_->close();
    socket_ = nullptr;
    peer_   = Endpoint{};
}

Status UdpClient::setPeer(Endpoint peer) {
    if (!peer.valid()) return fail(Err::BadArgument);
    peer_ = peer;
    return ok();
}

Status UdpClient::setPeer(INetworkInterface& net, const char* host, u16 port) {
    if (host == nullptr || port == 0) return fail(Err::BadArgument);

    HYDRA_TRY(const u32 address, net.resolve(host));
    if (address == 0) return fail(Err::NotFound);

    peer_ = Endpoint{address, port};
    return ok();
}

Status UdpClient::send(CByteSpan data) {
    if (socket_ == nullptr) return fail(Err::NotInitialized);
    if (!peer_.valid())     return fail(Err::NotInitialized);
    return sendTo(peer_, data);
}

Status UdpClient::sendTo(Endpoint to, CByteSpan data) {
    if (socket_ == nullptr) return fail(Err::NotInitialized);
    if (!to.valid())        return fail(Err::BadArgument);

    // Sprawdzenie długości tutaj, a nie w backendzie: komunikat o zbyt dużym
    // datagramie ma trafić do wołającego, zanim pakiet wyjdzie i zgubi się
    // po drodze na fragmentacji.
    if (data.size() > socket_->maxDatagram()) return fail(Err::OutOfRange);

    return socket_->sendTo(to, data);
}

Status UdpClient::broadcast(u16 port, CByteSpan data) {
    if (socket_ == nullptr) return fail(Err::NotInitialized);
    if (port == 0)          return fail(Err::BadArgument);

    if (!broadcastOn_) {
        // Zgoda na rozgłaszanie włączana raz i tylko wtedy, gdy ktoś jej
        // faktycznie użyje — gniazdo z włączonym rozgłaszaniem „na wszelki
        // wypadek" pozwala pomyłce w adresie zalać całą sieć.
        HYDRA_CHECK(socket_->setBroadcast(true));
        broadcastOn_ = true;
    }

    return sendTo(Endpoint{kBroadcastIpv4, port}, data);
}

Result<Datagram> UdpClient::receive(ByteSpan out, u32 timeoutMs) {
    if (socket_ == nullptr) return unexpected(Err::NotInitialized);

    const u32 deadline = static_cast<u32>(rtos::nowMs()) + timeoutMs;

    for (;;) {
        auto got = socket_->receiveFrom(out);

        if (!got) {
            if (got.error() != Err::WouldBlock) return unexpected(got.error());

            // Nic nie czeka. Przy zerowym limicie wracamy od razu — wołający
            // odpytuje w swojej pętli i sam decyduje, co robić w międzyczasie.
            if (timeoutMs == 0) return unexpected(Err::WouldBlock);
            if (static_cast<i32>(deadline - static_cast<u32>(rtos::nowMs())) <= 0) {
                return unexpected(Err::Timeout);
            }

            rtos::delayMs(1);
            continue;
        }

        if (got->truncated) ++truncated_;

        if (filterPeer_ && peer_.valid() && !(got->from == peer_)) {
            // Cudzy datagram. Odrzucamy i czekamy dalej — zwrócenie błędu
            // kazałoby wołającemu odróżniać „śmieć" od „pustki", a to jest
            // dokładnie ta decyzja, którą ta klasa ma z niego zdjąć.
            ++rejected_;
            if (rejected_ == 1) {
                HYDRA_LOGW("odrzucono datagram spoza rozmowcy (port %u)",
                           static_cast<unsigned>(got->from.port));
            }
            if (timeoutMs == 0) return unexpected(Err::WouldBlock);
            continue;
        }

        return *got;
    }
}

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
