#pragma once
/**
 * Hydra — deklaratywne mapowanie tematów MQTT na magistralę zdarzeń (rozdz. 7.2).
 *
 * Zamiast pisać w każdym module kod „weź zdarzenie, sformatuj, opublikuj",
 * deklaruje się powiązanie raz:
 *
 *     bridge.publishOn<SysHeartbeat>("hydra/rover-01/sys", 0, false,
 *         [](const SysHeartbeat& e, char* out, size_t cap) {
 *             return snprintf(out, cap, "{\"uptime\":%lu}", (unsigned long)e.uptimeMs);
 *         });
 *
 *     bridge.subscribeTo<SetSpeed>("hydra/rover-01/cmd/speed",
 *         [](CByteSpan payload, SetSpeed& out) { return parseSpeed(payload, out); });
 *
 * Od tej chwili każde zdarzenie danego typu trafia na brokera, a każda
 * wiadomość z tematu staje się zdarzeniem — bez dalszego kodu.
 *
 * Formatery i parsery przyjmowane są jako zwykłe wskaźniki na funkcje, a nie
 * domknięcia: mostek trzyma je w tablicy o stałym rozmiarze, a wskaźnik na
 * funkcję ma rozmiar znany w czasie kompilacji i nie wymaga alokacji.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include <stddef.h>

#include "hydra/core/EventBus.hpp"
#include "hydra/net/MqttClient.hpp"

namespace hydra {
namespace net {

/** Liczba deklarowanych powiązań w każdą stronę. */
#ifndef HYDRA_BRIDGE_MAX_ROUTES
#  define HYDRA_BRIDGE_MAX_ROUTES 8
#endif
/** Bufor na sformatowany ładunek publikacji. */
#ifndef HYDRA_BRIDGE_PAYLOAD_MAX
#  define HYDRA_BRIDGE_PAYLOAD_MAX 128
#endif

class TelemetryBridge {
public:
    explicit TelemetryBridge(MqttClient& mqtt) : mqtt_(mqtt) {}
    ~TelemetryBridge();

    /**
     * Publikuje każde zdarzenie typu E na wskazany temat.
     * format zwraca liczbę zapisanych znaków albo wartość ujemną przy błędzie.
     */
    template <typename E>
    Status publishOn(const char* topic, u8 qos, bool retain,
                     int (*format)(const E&, char*, size_t)) {
        if (!topic || !format) return fail(Err::BadArgument);
        if (outCount_ >= HYDRA_BRIDGE_MAX_ROUTES) return fail(Err::OutOfMemory);

        const u8 index = outCount_;
        Outbound& route = outbound_[index];
        route.topic  = topic;
        route.qos    = qos;
        route.retain = retain;
        route.fn     = reinterpret_cast<void*>(format);
        route.shim   = &formatShim<E>;

        auto sub = EventBus::subscribe<E>([this, index](const E& e) { emit(index, &e); });
        if (!sub) return fail(sub.error());

        route.sub = *sub;
        ++outCount_;
        return ok();
    }

    /**
     * Zamienia wiadomości z tematu na zdarzenia typu E.
     * parse zwraca false, gdy ładunek jest nie do przyjęcia — wtedy zdarzenie
     * nie powstaje, a licznik odrzuconych rośnie.
     */
    template <typename E>
    Status subscribeTo(const char* filter, u8 qos, bool (*parse)(CByteSpan, E&)) {
        if (!filter || !parse) return fail(Err::BadArgument);
        if (inCount_ >= HYDRA_BRIDGE_MAX_ROUTES) return fail(Err::OutOfMemory);

        const u8 index  = inCount_;
        Inbound& route  = inbound_[index];
        route.filter    = filter;
        route.fn        = reinterpret_cast<void*>(parse);
        route.shim      = &parseShim<E>;

        auto r = mqtt_.subscribe(filter, qos,
                                 [this, index](const char*, CByteSpan payload) {
                                     ingest(index, payload);
                                 });
        if (!r) return r;

        ++inCount_;
        return ok();
    }

    struct Stats {
        u32 publishedOk   = 0;
        u32 publishFailed = 0;
        u32 ingested      = 0;
        u32 rejected      = 0;
    };

    Stats stats() const { return stats_; }
    u8    outboundCount() const { return outCount_; }
    u8    inboundCount() const { return inCount_; }

private:
    using FormatShim = int (*)(void* fn, const void* event, char* out, size_t cap);
    using ParseShim  = bool (*)(void* fn, CByteSpan payload);

    /** Przywraca typ zdarzenia i woła formater użytkownika. */
    template <typename E>
    static int formatShim(void* fn, const void* event, char* out, size_t cap) {
        auto f = reinterpret_cast<int (*)(const E&, char*, size_t)>(fn);
        return f(*static_cast<const E*>(event), out, cap);
    }

    /** Parsuje ładunek i publikuje gotowe zdarzenie na magistrali. */
    template <typename E>
    static bool parseShim(void* fn, CByteSpan payload) {
        auto f = reinterpret_cast<bool (*)(CByteSpan, E&)>(fn);
        E    event{};
        if (!f(payload, event)) return false;
        EventBus::publish(event);
        return true;
    }

    struct Outbound {
        const char* topic  = nullptr;
        void*       fn     = nullptr;
        FormatShim  shim   = nullptr;
        SubId       sub    = kInvalidSub;
        u8          qos    = 0;
        bool        retain = false;
    };

    struct Inbound {
        const char* filter = nullptr;
        void*       fn     = nullptr;
        ParseShim   shim   = nullptr;
    };

    void emit(u8 index, const void* event);
    void ingest(u8 index, CByteSpan payload);

    MqttClient& mqtt_;
    Outbound    outbound_[HYDRA_BRIDGE_MAX_ROUTES];
    Inbound     inbound_[HYDRA_BRIDGE_MAX_ROUTES];
    u8          outCount_ = 0;
    u8          inCount_  = 0;
    Stats       stats_{};
};

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
