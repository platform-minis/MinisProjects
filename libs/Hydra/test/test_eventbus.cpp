/** Testy magistrali zdarzeń (rozdz. 4.3). */

#include "hydra_test.hpp"

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Events.hpp"

using namespace hydra;

namespace {

struct BatteryEvent {
    u8  percent;
    u16 mv;
};

struct LowPowerEvent {
    u8 reason;
};

struct TickEvent {
    u32 seq;
};

}  // namespace

HYDRA_DECLARE_EVENT(BatteryEvent,  "test/battery")
HYDRA_DECLARE_EVENT(LowPowerEvent, "test/lowpower")
HYDRA_DECLARE_EVENT(TickEvent,     "test/tick")

TEST("EventBus: każdy typ zdarzenia dostaje własny, stały temat") {
    const TopicId a1 = EventTraits<BatteryEvent>::id();
    const TopicId a2 = EventTraits<BatteryEvent>::id();
    const TopicId b  = EventTraits<LowPowerEvent>::id();

    CHECK_EQ(a1, a2);
    CHECK(a1 != b);
    CHECK(a1 != kInvalidTopic);
    CHECK_STR(EventTraits<BatteryEvent>::name(), "test/battery");
}

TEST("EventBus: subskrypcja Direct dostaje ładunek zdarzenia") {
    EventBus::reset();

    int received = 0;
    u8  percent  = 0;
    auto sub = EventBus::subscribe<BatteryEvent>([&](const BatteryEvent& e) {
        ++received;
        percent = e.percent;
    });
    REQUIRE(sub.has_value());

    EventBus::publish(BatteryEvent{42, 3900});
    CHECK_EQ(received, 1);
    CHECK_EQ(static_cast<int>(percent), 42);

    // Zdarzenie innego typu nie trafia do tej subskrypcji.
    EventBus::publish(LowPowerEvent{1});
    CHECK_EQ(received, 1);

    EventBus::unsubscribe(*sub);
    EventBus::publish(BatteryEvent{10, 3500});
    CHECK_EQ(received, 1);
}

TEST("EventBus: wielu subskrybentów tego samego tematu") {
    EventBus::reset();

    int a = 0, b = 0;
    auto s1 = EventBus::subscribe<TickEvent>([&](const TickEvent&) { ++a; });
    auto s2 = EventBus::subscribe<TickEvent>([&](const TickEvent&) { ++b; });
    REQUIRE(s1.has_value());
    REQUIRE(s2.has_value());

    EventBus::publish(TickEvent{1});
    CHECK_EQ(a, 1);
    CHECK_EQ(b, 1);

    EventBus::unsubscribe(*s1);
    EventBus::publish(TickEvent{2});
    CHECK_EQ(a, 1);
    CHECK_EQ(b, 2);
}

TEST("EventBus: subskrybent może publikować z wnętrza callbacku") {
    // Kanoniczny przykład z dokumentacji: BatteryEvent → LowPowerEvent.
    // Gdyby callbacki wołane były pod blokadą tablicy, byłoby to zakleszczenie.
    EventBus::reset();

    int low = 0;
    auto s1 = EventBus::subscribe<BatteryEvent>([](const BatteryEvent& e) {
        if (e.percent < 15) EventBus::publish(LowPowerEvent{e.percent});
    });
    auto s2 = EventBus::subscribe<LowPowerEvent>([&](const LowPowerEvent&) { ++low; });
    REQUIRE(s1.has_value());
    REQUIRE(s2.has_value());

    EventBus::publish(BatteryEvent{12, 3512});
    CHECK_EQ(low, 1);

    EventBus::publish(BatteryEvent{80, 4100});
    CHECK_EQ(low, 1);
}

TEST("EventBus: tryb Queued dostarcza dopiero przy pump()") {
    EventBus::reset();

    Inbox inbox;
    REQUIRE(inbox.create(4).has_value());

    int  handled = 0;
    u32  lastSeq = 0;
    auto sub = EventBus::subscribe<TickEvent>(inbox, [&](const TickEvent& e) {
        ++handled;
        lastSeq = e.seq;
    });
    REQUIRE(sub.has_value());

    EventBus::publish(TickEvent{7});
    // Callback jeszcze się nie wykonał — czeka w skrzynce subskrybenta.
    CHECK_EQ(handled, 0);

    CHECK_EQ(static_cast<int>(inbox.pump(0)), 1);
    CHECK_EQ(handled, 1);
    CHECK_EQ(static_cast<int>(lastSeq), 7);

    // Pusta skrzynka: pump nie blokuje i nie woła niczego.
    CHECK_EQ(static_cast<int>(inbox.pump(0)), 0);
}

TEST("EventBus: przepełniona skrzynka liczy porzucone zdarzenia") {
    EventBus::reset();

    Inbox inbox;
    REQUIRE(inbox.create(2).has_value());
    auto sub = EventBus::subscribe<TickEvent>(inbox, [](const TickEvent&) {});
    REQUIRE(sub.has_value());

    EventBus::publish(TickEvent{1});
    EventBus::publish(TickEvent{2});
    EventBus::publish(TickEvent{3});  // kolejka pełna

    CHECK_EQ(static_cast<int>(inbox.dropped()), 1);
    CHECK_EQ(static_cast<int>(EventBus::stats().queueDropped), 1);
    CHECK_EQ(static_cast<int>(inbox.pump(0)), 2);
}

TEST("EventBus: publikacja z ISR jest odraczana do drainIsr()") {
    EventBus::reset();
    REQUIRE(EventBus::init().has_value());

    int seen = 0;
    auto sub = EventBus::subscribe<TickEvent>([&](const TickEvent&) { ++seen; });
    REQUIRE(sub.has_value());

    CHECK(EventBus::publishFromIsr(TickEvent{1}));
    CHECK(EventBus::publishFromIsr(TickEvent{2}));
    // Przerwanie tylko zgłasza zdarzenie — nic się jeszcze nie wykonało.
    CHECK_EQ(seen, 0);

    CHECK_EQ(static_cast<int>(EventBus::drainIsr()), 2);
    CHECK_EQ(seen, 2);
    CHECK_EQ(static_cast<int>(EventBus::stats().isrPublished), 2);

    EventBus::shutdown();
}

TEST("EventBus: bez init() publikacja z ISR jest liczona jako utracona") {
    EventBus::reset();
    EventBus::shutdown();

    CHECK(!EventBus::publishFromIsr(TickEvent{1}));
    CHECK_EQ(static_cast<int>(EventBus::stats().isrDropped), 1);
}

TEST("EventBus: wyczerpanie tablicy subskrypcji zwraca błąd") {
    EventBus::reset();

    SubId ids[HYDRA_EVENT_MAX_SUBS];
    for (u16 i = 0; i < HYDRA_EVENT_MAX_SUBS; ++i) {
        auto s = EventBus::subscribe<TickEvent>([](const TickEvent&) {});
        REQUIRE(s.has_value());
        ids[i] = *s;
    }
    auto overflow = EventBus::subscribe<TickEvent>([](const TickEvent&) {});
    CHECK(!overflow.has_value());
    CHECK(overflow.error() == Err::OutOfMemory);

    EventBus::unsubscribe(ids[0]);
    CHECK(EventBus::subscribe<TickEvent>([](const TickEvent&) {}).has_value());
    EventBus::reset();
}

TEST("Zdarzenia systemowe mieszczą się w budżecie ładunku") {
    CHECK(sizeof(SysStarted) <= HYDRA_EVENT_MAX_SIZE);
    CHECK(sizeof(SysHeartbeat) <= HYDRA_EVENT_MAX_SIZE);
    CHECK(sizeof(ModuleStateChanged) <= HYDRA_EVENT_MAX_SIZE);
    CHECK(sizeof(TaskDeadlineMissed) <= HYDRA_EVENT_MAX_SIZE);

    // nameId jest stabilne i rozróżnia moduły
    CHECK_EQ(nameId("net"), nameId("net"));
    CHECK(nameId("net") != nameId("sense"));
}
