#pragma once
/**
 * Hydra — magistrala zdarzeń publish/subscribe (rozdz. 4.3).
 *
 * Moduły nie wołają się nawzajem bezpośrednio. Zdarzenia to lekkie POD-y
 * (maks. HYDRA_EVENT_MAX_SIZE bajtów) z identyfikatorem tematu; większe ładunki
 * przekazuje się wskaźnikiem z przeniesieniem własności.
 *
 * Dwa tryby dostarczania:
 *   Direct — callback w kontekście nadawcy (szybkie, wymaga świadomości),
 *   Queued — callback w kontekście taska subskrybenta przez Inbox (bezpieczne).
 *
 * Publikacja z ISR (publishFromIsr) nigdy nie woła callbacków — odkłada zdarzenie
 * do kolejki opróżnianej przez task core.house. Przerwania w Hydrze wyłącznie
 * zgłaszają zdarzenia, nigdy nie przetwarzają danych (rozdz. 10).
 */

#include <string.h>
#include <type_traits>

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {

/** Uchwyt subskrypcji zwracany przez subscribe(). */
using SubId = u16;
constexpr SubId kInvalidSub = 0xFFFF;

/** Przydziela kolejny identyfikator tematu. Wołane raz na typ zdarzenia. */
TopicId nextTopicId();

/**
 * Cechy typu zdarzenia. Identyfikator nadawany jest leniwie, przy pierwszym
 * użyciu typu — stały przez cały czas życia programu.
 * Nazwę tematu (dla mapowania MQTT i shella) nadaje makro HYDRA_DECLARE_EVENT.
 */
template <typename E>
struct EventTraits {
    static TopicId id() { static const TopicId v = nextTopicId(); return v; }
    static constexpr const char* name() { return "?"; }
};

/** Nadaje typowi zdarzenia czytelną nazwę tematu. Używać w zakresie globalnym. */
#define HYDRA_DECLARE_EVENT(Type, TopicName)                                   \
    namespace hydra {                                                          \
    template <>                                                                \
    struct EventTraits<Type> {                                                 \
        static TopicId id() { static const TopicId v = nextTopicId(); return v; } \
        static constexpr const char* name() { return TopicName; }              \
    };                                                                         \
    }

/** Wymagania stawiane każdemu zdarzeniu — sprawdzane w miejscu użycia. */
template <typename E>
struct EventContract {
    static_assert(std::is_trivially_copyable<E>::value,
                  "Zdarzenie musi być trywialnie kopiowalne (POD) — większe "
                  "ładunki przekazuj wskaźnikiem z przeniesieniem własności");
    static_assert(sizeof(E) <= HYDRA_EVENT_MAX_SIZE,
                  "Zdarzenie przekracza HYDRA_EVENT_MAX_SIZE");
    static constexpr bool value = true;
};

/**
 * Skrzynka odbiorcza taska. Subskrypcje założone z Inboxem dostarczają zdarzenia
 * do kolejki; callbacki wykonują się dopiero w pump(), czyli w kontekście
 * taska-właściciela skrzynki.
 */
class Inbox : NonCopyable {
public:
    Inbox() = default;
    ~Inbox();

    /** Alokuje kolejkę. Wyłącznie w fazie inicjalizacji (rozdz. 11). */
    Status create(u32 depth = 8);

    /**
     * Obsługuje zdarzenia ze skrzynki. Zwraca liczbę wykonanych callbacków.
     * timeoutMs = 0 → tylko to, co już czeka (poll).
     */
    u32 pump(u32 timeoutMs = 0);

    /** Liczba zdarzeń porzuconych z powodu pełnej kolejki. */
    u32 dropped() const { return dropped_; }
    bool valid() const { return q_.valid(); }

private:
    friend class EventBus;
    rtos::Queue q_;
    u32         dropped_ = 0;
};

class EventBus {
public:
    /** Handler po wymazaniu typu — rdzeń rzutuje ładunek na właściwy typ. */
    using RawHandler = Delegate<void(const void*)>;

    struct Stats {
        u32 published    = 0;  ///< publikacje z kontekstu taska
        u32 isrPublished = 0;  ///< publikacje odłożone z ISR
        u32 isrDropped   = 0;  ///< publikacje z ISR utracone (kolejka pełna)
        u32 queueDropped = 0;  ///< dostarczenia do Inboxów utracone
        u16 subs         = 0;  ///< aktywne subskrypcje
    };

    /** Tworzy kolejkę odroczonych publikacji z ISR. Wołane przez App::begin(). */
    static Status init();
    static void   shutdown();

    /** Subskrypcja w trybie Direct — callback w kontekście nadawcy. */
    template <typename E, typename F>
    static Result<SubId> subscribe(F&& fn) {
        static_assert(EventContract<E>::value, "");
        RawHandler h = [fn](const void* p) { fn(*static_cast<const E*>(p)); };
        return addSub(EventTraits<E>::id(), sizeof(E), h, nullptr);
    }

    /** Subskrypcja w trybie Queued — callback w kontekście taska właściciela skrzynki. */
    template <typename E, typename F>
    static Result<SubId> subscribe(Inbox& inbox, F&& fn) {
        static_assert(EventContract<E>::value, "");
        RawHandler h = [fn](const void* p) { fn(*static_cast<const E*>(p)); };
        return addSub(EventTraits<E>::id(), sizeof(E), h, &inbox);
    }

    static void unsubscribe(SubId id);

    /** Publikacja z kontekstu taska. Wołanie z ISR jest błędem — użyj publishFromIsr. */
    template <typename E>
    static void publish(const E& e) {
        static_assert(EventContract<E>::value, "");
        publishRaw(EventTraits<E>::id(), &e, sizeof(E));
    }

    /**
     * Publikacja z przerwania. Zdarzenie trafia do kolejki odroczonej i zostanie
     * rozgłoszone przez task core.house. Zwraca false, gdy kolejka jest pełna.
     */
    template <typename E>
    static bool publishFromIsr(const E& e) {
        static_assert(EventContract<E>::value, "");
        return publishRawFromIsr(EventTraits<E>::id(), &e, sizeof(E));
    }

    /** Opróżnia kolejkę odroczoną z ISR. Wołane przez task core.house. */
    static u32 drainIsr(u32 maxEvents = 16);

    static Stats stats();
    /** Zeruje stan magistrali. Wyłącznie do testów jednostkowych. */
    static void reset();

private:
    static Result<SubId> addSub(TopicId topic, u8 size, const RawHandler& h, Inbox* inbox);
    static void publishRaw(TopicId topic, const void* data, u8 size);
    static bool publishRawFromIsr(TopicId topic, const void* data, u8 size);
};

}  // namespace hydra
