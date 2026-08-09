#pragma once
/**
 * Hydra — potok: graf elementów, pule i podział na domeny czasowe.
 *
 * **Domena** jest tym, czego GStreamer nie ma, a co na mikrokontrolerze
 * decyduje o wszystkim. To grupa elementów obsługiwana przez jeden task
 * o jednym priorytecie i jednym okresie. Wewnątrz domeny przekazanie bloku
 * jest zwykłym wywołaniem funkcji; przejście między domenami to kolejka
 * między wątkami.
 *
 *     domena „capture"  (Realtime, 1 ms)  I2sSource
 *     domena „dsp"      (Normal,   5 ms)  Gain → Mix → Encode
 *     domena „sink"     (Low,     20 ms)  FileWriter
 *
 * ESP-ADF daje każdemu elementowi własny task — pięć elementów to pięć stosów
 * po 4 kB, czyli 20 kB na samo czekanie. Tutaj tyle samo elementów mieści się
 * w dwóch domenach i dwóch stosach, a granice między nimi są tam, gdzie
 * naprawdę zmienia się priorytet.
 *
 * **Graf jest zamrażany.** `prepare()` przechodzi elementy w kolejności
 * rejestracji, uzgadnia formaty od źródła w dół, zbiera zapotrzebowanie na
 * pamięć i wiąże pule. Po tym topologia się nie zmienia — bo po `App::begin()`
 * nic się nie alokuje. Kolejność rejestracji **jest** kierunkiem przepływu,
 * tak samo jak kolejność modułów w `App` jest kolejnością uruchamiania.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MEDIA

#include "hydra/media/Element.hpp"

/** Ile elementów w jednym potoku. */
#ifndef HYDRA_MEDIA_MAX_ELEMENTS
#  define HYDRA_MEDIA_MAX_ELEMENTS 12
#endif

/** Ile pul buforów. Zwykle jedna na format przechodzący przez potok. */
#ifndef HYDRA_MEDIA_MAX_POOLS
#  define HYDRA_MEDIA_MAX_POOLS 4
#endif

/** Ile domen czasowych. */
#ifndef HYDRA_MEDIA_MAX_DOMAINS
#  define HYDRA_MEDIA_MAX_DOMAINS 4
#endif

namespace hydra {
namespace media {

/** Numer domeny; elementy bez wskazania trafiają do domeny 0. */
using DomainId = u8;
constexpr DomainId kDefaultDomain = 0;

class Pipeline : NonCopyable {
public:
    struct Stats {
        u32 steps      = 0;
        u32 faults     = 0;
        u32 blocksMoved = 0;
    };

    // --- budowa (przed prepare) ---------------------------------------------

    /** Dodaje element. Kolejność rejestracji jest kierunkiem przepływu. */
    Status add(Element& element, DomainId domain = kDefaultDomain);

    /** Łączy wyjście z wejściem. Oba elementy muszą być już dodane. */
    Status link(Element& from, u8 fromPad, Element& to, u8 toPad,
                OverflowPolicy policy = OverflowPolicy::DropOldest);

    /** Skrót dla najczęstszego przypadku: jedno wyjście, jedno wejście. */
    Status link(Element& from, Element& to,
                OverflowPolicy policy = OverflowPolicy::DropOldest) {
        return link(from, 0, to, 0, policy);
    }

    /**
     * Udostępnia pamięć na pulę.
     *
     * Bufor należy do wołającego i musi przeżyć potok. Pule tworzy się przed
     * `prepare()`, bo to `prepare()` sprawdza, czy wystarczą.
     */
    Status addPool(ByteSpan storage, u32 blockSize, u16 count, u8 alignment = 4);

    // --- przygotowanie i praca ----------------------------------------------

    /**
     * Uzgadnia formaty, sprawdza zapotrzebowanie na pamięć i przechodzi
     * w stan `Ready`. Zwraca pierwszy napotkany problem, a nie wszystkie —
     * kolejne i tak wynikają z pierwszego.
     */
    Status prepare();

    Status start();
    void   stop();
    Status pause();
    Status resume();

    /**
     * Jeden krok domeny. Woła `process()` elementów tej domeny w kolejności
     * rejestracji, czyli z prądem.
     *
     * Task domeny wygląda tak:
     *
     *     task.startPeriodic(cfg, 5, [] { pipeline.step(kDsp, clockUs()); });
     */
    void step(DomainId domain, u64 nowUs);

    /** Wszystkie domeny po kolei — dla potoku mieszczącego się w jednym tasku. */
    void stepAll(u64 nowUs);

    /** Pula, z której element ma brać bloki dla swojego wyjścia. */
    BlockPool* poolFor(const Element& element, u8 outPad);
    /** Pula o pojemności bloku co najmniej `bytes`; najciaśniejsza pasująca. */
    BlockPool* poolAtLeast(u32 bytes);
    BlockPool* pool(PoolId id);

    /** Zgłasza zakłócenie. Element woła to zamiast milczeć. */
    void raise(MediaFault fault, const Element& element, u8 pad);

    PipelineState state() const { return state_; }
    u8            elementCount() const { return count_; }
    Element*      element(u8 index) { return index < count_ ? entries_[index].element : nullptr; }
    Stats         stats() const { return stats_; }

private:
    struct Entry {
        Element* element = nullptr;
        DomainId domain  = kDefaultDomain;
        /** Pula przypisana wyjściu; `kNoPool` = element nie produkuje bloków. */
        PoolId   pool[HYDRA_MEDIA_MAX_PADS] = {kNoPool, kNoPool, kNoPool, kNoPool};
    };

    Status negotiateAll();
    Status bindPools();
    void   transition(PipelineState next);
    Entry* entryFor(const Element& element);

    Entry     entries_[HYDRA_MEDIA_MAX_ELEMENTS];
    u8        count_ = 0;

    BlockPool pools_[HYDRA_MEDIA_MAX_POOLS];
    u8        poolCount_ = 0;

    PipelineState state_ = PipelineState::Idle;
    Stats         stats_{};
    u32           faultCount_[5] = {};
};

}  // namespace media
}  // namespace hydra

#endif  // HYDRA_ENABLE_MEDIA
