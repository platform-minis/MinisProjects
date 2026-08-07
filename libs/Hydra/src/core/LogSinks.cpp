/** Hydra — implementacja odbiorników logów. */

#include "hydra/core/LogSinks.hpp"

#include "hydra/hal/Hal.hpp"

namespace hydra {

void UartLogSink::write(LogLevel, const char* line, size_t len) {
    auto& port = hal::Hal::uart(index_);
    port.write(CByteSpan{reinterpret_cast<const u8*>(line), len});

    static const u8 kEol[2] = {'\r', '\n'};
    port.write(CByteSpan{kEol, sizeof(kEol)});
}

}  // namespace hydra
