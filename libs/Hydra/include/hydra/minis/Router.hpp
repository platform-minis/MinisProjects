#pragma once
/**
 * Hydra — trasowanie ramek MyCastle między łączami.
 *
 * Router robi trzy rzeczy i żadnej więcej:
 *
 *  1. **Dostarcza lokalnie** to, co adresowane do tego urządzenia.
 *  2. **Przekazuje** resztę na łącze, za którym stoi adresat.
 *  3. **Uczy się**, za którym łączem kto stoi — z ruchu, nie z konfiguracji.
 *
 * Punkt trzeci jest tym, co odróżnia to od tablicy w pliku. Bramka
 * RS-485↔Ethernet po włączeniu nie wie nic; pierwszy `hello` z węzła numer 3
 * uczy ją, że `dev-iot3` jest na łączu szeregowym, i od tej chwili komendy
 * z serwera trafiają tam same. Dodanie czujnika do magistrali nie wymaga
 * niczyjej zgody ani wpisu w konfiguracji bramki.
 *
 * Uczymy się wyłącznie z ruchu **w górę** i wyłącznie na łączach, które nie
 * prowadzą do serwera. Ruch z serwera nie mówi nic o topologii — serwer
 * rozmawia ze wszystkimi i nauka z niego oznaczałaby trasę „wszyscy są
 * w internecie", czyli pętlę przy pierwszej komendzie.
 *
 * Ochrona przed pętlą jest dwustopniowa i celowo prymitywna: ramka nigdy nie
 * wraca na łącze, z którego przyszła, a licznik przeskoków ją ubija, gdyby
 * dwie bramki widziały się nawzajem. Pamiętanie identyfikatorów wiadomości
 * kosztowałoby RAM i nie łapałoby nic ponad to.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/minis/ILink.hpp"
#include "hydra/minis/MinisTypes.hpp"

/** Ile łączy naraz. Bramka używa dwóch, węzeł jednego. */
#ifndef HYDRA_MINIS_MAX_LINKS
#  define HYDRA_MINIS_MAX_LINKS 4
#endif

/** Ile urządzeń może stać za bramką. */
#ifndef HYDRA_MINIS_MAX_ROUTES
#  define HYDRA_MINIS_MAX_ROUTES 16
#endif

namespace hydra {
namespace minis {

class Router {
public:
    struct Config {
        /**
         * Limit przeskoków. Cztery to instalacja z trzema bramkami w szeregu
         * — więcej oznacza błąd w topologii, a nie potrzebę większego limitu.
         */
        u8 maxHops = 4;
        /** Czy budować tablicę tras z ruchu. Wyłączenie zostawia same statyczne. */
        bool learn = true;
        /**
         * Po jakim czasie ciszy zapomnieć o urządzeniu. 0 = nigdy.
         *
         * Wygasanie ma sens tylko wtedy, gdy węzły bywają przepinane między
         * magistralami. W typowej instalacji przepięcia nie ma, a zapominanie
         * trasy oznaczałoby utratę komend do węzła, który akurat milczy.
         */
        u32 routeTtlMs = 0;
    };

    struct Stats {
        u32 delivered = 0;   ///< dostarczone lokalnie
        u32 forwarded = 0;   ///< przekazane dalej
        u32 dropped   = 0;
        u32 learned   = 0;
        u32 sent      = 0;   ///< nadane przez to urządzenie
    };

    /** Wołane dla ramek adresowanych do tego urządzenia. */
    using LocalHandler = Delegate<void(const Frame&)>;

    /**
     * Rejestruje łącze i przydziela mu numer.
     *
     * Kolejność rejestracji jest kolejnością numerów — tak samo jak kolejność
     * modułów w App. Zwraca `OutOfRange` po przekroczeniu HYDRA_MINIS_MAX_LINKS.
     */
    Result<LinkId> addLink(ILink& link);

    /** Adres tego urządzenia. Ramki do niego idą do `LocalHandler`. */
    void setLocal(const DeviceAddr& addr) { local_ = addr; }
    const DeviceAddr& local() const { return local_; }

    void setLocalHandler(LocalHandler handler) { onLocal_ = handler; }
    void configure(const Config& cfg) { cfg_ = cfg; }

    /**
     * Trasa domyślna — dokąd iść, gdy adresat nie jest znany.
     *
     * Dla węzła to jedyne łącze, dla bramki — to w stronę serwera. Ruch
     * w dół bez trasy domyślnej po prostu przepada, i tak ma być: węzeł nie
     * zgaduje, gdzie mieszka nieznane urządzenie.
     */
    void setUplink(LinkId link) { uplink_ = link; }
    LinkId uplink() const { return uplink_; }

    /** Trasa wpisana ręcznie. Ma pierwszeństwo nad nauczoną i nie wygasa. */
    Status addRoute(const DeviceAddr& addr, LinkId link);

    /** Nadanie ramki pochodzącej z tego urządzenia. */
    Status send(const Frame& frame, Millis now);

    /** Jeden krok: odpytuje łącza i czyści przeterminowane trasy. */
    void poll(Millis now);

    /** Łącze, którym pójdzie ramka do tego adresu; `kNoLink` = nie wiadomo. */
    LinkId routeFor(const DeviceAddr& addr) const;

    u8     linkCount() const { return linkCount_; }
    ILink* link(LinkId id) const;
    u16    routeCount() const;
    Stats  stats() const { return stats_; }

    /** Czy jest droga do serwera. Podstawa zdarzenia MinisState. */
    bool online() const;

    /** Maska działających łączy — bit na numer łącza. */
    u16 linksUpMask() const;

private:
    struct Route {
        DeviceAddr addr;
        LinkId     link   = kNoLink;
        bool       stat1c = false;   ///< wpisana ręcznie: nie wygasa, nie ustępuje
        Millis     seenAt = 0;
        bool       used   = false;
    };

    /** Wspólna ścieżka dla ramek z łączy i z aplikacji. */
    void route(const Frame& frame, Millis now);
    void onFrame(const Frame& frame, Millis now);
    void learn(const Frame& frame, Millis now);
    void drop(const Frame& frame, DropReason reason);
    Status deliverTo(LinkId target, const Frame& frame);

    ILink* links_[HYDRA_MINIS_MAX_LINKS] = {};
    u8     linkCount_ = 0;

    Route  routes_[HYDRA_MINIS_MAX_ROUTES];

    DeviceAddr   local_{};
    LocalHandler onLocal_{};
    Config       cfg_{};
    LinkId       uplink_ = kNoLink;
    Stats        stats_{};

    /** Czas ostatniego kroku — potrzebny odbiorowi z łącza, który go nie zna. */
    Millis now_ = 0;
};

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
