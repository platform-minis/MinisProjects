/** Testy callbacków bez alokacji i sygnałów w stylu Qt (rozdz. 3). */

#include "hydra_test.hpp"

#include "hydra/core/Delegate.hpp"

using namespace hydra;

TEST("Delegate: pusty jest fałszywy, wypełniony prawdziwy") {
    Delegate<void()> d;
    CHECK(!static_cast<bool>(d));

    int counter = 0;
    d = [&counter] { ++counter; };
    CHECK(static_cast<bool>(d));
    d();
    d();
    CHECK_EQ(counter, 2);

    d.reset();
    CHECK(!static_cast<bool>(d));
}

TEST("Delegate: przenosi argumenty i zwraca wartość") {
    Delegate<int(int, int)> sum = [](int a, int b) { return a + b; };
    CHECK_EQ(sum(2, 3), 5);
}

TEST("Delegate: kopia zachowuje przechwycony stan") {
    int  value = 7;
    Delegate<int()> a = [value] { return value; };
    Delegate<int()> b = a;
    CHECK_EQ(b(), 7);

    // Kopia jest niezależna — modyfikacja oryginału nie zmienia kopii.
    a = [] { return 0; };
    CHECK_EQ(b(), 7);
    CHECK_EQ(a(), 0);
}

TEST("Signal: sloty wołane w kolejności podłączenia") {
    Signal<4, int> sig;
    int order[4] = {0, 0, 0, 0};
    int idx      = 0;

    auto h0 = sig.connect([&](int v) { order[idx++] = v * 1; });
    auto h1 = sig.connect([&](int v) { order[idx++] = v * 2; });
    CHECK(h0.has_value());
    CHECK(h1.has_value());
    CHECK_EQ(static_cast<int>(sig.slotCount()), 2);

    sig.emit(3);
    CHECK_EQ(order[0], 3);
    CHECK_EQ(order[1], 6);
}

TEST("Signal: rozłączenie zwalnia miejsce") {
    Signal<2, int> sig;
    int hits = 0;

    auto h0 = sig.connect([&](int) { ++hits; });
    auto h1 = sig.connect([&](int) { ++hits; });
    CHECK(h1.has_value());

    // Trzeci slot nie mieści się w pojemności — błąd zamiast alokacji.
    auto h2 = sig.connect([&](int) { ++hits; });
    CHECK(!h2.has_value());
    CHECK(h2.error() == Err::OutOfMemory);

    sig.disconnect(*h0);
    CHECK_EQ(static_cast<int>(sig.slotCount()), 1);
    sig.emit(1);
    CHECK_EQ(hits, 1);

    // Po zwolnieniu miejsca kolejne podłączenie znów się udaje.
    CHECK(sig.connect([&](int) { ++hits; }).has_value());
}
