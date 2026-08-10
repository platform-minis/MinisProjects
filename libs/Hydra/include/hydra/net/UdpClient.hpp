#pragma once
/**
 * @file UdpClient.hpp
 * @brief Klient UDP — datagramy do jednego rozmówcy.
 *
 * `IUdpSocket` jest surowym gniazdem: wysyła dokądkolwiek i przyjmuje od
 * kogokolwiek. Ta klasa dokłada do niego trzy rzeczy, których brak jest
 * najczęstszym źródłem błędów w kodzie na UDP:
 *
 * 1. **Pamięć rozmówcy.** Adres podaje się raz, nie przy każdym pakiecie.
 * 2. **Filtrowanie nadawcy.** Domyślnie odrzucamy datagramy przychodzące
 *    spoza ustawionego rozmówcy — patrz niżej, to nie jest wygoda.
 * 3. **Rozwiązywanie nazw.** `setPeer("pool.ntp.org", 123)` zamiast ręcznego
 *    składania adresu z oktetów.
 *
 * ## Dlaczego filtrowanie jest domyślne
 *
 * UDP nie ma połączenia, więc na port, z którego wysłaliśmy zapytanie, może
 * odpowiedzieć **ktokolwiek** — wystarczy, że zgadnie port i zdąży przed
 * prawdziwym serwerem. Na tym polega zatruwanie odpowiedzi DNS i podszywanie
 * się pod serwer czasu. Kod pisany „na szybko" czyta pierwszy datagram, jaki
 * przyjdzie, i ufa mu.
 *
 * Filtr nie zastępuje uwierzytelnienia — adres nadawcy da się sfałszować —
 * ale odsiewa przypadkowy ruch i wymusza świadomą decyzję tam, gdzie odpowiedzi
 * mają przychodzić od wielu stron. Do wykrywania urządzeń w sieci lokalnej
 * czy nasłuchu rozgłoszeń woła się `acceptFromAnyone()`, i wtedy widać
 * w kodzie, że to było zamierzone.
 *
 * ## Przykład: zapytanie i odpowiedź
 *
 *     UdpClient udp;
 *     HYDRA_CHECK(udp.begin(*net.createUdp()));
 *     HYDRA_CHECK(udp.setPeer(net, "192.168.0.10", 5005));
 *
 *     HYDRA_CHECK(udp.send(CByteSpan{payload, sizeof(payload)}));
 *
 *     u8 buf[512];
 *     auto got = udp.receive(ByteSpan{buf, sizeof(buf)}, 1000);
 *     if (got) HYDRA_LOGI("odpowiedź: %u B", (unsigned)got->size);
 *
 * ## Czego ta klasa nie robi
 *
 * Nie powtarza wysyłki, nie numeruje pakietów i nie porządkuje ich kolejności.
 * UDP tego nie ma i udawanie, że ma, byłoby gorsze niż brak: kto potrzebuje
 * gwarancji dostarczenia, powinien zobaczyć, że musi je sobie zbudować —
 * albo użyć TCP.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/net/ITransport.hpp"

namespace hydra {
namespace net {

class UdpClient {
public:
    UdpClient() = default;

    /**
     * Przejmuje gniazdo w użytkowanie i otwiera je.
     *
     * Gniazdo pozostaje własnością tego, kto je utworzył — zwykle interfejsu
     * sieciowego. Port lokalny 0 oznacza przydzielony przez system; port
     * podany wprost jest potrzebny, gdy druga strona ma się odzywać sama.
     */
    Status begin(IUdpSocket& socket, u16 localPort = 0);

    /** Zamyka gniazdo i zapomina rozmówcę. */
    void end();

    bool ready() const { return socket_ != nullptr && socket_->isOpen(); }
    u16  localPort() const { return socket_ != nullptr ? socket_->localPort() : 0; }

    // ── Rozmówca ───────────────────────────────────────────────────────────

    /** Ustawia rozmówcę po adresie. */
    Status setPeer(Endpoint peer);

    /** Ustawia rozmówcę po nazwie; rozwiązuje ją przez interfejs sieciowy. */
    Status setPeer(INetworkInterface& net, const char* host, u16 port);

    Endpoint peer() const { return peer_; }

    /**
     * Wyłącza filtr nadawcy.
     *
     * Potrzebne przy nasłuchu rozgłoszeń i wykrywaniu urządzeń, gdzie
     * odpowiedzi z definicji przychodzą z nieznanych adresów. Nazwa jest
     * długa celowo — ma być widać w kodzie, że filtr zdjęto świadomie.
     */
    void acceptFromAnyone() { filterPeer_ = false; }
    void acceptFromPeerOnly() { filterPeer_ = true; }

    // ── Wysyłanie ──────────────────────────────────────────────────────────

    /** Wysyła do ustawionego rozmówcy. */
    Status send(CByteSpan data);
    /** Wysyła pod wskazany adres, nie zmieniając ustawionego rozmówcy. */
    Status sendTo(Endpoint to, CByteSpan data);

    /**
     * Włącza rozgłaszanie i wysyła pod adres rozgłoszeniowy.
     *
     * Osobna metoda, bo rozgłoszenie trafia do każdego urządzenia w sieci
     * i nie powinno się wydarzyć przez pomyłkę w adresie.
     */
    Status broadcast(u16 port, CByteSpan data);

    // ── Odbiór ─────────────────────────────────────────────────────────────

    /**
     * Odbiera jeden datagram.
     *
     * @param timeoutMs 0 oznacza „sprawdź i wróć" — wtedy `Err::WouldBlock`,
     *                  gdy nic nie czeka.
     *
     * Datagramy spoza rozmówcy są przy włączonym filtrze **odrzucane po
     * cichu**, ale liczone przez `rejected()`. Zwracanie ich jako błędu
     * mieszałoby „przyszedł śmieć" z „nic nie przyszło".
     */
    Result<Datagram> receive(ByteSpan out, u32 timeoutMs = 0);

    /** Ile datagramów odrzucił filtr nadawcy. */
    u32 rejected() const { return rejected_; }
    /** Ile datagramów nie zmieściło się w buforze wołającego. */
    u32 truncated() const { return truncated_; }

    /** Największy datagram, jaki wolno wysłać. */
    size_t maxDatagram() const { return socket_ != nullptr ? socket_->maxDatagram() : 0; }

private:
    IUdpSocket* socket_ = nullptr;
    Endpoint    peer_{};
    bool        filterPeer_    = true;
    bool        broadcastOn_   = false;
    u32         rejected_      = 0;
    u32         truncated_     = 0;
};

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
