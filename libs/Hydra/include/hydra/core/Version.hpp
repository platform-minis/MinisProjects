#pragma once
/** Hydra — wersja frameworka (semantyczna, zgodna z mapą drogową z rozdz. 14). */

#define HYDRA_VERSION_MAJOR 1
#define HYDRA_VERSION_MINOR 0
#define HYDRA_VERSION_PATCH 0

/**
 * Napis składany z liczb, a nie wpisywany osobno. Dwa niezależne zapisy tej
 * samej wersji rozjeżdżają się przy pierwszym wydaniu, o którym ktoś zapomni,
 * a rozbieżność widać dopiero w logu urządzenia w terenie.
 */
#define HYDRA_STRINGIFY_IMPL(x) #x
#define HYDRA_STRINGIFY(x)      HYDRA_STRINGIFY_IMPL(x)
#define HYDRA_VERSION_STR                     \
    HYDRA_STRINGIFY(HYDRA_VERSION_MAJOR) "."  \
    HYDRA_STRINGIFY(HYDRA_VERSION_MINOR) "."  \
    HYDRA_STRINGIFY(HYDRA_VERSION_PATCH)

/** Wersja jako liczba — do porównań przy aktualizacji wsadu. */
#define HYDRA_VERSION_NUM \
    (HYDRA_VERSION_MAJOR * 10000 + HYDRA_VERSION_MINOR * 100 + HYDRA_VERSION_PATCH)

namespace hydra {
constexpr const char* version() { return HYDRA_VERSION_STR; }
}  // namespace hydra
