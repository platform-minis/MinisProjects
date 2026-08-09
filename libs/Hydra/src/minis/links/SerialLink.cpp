/** Hydra — implementacja łącza RS-485 / RS-232. */

#include "hydra/minis/links/SerialLink.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"

HYDRA_LOG_MODULE("minis.ser")

namespace hydra {
namespace minis {

Status SerialLink::configure(const Config& cfg) {
    if (started_) return fail(Err::Busy);
    cfg_ = cfg;
    if (identity_.valid()) identLeft_ = cfg_.identRepeats;
    return ok();
}

Status SerialLink::begin() {
    if (cfg_.dePin != hal::kNoPin) {
        if (gpio_ == nullptr) return fail(Err::NotInitialized);
        HYDRA_CHECK(gpio_->configure(cfg_.dePin, hal::PinMode::Output));
        driveEnable(false);   // domyślnie słuchamy — magistrala należy do wszystkich
    }
    started_ = true;
    rxLen_ = 0;
    rxOverflow_ = false;
    return ok();
}

void SerialLink::driveEnable(bool on) {
    if (cfg_.dePin == hal::kNoPin || gpio_ == nullptr) return;
    (void)gpio_->write(cfg_.dePin, cfg_.deActiveHigh ? on : !on);
}

// ---------------------------------------------------------------------------
// Tablica węzłów
// ---------------------------------------------------------------------------

void SerialLink::remember(const DeviceAddr& addr, NodeId node, Millis now) {
    if (!addr.valid() || node == kBroadcastNode) return;

    for (auto& entry : nodes_) {
        if (entry.used && entry.node == node) {
            // Numer węzła jest tożsamością na magistrali; zmiana adresu pod
            // tym samym numerem oznacza wymianę urządzenia, a nie błąd.
            entry.addr   = addr;
            entry.seenAt = now;
            return;
        }
    }
    for (auto& entry : nodes_) {
        if (!entry.used) {
            entry.addr   = addr;
            entry.node   = node;
            entry.seenAt = now;
            entry.used   = true;
            HYDRA_LOGI("węzeł %u = %s/%s", static_cast<unsigned>(node),
                       addr.user, addr.device);
            return;
        }
    }
    HYDRA_LOGW("tablica węzłów pełna — %s/%s pozostaje nieznane",
               addr.user, addr.device);
}

const DeviceAddr* SerialLink::addrFor(NodeId node) const {
    for (const auto& entry : nodes_) {
        if (entry.used && entry.node == node) return &entry.addr;
    }
    return nullptr;
}

NodeId SerialLink::nodeFor(const DeviceAddr& addr) const {
    for (const auto& entry : nodes_) {
        if (entry.used && entry.addr.equals(addr)) return entry.node;
    }
    return kBroadcastNode;
}

u8 SerialLink::knownNodes() const {
    u8 count = 0;
    for (const auto& entry : nodes_) if (entry.used) ++count;
    return count;
}

// ---------------------------------------------------------------------------
// Nadawanie
// ---------------------------------------------------------------------------

Status SerialLink::send(const Frame& frame) {
    if (!started_) return fail(Err::NotInitialized);

    SerialFrame out;
    out.kind    = frame.kind;
    out.src     = cfg_.self;
    out.hops    = frame.hops;
    out.payload = frame.payload;

    if (frame.extType[0] != '\0') {
        out.flags |= kFlagExt;
        for (size_t i = 0; i < kExtTypeMax; ++i) out.extType[i] = frame.extType[i];
    }

    if (cfg_.self == kGatewayNode) {
        // Bramka adresuje po numerze węzła. Nieznany adres idzie rozgłoszeniem:
        // ramka dotrze do wszystkich, a właściwy odbiorca rozpozna się po
        // tożsamości, którą wtedy dołączamy.
        out.dst = nodeFor(frame.addr);
        if (out.dst == kBroadcastNode) {
            out.flags |= kFlagIdent;
            out.addr = frame.addr;
        }
    } else {
        out.dst = kGatewayNode;
        // Węzeł przedstawia się przez pierwsze kilka ramek. Potem przestaje,
        // bo tożsamość to 20 bajtów przy ładunku rzędu czterdziestu — koszt,
        // którego nie ma powodu ponosić przy każdym pomiarze.
        if (identLeft_ > 0 && identity_.valid()) {
            out.flags |= kFlagIdent;
            out.addr = identity_;
            --identLeft_;
        }
    }

    return transmit(out);
}

Status SerialLink::transmit(const SerialFrame& frame) {
    auto encoded = encodeSerial(frame, ByteSpan{tx_, sizeof(tx_)});
    if (!encoded) return fail(encoded.error());

    driveEnable(true);
    const size_t written = uart_.write(CByteSpan{tx_, *encoded});
    // Bez tego nadajnik zostaje wyłączony w chwili, gdy dane siedzą jeszcze
    // w buforze sprzętowym — na magistralę wychodzi ramka bez końca.
    uart_.flush();
    driveEnable(false);

    if (cfg_.turnaroundUs > 0) {
        // Milisekunda to najmniejsza jednostka, jaką ma scheduler; przy
        // typowych 200 µs czekamy zaokrąglone w górę, bo za krótko jest gorsze
        // niż za długo.
        rtos::delayMs(1);
    }

    if (written != *encoded) return fail(Err::IoError);
    ++stats_.framesTx;
    return ok();
}

void SerialLink::maybeDiscover(NodeId node, Millis now) {
    if (cfg_.self != kGatewayNode || cfg_.discoverEveryMs == 0) return;
    if (now - lastDiscoverMs_ < cfg_.discoverEveryMs) return;
    lastDiscoverMs_ = now;

    SerialFrame ask;
    ask.kind  = MsgKind::Unknown;
    ask.src   = cfg_.self;
    ask.dst   = node;
    ask.flags = kFlagDiscover;
    if (transmit(ask)) ++stats_.discoverTx;
}

// ---------------------------------------------------------------------------
// Odbiór
// ---------------------------------------------------------------------------

void SerialLink::poll(Millis now) {
    if (!started_) return;
    readBytes(now);
}

void SerialLink::readBytes(Millis now) {
    u8 byte;
    while (uart_.available() > 0) {
        if (uart_.read(ByteSpan{&byte, 1}) != 1) return;

        if (byte != 0) {
            if (rxLen_ < sizeof(rx_)) {
                rx_[rxLen_++] = byte;
            } else if (!rxOverflow_) {
                // Zbyt długa ramka. Nie zerujemy licznika od razu: trzeba
                // dojść do końca bieżącej ramki, inaczej jej ogon zostałby
                // wzięty za początek następnej.
                rxOverflow_ = true;
                ++stats_.overruns;
            }
            continue;
        }

        // Zero = granica ramki.
        if (rxLen_ == 0) continue;             // podwójne zero albo cisza — nic do złożenia
        if (rxOverflow_) {
            rxLen_ = 0;
            rxOverflow_ = false;
            ++stats_.resyncs;
            continue;
        }

        const size_t length = rxLen_;
        rxLen_ = 0;
        handleRaw(length, now);
    }
}

void SerialLink::handleRaw(size_t length, Millis now) {
    SerialFrame in;
    // Dekodowanie w miejscu — `in.payload` wskaże we wnętrze rx_.
    if (auto r = decodeSerial(ByteSpan{rx_, length}, in); !r) {
        // Błąd sumy kontrolnej na magistrali jest zdarzeniem oczekiwanym,
        // nie awarią. Liczymy je, bo rosnący licznik to jedyny sygnał, że
        // terminator odpadł albo kabel biegnie wzdłuż falownika.
        ++stats_.crcErrors;
        ++stats_.resyncs;
        return;
    }
    ++stats_.framesRx;

    // Prośba o przedstawienie się — odpowiada tylko adresat.
    if (in.isDiscover()) {
        if (in.dst == cfg_.self || in.dst == kBroadcastNode) {
            identLeft_ = cfg_.identRepeats;
        }
        return;
    }

    // Ramka nie do nas i nie rozgłoszeniowa: na magistrali słychać wszystko,
    // ale przekazywać cudzą korespondencję to zadanie bramki, nie węzła.
    if (cfg_.self != kGatewayNode && in.dst != cfg_.self && in.dst != kBroadcastNode) return;

    if (in.hasIdent()) remember(in.addr, in.src, now);

    Frame frame;
    frame.kind    = in.kind;
    frame.hops    = in.hops;
    frame.payload = in.payload;
    for (size_t i = 0; i < kExtTypeMax; ++i) frame.extType[i] = in.extType[i];

    if (in.hasIdent()) {
        frame.addr = in.addr;
    } else if (const DeviceAddr* known = addrFor(in.src); known != nullptr) {
        frame.addr = *known;
    } else if (flowsUpstream(in.kind)) {
        // Ruch w górę od węzła, którego jeszcze nie znamy. Bez adresu router
        // nie ma czego trasować, więc pytamy — i porzucamy tę jedną ramkę.
        // Alternatywa (buforowanie do czasu odpowiedzi) oznaczałaby kolejkę
        // na urządzeniu, które ma jej najmniej.
        HYDRA_LOGD("ramka od nieznanego węzła %u — pytam o tożsamość",
                   static_cast<unsigned>(in.src));
        maybeDiscover(in.src, now);
        return;
    } else {
        // Ruch w dół bez tożsamości jest adresowany numerem węzła i trafił do
        // nas — czyli dotyczy nas.
        frame.addr = identity_;
    }

    deliver(frame);
}

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
