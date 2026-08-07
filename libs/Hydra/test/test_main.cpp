/** Hydra — runner testów hostowych. */

#include "hydra_test.hpp"

#include <stdlib.h>

namespace hydratest {
namespace {

Case*       gHead        = nullptr;
Case*       gTail        = nullptr;
const char* gCurrent     = nullptr;
int         gChecks      = 0;
int         gFailures    = 0;
int         gCaseFailures = 0;

}  // namespace

void registerCase(Case* c) {
    if (!gHead) {
        gHead = gTail = c;
    } else {
        gTail->next = c;
        gTail       = c;
    }
}

void countCheck() { ++gChecks; }

void reportFailure(const char* expr, const char* file, int line, const char* detail) {
    ++gFailures;
    ++gCaseFailures;
    printf("  \033[31mFAIL\033[0m %s:%d\n        %s\n", file, line, expr);
    if (detail) printf("        %s\n", detail);
}

int runAll(const char* filter) {
    int cases = 0, failedCases = 0;

    for (Case* c = gHead; c; c = c->next) {
        if (filter && !strstr(c->name, filter)) continue;
        ++cases;
        gCurrent      = c->name;
        gCaseFailures = 0;
        printf("• %s\n", c->name);
        c->fn();
        if (gCaseFailures > 0) ++failedCases;
    }

    printf("\n%s %d przypadków, %d asercji, %d błędów\033[0m\n",
           gFailures == 0 ? "\033[32mOK:\033[0m" : "\033[31mBŁĄD:\033[0m", cases, gChecks,
           gFailures);
    return failedCases == 0 ? 0 : 1;
}

}  // namespace hydratest

int main(int argc, char** argv) {
    // Buforowanie liniowe: gdy test wywróci proces, wiadomo, na którym stanął.
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char* filter = (argc > 1) ? argv[1] : nullptr;
    if (filter) printf("filtr: \"%s\"\n\n", filter);
    return hydratest::runAll(filter);
}
