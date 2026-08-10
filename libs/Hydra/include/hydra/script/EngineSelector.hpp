#pragma once
/**
 * @file EngineSelector.hpp
 * @brief Dobór silnika skryptowego do pamięci, jaką ma urządzenie.
 *
 * Interpretery WebAssembly różnią się o rząd wielkości w kosztach i o tyle samo
 * w szybkości. Na ESP32-C3 ze 400 KB RAM mieści się wasm3 i nic więcej; na S3
 * z PSRAM opłaca się WAMR, bo ma szybszy interpreter i tryb AOT. Wybór między
 * nimi jest **decyzją o zasobach**, a nie preferencją — i dlatego jest tu
 * zapisany jako reguła, a nie zostawiony każdemu projektowi z osobna.
 *
 * ## Reguła
 *
 * ```
 *   < 256 KB użytecznego RAM        wasm3     rdzeń ~64 KB
 *   ≥ 256 KB                        WAMR      rdzeń 200 KB+, szybszy
 *   PSRAM obecny                    WAMR      próg nie obowiązuje
 * ```
 *
 * PSRAM zmienia rachunek, bo tam nie chodzi już o zmieszczenie się, tylko
 * o szybkość — a przy pamięci zewnętrznej wolniejszy interpreter boli podwójnie.
 *
 * ## Dlaczego funkcja czysta
 *
 * `choose()` nie czyta niczego ze świata: dostaje liczby, zwraca decyzję.
 * Dzięki temu regułę da się sprawdzić dla każdej konfiguracji sprzętu bez
 * posiadania tego sprzętu — łącznie z przypadkami, których nie sposób wywołać
 * na żądanie, jak wyczerpanie pamięci w trakcie pracy.
 *
 * Wariant czytający prawdziwy stan jest osobno, w `probe()`.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/IScriptEngine.hpp"

/** Czy wasm3 jest wkompilowany. Osadzony w `src/wasm3/`. */
#ifndef HYDRA_SCRIPT_HAS_WASM3
#  define HYDRA_SCRIPT_HAS_WASM3 1
#endif

/**
 * Czy WAMR jest wkompilowany.
 *
 * Domyślnie nie: interpretera nie ma jeszcze w drzewie. Reguła doboru wskazuje
 * go dla urządzeń, które go udźwigną, a `choose()` cofa się wtedy do wasm3
 * i **mówi o tym** w uzasadnieniu — zamiast po cichu udawać, że wybór był
 * zamierzony.
 */
#ifndef HYDRA_SCRIPT_HAS_WAMR
#  define HYDRA_SCRIPT_HAS_WAMR 0
#endif

namespace hydra {
namespace script {

/** Który interpreter. */
enum class EngineKind : u8 {
    None = 0,
    Lua,
    Wasm3,
    Wamr,
};

constexpr const char* toString(EngineKind kind) {
    switch (kind) {
        case EngineKind::None:  return "brak";
        case EngineKind::Lua:   return "lua";
        case EngineKind::Wasm3: return "wasm3";
        case EngineKind::Wamr:  return "wamr";
    }
    return "?";
}

/** Co wiemy o urządzeniu w chwili wyboru. */
struct EngineInputs {
    /** Pamięć dostępna dla programu, nie całkowita pojemność układu. */
    u32  usableRamBytes = 0;
    bool hasPsram       = false;

    /** Które silniki weszły do obrazu. */
    bool wasm3Available = HYDRA_SCRIPT_HAS_WASM3 != 0;
    bool wamrAvailable  = HYDRA_SCRIPT_HAS_WAMR != 0;

    /**
     * Czy program jest w WebAssembly.
     *
     * Fałsz oznacza skrypt tekstowy i wtedy nie ma czego wybierać — Lua jest
     * jedynym silnikiem, który przyjmuje źródło.
     */
    bool wantsWasm = true;
};

/** Decyzja razem z powodem. */
struct EngineChoice {
    EngineKind kind = EngineKind::None;
    /**
     * Dlaczego akurat ten. Trafia do logu przy starcie, bo pytanie „czemu
     * urządzenie wzięło wolniejszy interpreter" pada zawsze wtedy, gdy nikt
     * już nie pamięta, ile było wolnej pamięci.
     */
    const char* reason = "";
    /** Czy wybór jest ustępstwem — silnik wskazany przez regułę był niedostępny. */
    bool fallback = false;
};

/** Próg, powyżej którego opłaca się cięższy interpreter. */
constexpr u32 kWamrRamThreshold = 256 * 1024;

/** Reguła doboru. Funkcja czysta — patrz nagłówek pliku. */
EngineChoice choose(const EngineInputs& inputs);

/**
 * Odpytuje urządzenie i stosuje regułę.
 *
 * Bierze wolną pamięć **w chwili wywołania**, więc ma sens wyłącznie przy
 * starcie: wołanie tego po zaalokowaniu buforów sieci i obrazu dałoby inną
 * odpowiedź niż przed.
 */
EngineChoice probe(bool wantsWasm = true);

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
