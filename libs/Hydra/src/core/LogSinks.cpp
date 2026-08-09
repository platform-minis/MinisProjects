/** Hydra — implementacja odbiorników logów. */

#include "hydra/core/LogSinks.hpp"

#include "hydra/hal/Hal.hpp"

#if HYDRA_PLAT_HOST
#  include <stdio.h>
#endif

namespace hydra {

void UartLogSink::write(LogLevel, const char* line, size_t len) {
    auto& port = hal::Hal::uart(index_);
    port.write(CByteSpan{reinterpret_cast<const u8*>(line), len});

    static const u8 kEol[2] = {'\r', '\n'};
    port.write(CByteSpan{kEol, sizeof(kEol)});
}

#if HYDRA_PLAT_HOST

void StdoutLogSink::write(LogLevel, const char* line, size_t len) {
    // Jedno wywołanie na wiersz. Rozbicie na treść i znak końca dawało
    // przeplot wierszy z różnych tasków — na urządzeniu port szeregowy jest
    // jeden i sam to szereguje, tutaj pisze kilka wątków naraz.
    fprintf(stderr, "%.*s\n", static_cast<int>(len), line);
}

#endif

}  // namespace hydra
