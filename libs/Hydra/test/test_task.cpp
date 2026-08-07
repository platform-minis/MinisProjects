/** Testy tasków okresowych i wykrywania naruszeń deadline'u (rozdz. 4.2, 9). */

#include "hydra_test.hpp"

#include <atomic>

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Task.hpp"

using namespace hydra;

TEST("Task: pętla okresowa trzyma zadany okres") {
    Log::reset();
    Task task;

    std::atomic<int> iterations{0};
    Task::Cfg cfg;
    cfg.name = "test.periodic";
    cfg.prio = Prio::Normal;

    REQUIRE(task.startPeriodic(cfg, 10, [&] { ++iterations; }).has_value());
    CHECK(task.running());

    rtos::delayMs(105);
    CHECK(task.stopAndWait(500));
    CHECK(!task.running());

    // 105 ms / 10 ms ≈ 10 iteracji; tolerancja na szum schedulera hosta.
    const int n = iterations.load();
    CHECK(n >= 7);
    CHECK(n <= 14);
    CHECK_EQ(static_cast<int>(task.stats().iterations), n);
}

TEST("Task: zatrzymanie jest kooperatywne") {
    Task task;
    std::atomic<bool> inside{false};

    Task::Cfg cfg;
    cfg.name = "test.stop";
    REQUIRE(task.startPeriodic(cfg, 5, [&] { inside = true; }).has_value());

    rtos::delayMs(20);
    CHECK(inside.load());

    const u32 t0 = rtos::nowMs();
    CHECK(task.stopAndWait(500));
    // Task kończy bieżącą iterację i wychodzi — bez zabijania z zewnątrz.
    CHECK(rtos::nowMs() - t0 < 200);
    CHECK(!task.running());
}

TEST("Task: przekroczenie okresu podnosi licznik i publikuje zdarzenie") {
    EventBus::reset();
    Log::reset();
    Log::init(LogLevel::Off, Log::Mode::Sync);

    int  reported = 0;
    u16  taskId   = 0;
    auto sub = EventBus::subscribe<TaskDeadlineMissed>([&](const TaskDeadlineMissed& e) {
        ++reported;
        taskId = e.taskId;
    });
    REQUIRE(sub.has_value());

    Task task;
    Task::Cfg cfg;
    cfg.name          = "test.slow";
    cfg.missThreshold = 2;

    // Ciało trwa 20 ms przy okresie 5 ms — każda iteracja spóźniona.
    REQUIRE(task.startPeriodic(cfg, 5, [] { rtos::delayMs(20); }).has_value());
    rtos::delayMs(150);
    CHECK(task.stopAndWait(500));

    const auto s = task.stats();
    CHECK(s.deadlineMisses > 0);
    CHECK(s.maxDurationUs >= 15000);
    CHECK(reported >= 1);
    CHECK_EQ(taskId, nameId("test.slow"));

    EventBus::reset();
}

TEST("Task: task punktualny nie zgłasza naruszeń") {
    EventBus::reset();
    Task task;
    Task::Cfg cfg;
    cfg.name = "test.ontime";

    REQUIRE(task.startPeriodic(cfg, 20, [] { rtos::delayMs(1); }).has_value());
    rtos::delayMs(120);
    CHECK(task.stopAndWait(500));

    CHECK_EQ(static_cast<int>(task.stats().deadlineMisses), 0);
    CHECK(task.stats().iterations >= 3);
}

TEST("Task: rejestr tasków widzi tylko żywe wpisy") {
    const u8 before = Task::registered();

    {
        Task task;
        Task::Cfg cfg;
        cfg.name = "test.registry";
        REQUIRE(task.startPeriodic(cfg, 10, [] {}).has_value());
        rtos::delayMs(15);

        CHECK_EQ(static_cast<int>(Task::registered()), before + 1);

        bool found = false;
        for (u8 i = 0; i < Task::registered(); ++i) {
            Task* t = Task::at(i);
            if (t && t->id() == nameId("test.registry")) found = true;
        }
        CHECK(found);
        CHECK(task.stopAndWait(500));
    }

    rtos::delayMs(10);
    CHECK_EQ(static_cast<int>(Task::registered()), before);
}

TEST("Task: błędne argumenty są odrzucane") {
    Task task;
    Task::Cfg cfg;
    cfg.name = "test.bad";

    auto zeroPeriod = task.startPeriodic(cfg, 0, [] {});
    CHECK(!zeroPeriod.has_value());
    CHECK(zeroPeriod.error() == Err::BadArgument);

    auto emptyBody = task.startPeriodic(cfg, 10, Task::Body{});
    CHECK(!emptyBody.has_value());
    CHECK(emptyBody.error() == Err::BadArgument);
    CHECK(!task.running());
}

TEST("Task: pętla zdarzeniowa kończy się na żądanie") {
    Task task;
    std::atomic<int> loops{0};

    Task::Cfg cfg;
    cfg.name = "test.eventloop";
    REQUIRE(task.startEventLoop(cfg, [&](Task& self) {
                while (!self.stopRequested()) {
                    ++loops;
                    rtos::delayMs(5);
                }
            }).has_value());

    rtos::delayMs(40);
    CHECK(task.stopAndWait(500));
    CHECK(loops.load() > 1);
    CHECK(!task.running());
}
