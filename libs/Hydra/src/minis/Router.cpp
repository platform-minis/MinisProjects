/** Hydra — implementacja trasowania ramek MyCastle. */

#include "hydra/minis/Router.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("minis.route")

namespace hydra {
namespace minis {

Result<LinkId> Router::addLink(ILink& link) {
    if (linkCount_ >= HYDRA_MINIS_MAX_LINKS) return unexpected(Err::OutOfRange);

    const LinkId id = linkCount_++;
    links_[id] = &link;
    link.setId(id);
    link.setReceiver([this](const Frame& frame) { onFrame(frame, now_); });

    // Pierwsze łącze prowadzące do serwera staje się trasą domyślną. Bez tego
    // najczęstszy przypadek — jedno łącze, jeden węzeł — wymagałby jawnego
    // setUplink() i milczał, gdyby ktoś o nim zapomniał.
    if (uplink_ == kNoLink && link.isUplink()) uplink_ = id;

    return id;
}

ILink* Router::link(LinkId id) const {
    return id < linkCount_ ? links_[id] : nullptr;
}

Status Router::addRoute(const DeviceAddr& addr, LinkId target) {
    if (!addr.valid() || target >= linkCount_) return fail(Err::BadArgument);

    for (auto& route : routes_) {
        if (route.used && route.addr.equals(addr)) {
            route.link   = target;
            route.stat1c = true;
            return ok();
        }
    }
    for (auto& route : routes_) {
        if (!route.used) {
            route.addr   = addr;
            route.link   = target;
            route.stat1c = true;
            route.used   = true;
            return ok();
        }
    }
    return fail(Err::OutOfMemory);
}

LinkId Router::routeFor(const DeviceAddr& addr) const {
    for (const auto& route : routes_) {
        if (route.used && route.addr.equals(addr)) return route.link;
    }
    return kNoLink;
}

u16 Router::routeCount() const {
    u16 count = 0;
    for (const auto& route : routes_) if (route.used) ++count;
    return count;
}

bool Router::online() const {
    const ILink* up = link(uplink_);
    return up != nullptr && up->up();
}

u16 Router::linksUpMask() const {
    u16 mask = 0;
    for (u8 i = 0; i < linkCount_; ++i) {
        if (links_[i] != nullptr && links_[i]->up()) mask = static_cast<u16>(mask | (1u << i));
    }
    return mask;
}

// ---------------------------------------------------------------------------

Status Router::send(const Frame& frame, Millis now) {
    now_ = now;

    Frame outgoing = frame;
    // Ramka od aplikacji nie przyszła znikąd — `ingress` musi zostać pusty,
    // inaczej router uznałby jedno z łączy za zakazane i sam sobie zamknął
    // jedyną drogę wyjścia.
    outgoing.ingress = kNoLink;
    outgoing.hops    = 0;
    ++stats_.sent;

    const LinkId target = routeFor(outgoing.addr);
    const LinkId chosen = (target != kNoLink) ? target : uplink_;

    if (chosen == kNoLink) {
        drop(outgoing, DropReason::NoRoute);
        return fail(Err::NotFound);
    }
    return deliverTo(chosen, outgoing);
}

void Router::poll(Millis now) {
    now_ = now;
    for (u8 i = 0; i < linkCount_; ++i) {
        if (links_[i] != nullptr) links_[i]->poll(now);
    }

    if (cfg_.routeTtlMs == 0) return;
    for (auto& route : routes_) {
        // Trasy statyczne nie wygasają: skoro ktoś je wpisał, cisza urządzenia
        // jest informacją o urządzeniu, a nie o topologii.
        if (!route.used || route.stat1c) continue;
        if (now - route.seenAt > cfg_.routeTtlMs) route.used = false;
    }
}

// ---------------------------------------------------------------------------

void Router::onFrame(const Frame& frame, Millis now) {
    now_ = now;

    if (frame.hops > cfg_.maxHops) {
        drop(frame, DropReason::HopLimit);
        return;
    }

    learn(frame, now);

    if (local_.valid() && frame.addr.equals(local_)) {
        ++stats_.delivered;
        if (onLocal_) onLocal_(frame);
        return;
    }

    route(frame, now);
}

void Router::route(const Frame& frame, Millis now) {
    HYDRA_UNUSED(now);

    LinkId target = routeFor(frame.addr);

    // Nigdy z powrotem tam, skąd przyszło. To jest cała ochrona przed pętlą
    // przy dwóch bramkach na tej samej magistrali — tańsza i pewniejsza niż
    // pamiętanie identyfikatorów wiadomości.
    if (target == frame.ingress) target = kNoLink;
    if (target == kNoLink && uplink_ != frame.ingress) target = uplink_;

    if (target == kNoLink) {
        drop(frame, DropReason::NoRoute);
        return;
    }

    Frame forwarded = frame;
    forwarded.hops  = static_cast<u8>(frame.hops + 1);
    if (deliverTo(target, forwarded)) ++stats_.forwarded;
}

/**
 * Uczenie trasy z ruchu.
 *
 * Warunki są trzy i każdy odsiewa konkretny błąd:
 *
 *  • tylko ruch w górę — komenda z serwera do węzła przechodzi przez bramkę
 *    w dół i nie mówi, gdzie węzeł stoi;
 *  • tylko z łączy nieprowadzących do serwera — z łącza do serwera przychodzi
 *    ruch wszystkich urządzeń świata i nauka z niego dałaby trasę „każdy jest
 *    w internecie", czyli pętlę przy pierwszej odpowiedzi;
 *  • nigdy dla adresu własnego — inaczej urządzenie nauczyłoby się trasy do
 *    siebie samego i przestało dostarczać lokalnie.
 */
void Router::learn(const Frame& frame, Millis now) {
    if (!cfg_.learn) return;
    if (!flowsUpstream(frame.kind)) return;
    if (frame.ingress == kNoLink) return;
    if (local_.valid() && frame.addr.equals(local_)) return;

    const ILink* source = link(frame.ingress);
    if (source == nullptr || source->isUplink()) return;
    if (!frame.addr.valid()) return;

    for (auto& route : routes_) {
        if (route.used && route.addr.equals(frame.addr)) {
            route.seenAt = now;
            // Trasa statyczna wygrywa z obserwacją: ktoś ją wpisał świadomie,
            // a echo z sąsiedniej magistrali nie ma prawa jej przestawić.
            if (!route.stat1c) route.link = frame.ingress;
            return;
        }
    }

    for (u8 slot = 0; slot < HYDRA_MINIS_MAX_ROUTES; ++slot) {
        Route& route = routes_[slot];
        if (route.used) continue;

        route.addr   = frame.addr;
        route.link   = frame.ingress;
        route.stat1c = false;
        route.seenAt = now;
        route.used   = true;
        ++stats_.learned;

        HYDRA_LOGI("trasa: %s/%s przez %s", route.addr.user, route.addr.device,
                   source->name());

        // Łącze do serwera musi się dowiedzieć, że ma nasłuchiwać także dla
        // tego urządzenia — inaczej telemetria pójdzie w górę, a komenda
        // z serwera nigdy tu nie wróci.
        ILink* up = link(uplink_);
        if (up != nullptr) (void)up->observe(frame.addr);

        EventBus::publish(MinisRouteLearned{frame.ingress, slot, routeCount()});
        return;
    }

    // Tablica pełna. Zgłaszamy raz na ramkę, bo cisza wyglądałaby jak brak
    // urządzenia, a nie jak brak miejsca na jego zapamiętanie.
    HYDRA_LOGW("tablica tras pełna (%u) — %s/%s pozostaje nieznane",
               static_cast<unsigned>(HYDRA_MINIS_MAX_ROUTES),
               frame.addr.user, frame.addr.device);
}

Status Router::deliverTo(LinkId target, const Frame& frame) {
    ILink* out = link(target);
    if (out == nullptr) {
        drop(frame, DropReason::NoRoute);
        return fail(Err::NotFound);
    }
    if (!out->up()) {
        drop(frame, DropReason::LinkDown);
        return fail(Err::NotInitialized);
    }
    if (frame.payload.size() > out->mtu()) {
        drop(frame, DropReason::TooLarge);
        return fail(Err::OutOfRange);
    }

    Status result = out->send(frame);
    if (!result) drop(frame, DropReason::Busy);
    return result;
}

void Router::drop(const Frame& frame, DropReason reason) {
    ++stats_.dropped;
    HYDRA_LOGW("odrzucona %s do %s/%s: %s", toString(frame.kind),
               frame.addr.user, frame.addr.device, toString(reason));
    EventBus::publish(MinisDropped{reason, frame.ingress, frame.kind, stats_.dropped});
}

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
