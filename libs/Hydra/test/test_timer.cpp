/**
 * Hydra — testy timera programowego (rozdz. 10).
 *
 * Timer istnieje po to, żeby rzadkie zdarzenia okresowe nie kosztowały
 * osobnego stosu. Testy pilnują zachowania, na którym opiera się to
 * zastosowanie: że okresowy wraca, że jednorazowy nie wraca, że zatrzymanie
 * działa natychmiast, i że przezbrojenie liczy okres od nowa.
 *
 * Testy czasowe są z natury chwiejne, więc zamiast dokładnej liczby tyknięć
 * sprawdzamy przedziały z zapasem na obciążoną maszynę CI. Za wąskie widełki
 * dałyby test, który pada losowo — a taki przestaje być czytany.
 */

#include <atomic>

#include "hydra/core/Rtos.hpp"
#include "hydra_test.hpp"

using namespace hydra;

namespace {

std::atomic<int> gTicks{0};
std::atomic<void*> gLastArg{nullptr};

void onTick(void* arg) {
    gTicks.fetch_add(1);
    gLastArg.store(arg);
}

/** Zeruje liczniki przed każdym przypadkiem — timery żyją między testami. */
void reset() {
    gTicks.store(0);
    gLastArg.store(nullptr);
}

}  // namespace

TEST("Timer: odrzuca błędne parametry") {
    rtos::Timer t;
    CHECK(!(static_cast<bool>(t.create("zly", 0, true, onTick, nullptr))));
    CHECK(!(static_cast<bool>(t.create("zly", 10, true, nullptr, nullptr))));
    CHECK(!(t.valid()));

    // Operacje na nieutworzonym timerze nie mogą się wywracać ani kłamać.
    CHECK(!(t.start()));
    CHECK(!(t.stop()));
    CHECK(!(t.running()));
}

TEST("Timer: okresowy wraca i przekazuje argument") {
    reset();
    int marker = 42;

    rtos::Timer t;
    CHECK(static_cast<bool>(t.create("okresowy", 20, true, onTick, &marker)));
    CHECK(t.start());

    rtos::delayMs(130);
    const int ticks = gTicks.load();

    // Przy okresie 20 ms w 130 ms mieści się 6 tyknięć; szeroki przedział,
    // bo maszyna CI potrafi zgubić kilka wybudzeń.
    CHECK(ticks >= 3);
    CHECK(ticks <= 9);
    CHECK(gLastArg.load() == &marker);
}

TEST("Timer: stop zatrzymuje natychmiast") {
    reset();

    rtos::Timer t;
    CHECK(static_cast<bool>(t.create("stop", 20, true, onTick, nullptr)));
    CHECK(t.start());
    rtos::delayMs(70);

    CHECK(t.stop());
    const int afterStop = gTicks.load();

    // Kluczowe: zatrzymanie nie czeka na koniec bieżącego okresu.
    rtos::delayMs(80);
    CHECK_EQ(gTicks.load(), afterStop);
    CHECK(!(t.running()));
}

TEST("Timer: jednorazowy strzela raz") {
    reset();

    rtos::Timer t;
    CHECK(static_cast<bool>(t.create("raz", 20, false, onTick, nullptr)));
    CHECK(t.start());

    rtos::delayMs(100);
    CHECK_EQ(gTicks.load(), 1);

    // Po wystrzeleniu timer jednorazowy sam się rozbraja — inaczej `running()`
    // kłamałoby i nie dałoby się odróżnić „czeka" od „już było".
    CHECK(!(t.running()));

    // …i daje się uzbroić ponownie.
    CHECK(t.start());
    rtos::delayMs(60);
    CHECK_EQ(gTicks.load(), 2);
}

TEST("Timer: przezbrojenie liczy okres od nowa") {
    reset();

    rtos::Timer t;
    CHECK(static_cast<bool>(t.create("przezbroj", 120, false, onTick, nullptr)));
    CHECK(t.start());

    // Przezbrojenie tuż przed wystrzeleniem musi odsunąć je o pełny okres,
    // a nie zignorować polecenia ani wystrzelić natychmiast.
    //
    // Okresy są tu dłuższe niż w pozostałych przypadkach celowo: sprawdzamy
    // brak zdarzenia w danym oknie, a to jedyny rodzaj asercji, który psuje
    // się od opóźnienia. Przy 60 ms margines wynosił 20 ms i test potrafił
    // paść na maszynie zajętej kompilacją.
    rtos::delayMs(70);
    CHECK(t.start());          // odsuwa wystrzelenie na ~190 ms od startu
    rtos::delayMs(70);
    CHECK_EQ(gTicks.load(), 0);

    rtos::delayMs(120);
    CHECK_EQ(gTicks.load(), 1);
}

TEST("Timer: setPeriod zmienia tempo") {
    reset();

    rtos::Timer t;
    CHECK(static_cast<bool>(t.create("tempo", 200, true, onTick, nullptr)));
    CHECK(t.start());

    // Okres 0 jest odrzucany — inaczej byłby cichym sposobem na zatrzymanie
    // timera, myląco różnym od `stop()`.
    CHECK(!(t.setPeriod(0)));

    CHECK(t.setPeriod(20));
    rtos::delayMs(110);
    CHECK(gTicks.load() >= 2);
}

TEST("Timer: destroy kończy pracę i da się powtórzyć") {
    reset();

    rtos::Timer t;
    CHECK(static_cast<bool>(t.create("koniec", 20, true, onTick, nullptr)));
    CHECK(t.start());
    rtos::delayMs(50);

    t.destroy();
    CHECK(!(t.valid()));
    const int afterDestroy = gTicks.load();

    rtos::delayMs(60);
    CHECK_EQ(gTicks.load(), afterDestroy);

    // Powtórne destroy() nie może się wywrócić — RAII wywoła je jeszcze raz
    // w destruktorze.
    t.destroy();

    // …a po zniszczeniu ten sam obiekt daje się utworzyć od nowa.
    CHECK(static_cast<bool>(t.create("znowu", 20, true, onTick, nullptr)));
    CHECK(t.valid());
}
