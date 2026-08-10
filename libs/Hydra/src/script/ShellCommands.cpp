/**
 * Hydra — komenda `script` shella diagnostycznego (alias: `lua`).
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/ScriptCommands.hpp"

#include <stdio.h>
#include <string.h>

namespace hydra {
namespace script {

namespace {

/**
 * Cel komendy. Moduł jest opcjonalny: `script mem` i wykonywanie fragmentów
 * mają sens także wtedy, gdy silnik został otwarty ręcznie, bez modułu.
 */
struct Target {
    IScriptEngine* engine = nullptr;
    ScriptModule*  module = nullptr;
};

Target gTarget{};

/**
 * Skleja argumenty z powrotem w jeden wiersz.
 *
 * Shell rozbija wiersz na słowa po spacjach, a fragment Lua spacji potrzebuje.
 * Sklejanie pojedynczą spacją gubi ich oryginalną liczbę, co dla kodu Lua jest
 * bez znaczenia wszędzie poza wnętrzem literałów tekstowych — i to jest jedyne
 * ograniczenie tej komendy, które warto znać.
 */
void joinArgv(int argc, char** argv, int from, char* out, size_t cap) {
    size_t used = 0;
    out[0]      = '\0';
    for (int i = from; i < argc; ++i) {
        const size_t len = strlen(argv[i]);
        if (used > 0 && used + 1 < cap) out[used++] = ' ';
        const size_t fit = (used + len < cap) ? len : (cap > used + 1 ? cap - used - 1 : 0);
        memcpy(out + used, argv[i], fit);
        used += fit;
        out[used] = '\0';
        if (used + 1 >= cap) break;
    }
}

void printMemory(const IScriptEngine& engine, shell::Output& out) {
    const auto m = engine.memory();
    out.field("used", m.used);
    out.field("peak", m.peak);
    out.field("capacity", m.capacity);
    out.field("live-blocks", m.liveBlocks);
    out.field("free-blocks", m.freeBlocks);
    // Największy spójny wolny blok mówi o fragmentacji więcej niż suma wolnych
    // bajtów: to on decyduje, czy da się jeszcze przydzielić większą tabelę.
    out.field("largest-free", m.largestFree);
    out.field("failures", m.failures);
}

void printStats(const ScriptModule& module, shell::Output& out) {
    const auto s = module.stats();
    if (module.engine() != nullptr) out.field("engine", module.engine()->name());
    out.field("cycles", s.cycles);
    out.field("loop-runs", s.loopRuns);
    out.field("loop-preemptions", s.loopPreemptions);
    out.field("loop-errors", s.loopErrors);
    out.field("signals", s.signalsHandled);
    out.field("instructions", s.instructions);
    out.field("dropped-signals", droppedSignals());
    out.field("loop-stopped", module.loopStopped() ? "yes" : "no");
    out.field("state", toString(module.state()));
}

/** Wykonuje fragment, kierując wyjście skryptu do shella. */
Status runChunk(IScriptEngine& engine, const char* source, shell::Output& out) {
    setOutput([&out](const char* text, size_t len) {
        (void)len;
        out.writeLine(text);
    });

    auto result = engine.eval(source, "=shell");
    flushOutput();
    resetOutput();

    if (!result) {
        // Silnik binarny odmawia z zasady, a nie z powodu błędu w kodzie —
        // komunikat o „błędzie" wprowadzałby w błąd.
        out.writeLine(result.error() == Err::NotSupported
                          ? "silnik nie wykonuje fragmentow zrodlowych"
                          : engine.error());
    }
    return result;
}

Status cmdScript(int argc, char** argv, shell::Output& out) {
    IScriptEngine* engine = gTarget.engine;
    if (engine == nullptr || !engine->ready()) {
        out.writeLine("silnik skryptowy nie jest otwarty");
        return fail(Err::NotInitialized);
    }

    if (argc < 2) {
        out.writeLine("uzycie: script <kod> | script mem | script stat | script gc | script reload");
        return ok();
    }

    if (strcmp(argv[1], "mem") == 0) {
        printMemory(*engine, out);
        return ok();
    }

    if (strcmp(argv[1], "gc") == 0) {
        const u32 before = engine->memory().used;
        const u32 after  = engine->collect();
        out.field("before", before);
        out.field("after", after);
        out.field("freed", before > after ? before - after : 0u);
        return ok();
    }

    if (strcmp(argv[1], "stat") == 0) {
        if (gTarget.module == nullptr) {
            out.writeLine("brak modulu skryptowego");
            return fail(Err::NotSupported);
        }
        printStats(*gTarget.module, out);
        return ok();
    }

    if (strcmp(argv[1], "reload") == 0) {
        if (gTarget.module == nullptr) {
            out.writeLine("brak modulu skryptowego");
            return fail(Err::NotSupported);
        }
        auto result = gTarget.module->reload();
        // Po przeładowaniu silnik jest tym samym obiektem z nowym stanem
        // wewnętrznym, więc wskaźnik w celu komendy pozostaje ważny.
        if (!result) {
            out.writeLine(engine->error());
            return result;
        }
        out.writeLine("skrypt wczytany od nowa");
        return ok();
    }

    // Wszystko inne jest kodem źródłowym. Wiersz zaczynający się od `=` to
    // skrót na wypisanie wartości wyrażenia — konwencja znana z samodzielnego
    // `lua` i dlatego zapisana przez `print(...)`.
    char source[HYDRA_SHELL_LINE_MAX + 16];
    if (argv[1][0] == '=') {
        char expr[HYDRA_SHELL_LINE_MAX];
        joinArgv(argc, argv, 1, expr, sizeof(expr));
        snprintf(source, sizeof(source), "print(%s)", expr + 1);
    } else {
        joinArgv(argc, argv, 1, source, sizeof(source));
    }
    return runChunk(*engine, source, out);
}

constexpr const char* kHelp =
    "wykonaj kod skryptu; podpolecenia: mem, stat, gc, reload";

/** `lua` zostaje aliasem — komenda jest starsza niż podział na silniki. */
Status addCommands(shell::Shell& shell) {
    HYDRA_CHECK(shell.add("script", kHelp, cmdScript));
    return shell.add("lua", kHelp, cmdScript);
}

}  // namespace

Status registerScriptCommands(shell::Shell& shell, IScriptEngine& engine) {
    gTarget.engine = &engine;
    gTarget.module = nullptr;
    return addCommands(shell);
}

Status registerScriptCommands(shell::Shell& shell, ScriptModule& module) {
    auto status    = addCommands(shell);
    gTarget.engine = module.engine();
    gTarget.module = &module;
    return status;
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
