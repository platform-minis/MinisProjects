/** Hydra — implementacja shella diagnostycznego (rozdz. 13). */

#include "hydra/shell/Shell.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace hydra {
namespace shell {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

}  // namespace

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

void Output::write(const char* text) {
    if (!text || !sink_) return;
    const size_t length = strlen(text);
    sink_(text, length);
    written_ += length;
}

void Output::writeLine(const char* text) {
    write(text);
    write("\r\n");
}

void Output::printf(const char* format, ...) {
    if (!sink_) return;

    char    buffer[HYDRA_SHELL_LINE_MAX];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (n <= 0) return;
    const size_t length = static_cast<size_t>(n) < sizeof(buffer) ? static_cast<size_t>(n)
                                                                 : sizeof(buffer) - 1;
    sink_(buffer, length);
    written_ += length;
}

void Output::field(const char* key, const char* value) {
    printf("%s=%s\r\n", key, value ? value : "");
}

void Output::field(const char* key, i32 value) {
    printf("%s=%ld\r\n", key, static_cast<long>(value));
}

void Output::field(const char* key, u32 value) {
    printf("%s=%lu\r\n", key, static_cast<unsigned long>(value));
}

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

Status Shell::add(const char* name, const char* help, Handler handler) {
    if (!name || name[0] == '\0' || !handler) return fail(Err::BadArgument);
    if (find(name)) return fail(Err::AlreadyExists);
    if (count_ >= HYDRA_SHELL_MAX_COMMANDS) return fail(Err::OutOfMemory);

    commands_[count_].name    = name;
    commands_[count_].help    = help ? help : "";
    commands_[count_].handler = handler;
    ++count_;
    return ok();
}

const Shell::Command* Shell::command(u8 index) const {
    return index < count_ ? &commands_[index] : nullptr;
}

const Shell::Command* Shell::find(const char* name) const {
    if (!name) return nullptr;
    for (u8 i = 0; i < count_; ++i) {
        if (strcmp(commands_[i].name, name) == 0) return &commands_[i];
    }
    return nullptr;
}

Status Shell::execute(const char* line) {
    if (!line) return fail(Err::BadArgument);

    // Rozbiór na miejscu, w kopii roboczej: handler dostaje wskaźniki
    // do wnętrza bufora, więc nic nie jest kopiowane po raz drugi.
    char working[HYDRA_SHELL_LINE_MAX];
    strncpy(working, line, sizeof(working) - 1);
    working[sizeof(working) - 1] = '\0';

    char* argv[HYDRA_SHELL_MAX_ARGS] = {};
    int   argc = 0;

    char* cursor = working;
    while (*cursor && argc < HYDRA_SHELL_MAX_ARGS) {
        while (isSpace(*cursor)) ++cursor;
        if (*cursor == '\0') break;

        argv[argc++] = cursor;
        while (*cursor && !isSpace(*cursor)) ++cursor;
        if (*cursor) *cursor++ = '\0';
    }

    if (argc == 0) return ok();  // pusty wiersz nie jest błędem

    Output output = makeOutput();
    const Command* cmd = find(argv[0]);
    if (!cmd) {
        ++stats_.unknown;
        output.printf("nieznana komenda: %s (spróbuj 'help')\r\n", argv[0]);
        return fail(Err::NotFound);
    }

    ++stats_.executed;
    const Status result = cmd->handler(argc, argv, output);
    if (!result) {
        ++stats_.failed;
        output.printf("błąd: %s\r\n", toString(result.error()));
    }
    return result;
}

void Shell::clearLine() {
    length_    = 0;
    line_[0]   = '\0';
    truncated_ = false;
}

void Shell::feed(char c) {
    if (c == '\r' || c == '\n') {
        if (truncated_) {
            ++stats_.overflow;
            Output output = makeOutput();
            output.writeLine("wiersz za długi");
            clearLine();
            prompt();
            return;
        }
        line_[length_] = '\0';
        // Kopia wiersza: handler może wywołać feed() pośrednio, a wtedy
        // bufor edycji zmieniłby się w trakcie własnego rozbioru.
        char snapshot[HYDRA_SHELL_LINE_MAX];
        strncpy(snapshot, line_, sizeof(snapshot) - 1);
        snapshot[sizeof(snapshot) - 1] = '\0';

        clearLine();
        execute(snapshot);
        prompt();
        return;
    }

    if (c == 0x08 || c == 0x7F) {  // backspace albo delete
        if (length_ > 0) {
            --length_;
            line_[length_] = '\0';
            Output output = makeOutput();
            // Cofnięcie kursora, zamazanie znaku, ponowne cofnięcie —
            // tak terminal kasuje znak bez przerysowania całego wiersza.
            output.write("\b \b");
        }
        return;
    }

    // Znaki sterujące poza obsłużonymi wyżej nie mają w wierszu czego szukać.
    if (c < 0x20 || c == 0x7F) return;

    if (length_ + 1 >= sizeof(line_)) {
        truncated_ = true;
        return;
    }
    line_[length_++] = c;
    line_[length_]   = '\0';
}

void Shell::feed(const char* text) {
    if (!text) return;
    for (const char* p = text; *p; ++p) feed(*p);
}

void Shell::prompt() {
    Output output = makeOutput();
    output.write("hydra> ");
}

}  // namespace shell
}  // namespace hydra
