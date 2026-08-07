/** Hydra — implementacja modułu shella (rozdz. 13). */

#include "hydra/shell/ShellModule.hpp"

#include "hydra/core/Log.hpp"
#include "hydra/core/Version.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("shell")

namespace hydra {
namespace shell {

Status ShellModule::configure(const Config& cfg) {
    if (cfg.pollMs == 0) return fail(Err::BadArgument);
    cfg_ = cfg;
    return ok();
}

Status ShellModule::onInit() {
    // Wyjście shella idzie na ten sam port, z którego czyta — inaczej
    // odpowiedzi trafiałyby gdzie indziej niż pytania.
    const u8 index = cfg_.uartIndex;
    shell_.setOutput([index](const char* text, size_t length) {
        hal::Hal::uart(index).write(
            CByteSpan{reinterpret_cast<const u8*>(text), length});
    });

    if (cfg_.builtinCommands) {
        HYDRA_CHECK(registerCoreCommands(shell_));
        HYDRA_CHECK(registerHalCommands(shell_));
    }
    return ok();
}

Status ShellModule::onStart() {
    if (cfg_.banner) {
        Output output(
            [this](const char* text, size_t length) {
                hal::Hal::uart(cfg_.uartIndex)
                    .write(CByteSpan{reinterpret_cast<const u8*>(text), length});
            });
        output.printf("\r\nHydra %s — wpisz 'help'\r\n", version());
        shell_.prompt();
    }

    Task::Cfg cfg;
    cfg.name = "shell";
    // Najniższy priorytet: diagnostyka nie może opóźnić pętli sterowania.
    cfg.prio = cfg_.priority;
    cfg.core = cfg_.core;
    return task_.startPeriodic(cfg, cfg_.pollMs, [this] { step(); });
}

void ShellModule::onStop() { task_.stopAndWait(); }

void ShellModule::step() {
    auto& port = hal::Hal::uart(cfg_.uartIndex);

    // Ograniczenie na przebieg: wklejony blok tekstu nie może zająć taska
    // na dowolnie długo.
    u8 buffer[32];
    for (u8 pass = 0; pass < 4 && port.available() > 0; ++pass) {
        const size_t read = port.read(ByteSpan{buffer, sizeof(buffer)});
        if (read == 0) break;

        bytesRead_ += static_cast<u32>(read);
        for (size_t i = 0; i < read; ++i) shell_.feed(static_cast<char>(buffer[i]));
    }
}

}  // namespace shell
}  // namespace hydra
