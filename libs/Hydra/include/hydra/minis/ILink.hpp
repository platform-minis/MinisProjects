#pragma once
/**
 * Hydra — łącze przenoszące ramki MyCastle.
 *
 * Łącze wie o dwóch rzeczach: jak zakodować `Frame` w swoim medium i jak go
 * z niego odzyskać. Nie wie nic o encjach, telemetrii ani o tym, czy ramka
 * dotyczy tego urządzenia, czy sąsiedniego — od tego jest router.
 *
 * Ten podział jest powodem, dla którego bramka RS-485↔Ethernet ma w Hydrze
 * kilkanaście linii kodu aplikacji: to po prostu dwa łącza wpięte w jeden
 * router. Gdyby MQTT był wbudowany w moduł IoT, bramka byłaby osobnym
 * programem.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/minis/MinisTypes.hpp"

namespace hydra {
namespace minis {

class ILink {
public:
    /** Wołane dla każdej odebranej ramki; ustawia `ingress` na numer łącza. */
    using Receiver = Delegate<void(const Frame&)>;

    virtual ~ILink() = default;

    /** Nazwa do logów i diagnostyki: „mqtt", „rs485", „ws". */
    virtual const char* name() const = 0;

    virtual Status begin() = 0;
    virtual void   end() {}

    /** Czy łącze jest gotowe przyjąć ramkę. */
    virtual bool up() const = 0;

    /** Największy ładunek, jaki przejdzie. Router odrzuca większe z TooLarge. */
    virtual size_t mtu() const = 0;

    /**
     * Czy łącze prowadzi w stronę serwera.
     *
     * Rozróżnienie steruje uczeniem tras: adres widziany na łączu **nie**
     * prowadzącym do serwera należy do urządzenia za tym łączem. Adres
     * widziany na łączu do serwera nie mówi nic o topologii — serwer rozmawia
     * ze wszystkimi.
     */
    virtual bool isUplink() const = 0;

    virtual Status send(const Frame& frame) = 0;

    /** Jeden krok: odbiór, podtrzymanie, retransmisje. Bez blokowania. */
    virtual void poll(Millis now) = 0;

    /**
     * Prosi łącze o przyjmowanie ruchu adresowanego do innego urządzenia.
     *
     * Bez tego bramka przekaże telemetrię węzła w górę, ale komenda z serwera
     * do tego węzła nigdy do niej nie dotrze: na MQTT trzeba się jawnie
     * zapisać na temat, a bramka nie subskrybuje tematów cudzych urządzeń,
     * dopóki nie dowie się, że za nią stoją.
     *
     * Łącza, dla których pojęcie subskrypcji nie istnieje (szeregowe słyszy
     * wszystko na magistrali), zwracają `NotSupported` i to jest poprawna
     * odpowiedź, nie błąd.
     */
    virtual Status observe(const DeviceAddr& addr) {
        HYDRA_UNUSED(addr);
        return fail(Err::NotSupported);
    }

    void   setReceiver(Receiver receiver) { rx_ = receiver; }
    LinkId id() const { return id_; }
    void   setId(LinkId id) { id_ = id; }

protected:
    /** Wywoływane przez implementację po złożeniu ramki z medium. */
    void deliver(Frame& frame) {
        frame.ingress = id_;
        if (rx_) rx_(frame);
    }

    Receiver rx_{};
    LinkId   id_ = kNoLink;
};

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
