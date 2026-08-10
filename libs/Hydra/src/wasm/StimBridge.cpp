/**
 * Most między panelem w przeglądarce a warstwą bodźców.
 *
 * `stim` istnieje po to, żeby wywołać zjawisko w świecie wokół urządzenia:
 * na hoście podmienia atrapę HAL, na stanowisku steruje przekaźnikiem.
 * W przeglądarce tym „światem" jest interfejs — suwak temperatury i przycisk
 * na stronie. Ten plik jest jedyną rzeczą, której do tego brakowało.
 *
 * ## Dlaczego płaskie funkcje w C, a nie `EMSCRIPTEN_BINDINGS`
 *
 * Bindingi Embinda wciągają obsługę wyjątków i RTTI, a Hydra buduje się
 * z `-fno-exceptions -fno-rtti`. Płaskie funkcje z `EMSCRIPTEN_KEEPALIVE`
 * wołane przez `Module.ccall` nie wnoszą niczego poza sobą i działają przy
 * tych samych przełącznikach, co reszta biblioteki.
 *
 * ## Wynik jest liczbą, nie wyjątkiem
 *
 * Każda funkcja zwraca zero przy powodzeniu albo kod `Err`. Panel po drugiej
 * stronie ma pokazać, że bodziec nie przeszedł — na przykład dlatego, że
 * projekt nie ma atrapy magistrali — a nie przewrócić stronę.
 */

#include "hydra/core/Config.hpp"

#if defined(HYDRA_PLAT_WASM) && HYDRA_PLAT_WASM

#include <emscripten/emscripten.h>

#include "hydra/stim/MockStimulus.hpp"

namespace {

/**
 * Jedyna instancja bodźca dla strony.
 *
 * Statyczna, bo panel woła ją z zewnątrz i nie ma jak jej podać — po stronie
 * JavaScriptu istnieje tylko nazwa funkcji, nie wskaźnik na obiekt.
 */
hydra::stim::MockStimulus& bridge() {
    static hydra::stim::MockStimulus instance;
    return instance;
}

int toCode(const hydra::Status& status) {
    return status ? 0 : static_cast<int>(status.error());
}

}  // namespace

extern "C" {

/** Stan nóżki cyfrowej — przycisk w panelu. */
EMSCRIPTEN_KEEPALIVE int hydra_stim_digital(int pin, int high) {
    return toCode(bridge().digitalInput(static_cast<hydra::hal::PinNum>(pin), high != 0));
}

/** Pojedyncze zbocze — impuls, którego nie da się oddać dwoma stanami. */
EMSCRIPTEN_KEEPALIVE int hydra_stim_edge(int pin) {
    return toCode(bridge().edge(static_cast<hydra::hal::PinNum>(pin)));
}

/** Napięcie na wejściu analogowym w miliwoltach — suwak w panelu. */
EMSCRIPTEN_KEEPALIVE int hydra_stim_analog(int pin, int millivolts) {
    if (millivolts < 0) millivolts = 0;
    return toCode(bridge().analogInput(static_cast<hydra::hal::PinNum>(pin),
                                       static_cast<hydra::u16>(millivolts)));
}

/** Obecność układu na magistrali — wypięcie czujnika bez ruszania kabla. */
EMSCRIPTEN_KEEPALIVE int hydra_stim_presence(int bus, int address, int present) {
    return toCode(bridge().devicePresence(static_cast<hydra::u8>(bus),
                                          static_cast<hydra::u8>(address), present != 0));
}

/** Zawartość rejestru układu — surowa wartość, którą zwróci sterownik. */
EMSCRIPTEN_KEEPALIVE int hydra_stim_register(int bus, int address, int reg, int value,
                                             int width) {
    return toCode(bridge().deviceRegister(
        static_cast<hydra::u8>(bus), static_cast<hydra::u8>(address),
        static_cast<hydra::u8>(reg), static_cast<hydra::u16>(value),
        static_cast<hydra::u8>(width < 1 ? 1 : width)));
}

/**
 * Awarie magistrali — `count` kolejnych transferów zakończy się błędem.
 *
 * Bez tego panel potrafiłby wywołać tylko sytuacje poprawne, a najciekawsze
 * w symulatorze jest to, czego na biurku nie da się wywołać na żądanie.
 */
EMSCRIPTEN_KEEPALIVE int hydra_stim_bus_fault(int bus, int count) {
    return toCode(bridge().busFault(static_cast<hydra::u8>(bus),
                                    static_cast<hydra::u32>(count)));
}

/** Czy dane zjawisko jest w ogóle obsługiwane przez ten cel. */
EMSCRIPTEN_KEEPALIVE int hydra_stim_supports(int phenomenon) {
    return bridge().supports(static_cast<hydra::stim::Phenomenon>(phenomenon)) ? 1 : 0;
}

/** Przywraca stan wyjściowy — panel po przeładowaniu programu. */
EMSCRIPTEN_KEEPALIVE void hydra_stim_reset() {
    bridge().reset();
}

}  // extern "C"

#endif  // HYDRA_PLAT_WASM
