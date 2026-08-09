/**
 * Hydra — przechwycenie wyjścia interpretera Lua.
 *
 * Makra `lua_writestring` i `lua_writestringerror` z `hydra_lua_conf.h`
 * rozwijają się do funkcji zdefiniowanych tutaj. Zadanie tej warstwy jest
 * jedno: scalić fragmenty w wiersze i oddać je odbiornikowi, bo Lua wypisuje
 * `print("a", "b")` jako pięć osobnych zapisów, a log i shell operują wierszami.
 */

#include "hydra/script/Output.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("lua")

namespace hydra {
namespace script {

namespace {

/**
 * Bufor składanego wiersza. Dłuższy wiersz jest oddawany w kawałkach —
 * lepiej urwać w niewygodnym miejscu, niż zgubić tekst albo przydzielić pamięć.
 */
#ifndef HYDRA_SCRIPT_LINE_MAX
#  define HYDRA_SCRIPT_LINE_MAX 160
#endif

char       gLine[HYDRA_SCRIPT_LINE_MAX];
size_t     gLen = 0;
OutputSink gSink{};

void emit(const char* text, size_t len) {
    if (gSink) {
        gSink(text, len);
    } else {
        HYDRA_LOGI("%s", text);
    }
}

void flushLine() {
    if (gLen == 0) return;
    gLine[gLen] = '\0';
    emit(gLine, gLen);
    gLen = 0;
}

}  // namespace

void setOutput(OutputSink sink) {
    flushLine();
    gSink = sink;
}

void resetOutput() {
    flushLine();
    gSink.reset();
}

void flushOutput() { flushLine(); }

}  // namespace script
}  // namespace hydra

// ---------------------------------------------------------------------------
// Punkty wejścia dla kodu Lua (linkowanie C)
// ---------------------------------------------------------------------------

extern "C" void hydraLuaWrite(const char* text, size_t len) {
    using namespace hydra::script;
    for (size_t i = 0; i < len; ++i) {
        const char c = text[i];
        if (c == '\n') {
            flushLine();
            continue;
        }
        if (c == '\r') continue;
        if (gLen + 1 >= sizeof(gLine)) flushLine();
        gLine[gLen++] = c;
    }
}

extern "C" void hydraLuaWriteError(const char* format, const char* arg) {
    // Kontrakt Lua: format zawiera dokładnie jedno %s. Składamy go u siebie,
    // żeby komunikat trafił do logu jako jeden wiersz na poziomie Error,
    // a nie rozsypał się po odbiorniku strumienia zwykłego.
    char line[HYDRA_SCRIPT_LINE_MAX];
    snprintf(line, sizeof(line), format, arg ? arg : "");

    // Lua kończy komunikaty własnym znakiem nowej linii — obcinamy go, bo
    // odbiornik dostaje wiersze bez terminatora.
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    if (n == 0) return;

    HYDRA_LOGE("%s", line);
}
