/**
 * Hydra — komendy rdzenia dla shella diagnostycznego (rozdz. 13).
 *
 * Zestaw odpowiada temu, po co w ogóle sięga się po shell na działającym
 * urządzeniu: co robi system (`ps`, `top`), co się wydarzyło (`log`),
 * jak długo działa (`uptime`) i jak go zrestartować (`reboot`).
 */

#include <stdlib.h>
#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/core/Version.hpp"
#include "hydra/shell/Shell.hpp"

namespace hydra {
namespace shell {
namespace {

Status cmdHelp(Shell& shell, int argc, char** argv, Output& out) {
    HYDRA_UNUSED(argc);
    HYDRA_UNUSED(argv);

    out.writeLine("dostępne komendy:");
    for (u8 i = 0; i < shell.count(); ++i) {
        const Shell::Command* cmd = shell.command(i);
        if (cmd) out.printf("  %-12s %s\r\n", cmd->name, cmd->help);
    }
    return ok();
}

/** Lista tasków wraz z zapasem stosu — odpowiednik `ps`. */
Status cmdPs(int argc, char** argv, Output& out) {
    HYDRA_UNUSED(argc);
    HYDRA_UNUSED(argv);

    out.printf("%-16s %8s %8s %10s %10s\r\n", "task", "okres", "iteracje", "spóźnienia",
               "stos[B]");
    for (u8 i = 0; i < Task::registered(); ++i) {
        Task* task = Task::at(i);
        if (!task) continue;

        const Task::Stats stats = task->stats();
        out.printf("%-16s %8lu %8lu %10lu %10lu\r\n", task->name(),
                   static_cast<unsigned long>(task->periodMs()),
                   static_cast<unsigned long>(stats.iterations),
                   static_cast<unsigned long>(stats.deadlineMisses),
                   static_cast<unsigned long>(stats.stackFreeBytes));
    }
    out.field("tasks", static_cast<u32>(Task::registered()));
    return ok();
}

/** Obciążenie i pamięć — odpowiednik `top`. */
Status cmdTop(int argc, char** argv, Output& out) {
    HYDRA_UNUSED(argc);
    HYDRA_UNUSED(argv);

    out.field("uptime_s", App::uptimeMs() / 1000u);
    out.field("heap_free", rtos::freeHeapBytes());
    out.field("tasks", static_cast<u32>(Task::registered()));

    // Najgorszy zapas stosu spośród wszystkich tasków to liczba, po której
    // najwcześniej widać zbliżające się przepełnienie.
    u32  worst = 0;
    bool any   = false;
    for (u8 i = 0; i < Task::registered(); ++i) {
        Task* task = Task::at(i);
        if (!task) continue;
        const u32 free = task->stats().stackFreeBytes;
        if (free == 0) continue;
        if (!any || free < worst) {
            worst = free;
            any   = true;
        }
    }
    out.field("stack_min", worst);

    const auto bus = EventBus::stats();
    out.field("events", bus.published);
    out.field("events_dropped", bus.queueDropped + bus.isrDropped);

    const auto logStats = Log::stats();
    out.field("log_dropped", logStats.ringDropped);
    return ok();
}

/** Zrzut bufora pierścieniowego logów. */
Status cmdLog(int argc, char** argv, Output& out) {
    if (argc > 1 && strcmp(argv[1], "level") == 0) {
        if (argc < 3) {
            out.field("level", toString(Log::level()));
            return ok();
        }
        struct {
            const char* name;
            LogLevel    value;
        } levels[] = {{"trace", LogLevel::Trace}, {"debug", LogLevel::Debug},
                      {"info", LogLevel::Info},   {"warn", LogLevel::Warn},
                      {"error", LogLevel::Error}, {"off", LogLevel::Off}};

        for (const auto& level : levels) {
            if (strcmp(argv[2], level.name) == 0) {
                Log::setLevel(level.value);
                out.field("level", level.name);
                return ok();
            }
        }
        return fail(Err::BadArgument);
    }

    // Bufor pierścieniowy trzyma ostatnie linie niezależnie od tego, czy
    // trafiły już na port — to on jest zrzucany po awarii.
    char   buffer[HYDRA_LOG_RING_SIZE + 64];
    const size_t written = Log::dump(buffer, sizeof(buffer));
    if (written == 0) {
        out.writeLine("(bufor logów pusty)");
        return ok();
    }
    out.write(buffer);
    return ok();
}

Status cmdUptime(int argc, char** argv, Output& out) {
    HYDRA_UNUSED(argc);
    HYDRA_UNUSED(argv);

    const u32 seconds = App::uptimeMs() / 1000u;
    out.printf("%lu dni %02lu:%02lu:%02lu\r\n", static_cast<unsigned long>(seconds / 86400),
               static_cast<unsigned long>((seconds % 86400) / 3600),
               static_cast<unsigned long>((seconds % 3600) / 60),
               static_cast<unsigned long>(seconds % 60));
    out.field("uptime_s", seconds);
    return ok();
}

Status cmdVersion(int argc, char** argv, Output& out) {
    HYDRA_UNUSED(argc);
    HYDRA_UNUSED(argv);

    out.field("hydra", version());
    out.field("platform", App::platform());
    out.field("device", App::deviceName());
    out.field("modules", static_cast<u32>(App::moduleCount()));
    for (u8 i = 0; i < App::moduleCount(); ++i) {
        IModule* module = App::module(i);
        if (module) out.printf("  %-10s %s\r\n", module->name(), toString(module->state()));
    }
    return ok();
}

/**
 * Moduł po nazwie.
 *
 * Nazwa, a nie indeks: numer w rejestrze zależy od kolejności rejestracji
 * w aplikacji, więc to samo polecenie znaczyłoby co innego w dwóch wsadach.
 */
IModule* findModule(const char* name) {
    for (u8 i = 0; i < App::moduleCount(); ++i) {
        IModule* module = App::module(i);
        if (module && strcmp(module->name(), name) == 0) return module;
    }
    return nullptr;
}

/**
 * Sterowanie modułami: `module [stop|start|restart <nazwa>]`.
 *
 * `IModule` obiecuje miękki restart pojedynczego podsystemu — restart sieci
 * bez restartu robota. Dotąd tę obietnicę dało się spełnić wyłącznie z kodu;
 * tutaj staje się poleceniem, więc sięga po nią też ktoś na drugim końcu
 * portu szeregowego i scenariusz testowy.
 */
Status cmdModule(int argc, char** argv, Output& out) {
    if (argc == 1) {
        out.field("modules", static_cast<u32>(App::moduleCount()));
        for (u8 i = 0; i < App::moduleCount(); ++i) {
            IModule* module = App::module(i);
            if (module) out.printf("  %-10s %s\r\n", module->name(), toString(module->state()));
        }
        return ok();
    }

    // Nazwa modułu jest wymagana przy każdym działaniu: „module stop" bez
    // wskazania, co zatrzymać, jest pomyłką, a nie poleceniem na wszystko.
    if (argc < 3) return fail(Err::BadArgument);

    const char* action = argv[1];
    IModule*    module = findModule(argv[2]);
    // Literówka w nazwie nie może wyglądać jak wykonane polecenie — to jedyny
    // sygnał, jaki dostaje sterujący urządzeniem zdalnie.
    if (!module) return fail(Err::NotFound);

    if (strcmp(action, "stop") == 0) {
        module->stop();
        out.field("module", module->name());
        out.field("state", toString(module->state()));
        return ok();
    }

    if (strcmp(action, "start") == 0 || strcmp(action, "restart") == 0) {
        // Restart to zatrzymanie i start, a nie osobna ścieżka: moduł ma jeden
        // kontrakt cyklu życia i to on decyduje, co znaczy ponowne wejście.
        if (strcmp(action, "restart") == 0) module->stop();

        const Status result = module->start();
        out.field("module", module->name());
        out.field("state", toString(module->state()));
        return result;
    }

    return fail(Err::BadArgument);
}

Status cmdReboot(int argc, char** argv, Output& out) {
    u8 delaySec = 0;
    if (argc > 1) {
        const long value = strtol(argv[1], nullptr, 10);
        if (value < 0 || value > 60) return fail(Err::OutOfRange);
        delaySec = static_cast<u8>(value);
    }

    out.printf("restart za %u s\r\n", static_cast<unsigned>(delaySec));
    // Shell nie restartuje urządzenia sam: zgłasza żądanie na magistralę,
    // a decyzję o momencie podejmuje warstwa, która wie, co jest w toku —
    // zapis do pamięci trwałej albo trwająca aktualizacja.
    EventBus::publish(RebootRequest{nameId("shell"), delaySec});
    return ok();
}

}  // namespace

Status registerCoreCommands(Shell& shell) {
    HYDRA_CHECK(shell.add("help", "lista komend",
                          [&shell](int argc, char** argv, Output& out) {
                              return cmdHelp(shell, argc, argv, out);
                          }));
    HYDRA_CHECK(shell.add("ps", "taski i zapas stosów", &cmdPs));
    HYDRA_CHECK(shell.add("top", "obciążenie i pamięć", &cmdTop));
    HYDRA_CHECK(shell.add("log", "zrzut logów; log level <poziom>", &cmdLog));
    HYDRA_CHECK(shell.add("uptime", "czas pracy", &cmdUptime));
    HYDRA_CHECK(shell.add("version", "wersja i stan modułów", &cmdVersion));
    HYDRA_CHECK(shell.add("module", "moduły; module [stop|start|restart <nazwa>]", &cmdModule));
    HYDRA_CHECK(shell.add("reboot", "restart; reboot [sekundy]", &cmdReboot));
    return ok();
}

}  // namespace shell
}  // namespace hydra
