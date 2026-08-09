#pragma once
/**
 * Hydra — łącze MyCastle po RS-485 albo RS-232.
 *
 * Zamienia strumień bajtów w ramki i z powrotem (patrz SerialCodec) oraz robi
 * jedną rzecz ponad to: **tłumaczy adresy**. Na magistrali węzły mają numery
 * jednobajtowe, w MyCastle — pary (użytkownik, urządzenie). Tablica przypisań
 * powstaje sama, z ramek niosących tożsamość.
 *
 * Dwie role, jeden kod:
 *
 *   • **węzeł** (`self` ≠ 0) — nadaje do bramki, dołącza tożsamość, dopóki
 *     nie usłyszy, że została przyjęta;
 *   • **bramka** (`self` = 0) — zbiera tożsamości, tłumaczy adresy w obie
 *     strony, dopytuje nieznane węzły ramką `Discover`.
 *
 * **Half-dupleks.** RS-485 ma jedną parę przewodów, więc nadajnik trzeba
 * włączyć przed wysłaniem i wyłączyć po. Wyłączenie za wcześnie ucina koniec
 * ramki, za późno — blokuje magistralę i węzeł odpowiada w ciszę. Dlatego
 * `send()` czeka na opróżnienie sprzętowego bufora (`flush()`) i dopiero potem
 * zwalnia linię. Na RS-232 pin sterujący jest po prostu nieustawiony
 * i cały ten mechanizm znika.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/hal/IBus.hpp"
#include "hydra/hal/IGpio.hpp"
#include "hydra/minis/ILink.hpp"
#include "hydra/minis/SerialCodec.hpp"

/** Ile węzłów bramka pamięta. */
#ifndef HYDRA_MINIS_MAX_NODES
#  define HYDRA_MINIS_MAX_NODES 16
#endif

namespace hydra {
namespace minis {

class SerialLink : public ILink {
public:
    struct Config {
        /** Numer tego urządzenia na magistrali. 0 = bramka. */
        NodeId self = kGatewayNode;
        /**
         * Pin sterujący nadajnikiem RS-485 (DE/RE zwarte razem).
         * `kNoPin` = RS-232 albo konwerter z automatycznym przełączaniem.
         */
        hal::PinNum dePin = hal::kNoPin;
        /** Czy nadajnik włącza stan wysoki. Prawie zawsze tak. */
        bool deActiveHigh = true;
        /**
         * Odczekanie po zwolnieniu linii, zanim wolno nadać ponownie.
         *
         * Magistrala z odbiciami potrzebuje chwili na uspokojenie się; bez
         * tego dwie ramki nadane pod rząd potrafią się skleić u odbiorcy
         * o gorszym terminatorze.
         */
        u16 turnaroundUs = 200;
        /** Co ile bramka dopytuje nieznany węzeł. 0 = nigdy. */
        u32 discoverEveryMs = 5000;
        /** Ile razy węzeł dołącza tożsamość, zanim uzna ją za znaną. */
        u8 identRepeats = 3;
    };

    struct Stats {
        u32 framesRx    = 0;
        u32 framesTx    = 0;
        u32 crcErrors   = 0;
        u32 resyncs     = 0;   ///< ile razy odbiornik gubił się i zaczynał od nowa
        u32 overruns    = 0;   ///< ramka dłuższa niż bufor odbiorczy
        u32 discoverTx  = 0;
    };

    SerialLink(hal::IUart& uart, hal::IGpio* gpio = nullptr)
        : uart_(uart), gpio_(gpio) {}

    Status configure(const Config& cfg);

    /** Adres MyCastle tego urządzenia — dołączany do ramek jako tożsamość. */
    void setIdentity(const DeviceAddr& addr) { identity_ = addr; identLeft_ = cfg_.identRepeats; }

    // --- ILink ---------------------------------------------------------------

    const char* name() const override { return cfg_.self == kGatewayNode ? "rs485-gw" : "rs485"; }
    Status begin() override;
    bool   up() const override { return started_; }
    size_t mtu() const override { return HYDRA_MINIS_SERIAL_MTU; }
    /**
     * Magistrala nie prowadzi do serwera nawet wtedy, gdy prowadzi do bramki,
     * która do niego prowadzi. Router uczy się tras wyłącznie z łączy
     * oznaczonych tak — i o to tu chodzi.
     */
    bool   isUplink() const override { return false; }
    Status send(const Frame& frame) override;
    void   poll(Millis now) override;

    Stats stats() const { return stats_; }
    /** Numer węzła, pod którym bramka zna ten adres; `kBroadcastNode` = nieznany. */
    NodeId nodeFor(const DeviceAddr& addr) const;
    u8     knownNodes() const;

private:
    struct NodeEntry {
        DeviceAddr addr;
        NodeId     node = kBroadcastNode;
        Millis     seenAt = 0;
        bool       used = false;
    };

    void   readBytes(Millis now);
    void   handleRaw(size_t length, Millis now);
    void   remember(const DeviceAddr& addr, NodeId node, Millis now);
    const DeviceAddr* addrFor(NodeId node) const;
    Status transmit(const SerialFrame& frame);
    void   driveEnable(bool on);
    void   maybeDiscover(NodeId node, Millis now);

    hal::IUart& uart_;
    hal::IGpio* gpio_ = nullptr;
    Config      cfg_{};
    bool        started_ = false;

    DeviceAddr identity_{};
    /** Ile jeszcze ramek ma nieść tożsamość. */
    u8 identLeft_ = 0;

    NodeEntry nodes_[HYDRA_MINIS_MAX_NODES];

    /** Bufor odbiorczy — jedna ramka w postaci zakodowanej. */
    u8     rx_[serialWorstCase(HYDRA_MINIS_SERIAL_MTU)];
    size_t rxLen_ = 0;
    /** Czy bieżąca ramka już się nie zmieści; czekamy na zero i zaczynamy od nowa. */
    bool   rxOverflow_ = false;

    u8 tx_[serialWorstCase(HYDRA_MINIS_SERIAL_MTU)];

    Millis lastDiscoverMs_ = 0;
    Stats  stats_{};
};

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
