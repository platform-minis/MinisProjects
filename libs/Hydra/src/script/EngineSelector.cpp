#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/EngineSelector.hpp"

#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace script {

EngineChoice choose(const EngineInputs& in) {
    EngineChoice out;

    // Program tekstowy: żaden interpreter WebAssembly go nie przyjmie.
    if (!in.wantsWasm) {
        out.kind   = EngineKind::Lua;
        out.reason = "program w postaci zrodlowej — tylko Lua przyjmuje tekst";
        return out;
    }

    if (!in.wasm3Available && !in.wamrAvailable) {
        // Lepiej powiedzieć „nie ma czym", niż podstawić Lua i zostawić
        // wołającego z silnikiem, który odrzuci jego moduł przy ładowaniu.
        out.kind   = EngineKind::None;
        out.reason = "zaden interpreter WebAssembly nie jest wkompilowany";
        return out;
    }

    // PSRAM albo dużo RAM-u — reguła wskazuje WAMR.
    const bool wantsWamr = in.hasPsram || in.usableRamBytes >= kWamrRamThreshold;

    if (wantsWamr) {
        if (in.wamrAvailable) {
            out.kind   = EngineKind::Wamr;
            out.reason = in.hasPsram ? "PSRAM obecny — szybszy interpreter oplaca sie podwojnie"
                                     : "co najmniej 256 KB RAM — miejsce na cięższy interpreter";
            return out;
        }

        // Reguła wskazała silnik, którego nie ma w obrazie. Wybór jest
        // poprawny, ale nie jest tym, o który chodziło — i to musi być widać.
        out.kind     = EngineKind::Wasm3;
        out.fallback = true;
        out.reason   = "pamiec pozwala na WAMR, ale nie jest wkompilowany — zostaje wasm3";
        return out;
    }

    if (in.wasm3Available) {
        out.kind   = EngineKind::Wasm3;
        out.reason = "ponizej 256 KB RAM — miejsce ma tylko lekki interpreter";
        return out;
    }

    // Mało pamięci, a dostępny jest wyłącznie ten cięższy. Zgoda, ale
    // z ostrzeżeniem: uruchomi się albo nie, zależnie od rozmiaru programu.
    out.kind     = EngineKind::Wamr;
    out.fallback = true;
    out.reason   = "malo pamieci, a dostepny jest tylko WAMR — moze zabraknac miejsca";
    return out;
}

EngineChoice probe(bool wantsWasm) {
    EngineInputs in;
    in.usableRamBytes = rtos::freeHeapBytes();
    in.hasPsram       = HYDRA_HAS_PSRAM != 0;
    in.wantsWasm      = wantsWasm;
    return choose(in);
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
