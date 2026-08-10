/**
 * Funkcje gospodarza dla modułów WebAssembly.
 *
 * Odpowiedniki bindingów Lua z `Bindings.cpp`, wystawione jako importy. Robią
 * to samo i wołają to samo — różni je wyłącznie sposób przekazywania wartości:
 * Lua ma stos z typami dynamicznymi, WebAssembly cztery typy liczbowe i pamięć
 * liniową, przez którą przechodzi wszystko inne.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/WasmBindings.hpp"

#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/core/Rtos.hpp"
#include "hydra/hal/Hal.hpp"

#include "wasm3.h"

HYDRA_LOG_MODULE("wasm")

namespace hydra {
namespace script {

namespace {

// ═══════════════════════════════════════════════════════════════════════════
//  Rdzeń
// ═══════════════════════════════════════════════════════════════════════════

m3ApiRawFunction(wasmMillis) {
    m3ApiReturnType(uint32_t);
    m3ApiReturn(static_cast<uint32_t>(App::uptimeMs()));
}

m3ApiRawFunction(wasmDelayMs) {
    m3ApiGetArg(uint32_t, ms);

    // Czekanie w module blokuje task skryptu — tak samo jak w Lua. Różnica
    // jest taka, że tam budżet mógł przerwać pętlę wokół `delay`, a tutaj nie
    // ma czego przerwać. Zostawiamy funkcję, bo bez niej moduł zrobi pustą
    // pętlę, która jest gorsza: zajmie rdzeń zamiast go oddać.
    rtos::delayMs(ms);
    m3ApiSuccess();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Log
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Wypisuje łańcuch z pamięci modułu.
 *
 * `m3ApiCheckMem` jest tu jedyną rzeczą stojącą między modułem a pamięcią
 * urządzenia. Bez niego moduł podający `ptr = 0xFFFFFFF0` czytałby cokolwiek
 * leży pod tym adresem u gospodarza.
 */
m3ApiRawFunction(wasmLog) {
    m3ApiGetArg(uint32_t, level);
    m3ApiGetArgMem(const char*, text);
    m3ApiGetArg(uint32_t, length);

    m3ApiCheckMem(text, length);

    // Łańcuchy w WebAssembly nie są zakończone zerem — długość przychodzi
    // osobno. Kopiujemy do bufora o znanym rozmiarze, bo `HYDRA_LOG_AT`
    // oczekuje łańcucha w stylu C.
    char line[HYDRA_LOG_LINE_MAX];
    const size_t take = length < sizeof(line) - 1 ? length : sizeof(line) - 1;
    memcpy(line, text, take);
    line[take] = '\0';

    const LogLevel mapped = level >= static_cast<uint32_t>(LogLevel::Error)
                                ? LogLevel::Error
                                : static_cast<LogLevel>(level);
    HYDRA_LOG_AT(mapped, "wasm", "%s", line);
    m3ApiSuccess();
}

// ═══════════════════════════════════════════════════════════════════════════
//  GPIO
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Tryb nóżki liczbą, nie nazwą.
 *
 * Lua przyjmuje `"out"`, bo tam łańcuch nic nie kosztuje. Tutaj oznaczałby
 * przekazanie wskaźnika i długości oraz porównywanie tekstu przy każdym
 * wywołaniu — dla wartości, która ma sześć możliwości. Kolejność odpowiada
 * `hal::PinMode`, a nazwy stałych podaje nagłówek dla modułów.
 */
hal::PinMode pinModeFrom(uint32_t value) {
    switch (value) {
        case 0:  return hal::PinMode::Input;
        case 1:  return hal::PinMode::InputPullUp;
        case 2:  return hal::PinMode::InputPullDown;
        case 3:  return hal::PinMode::Output;
        case 4:  return hal::PinMode::OutputOpenDrain;
        default: return hal::PinMode::Analog;
    }
}

m3ApiRawFunction(wasmGpioMode) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);
    m3ApiGetArg(uint32_t, mode);

    const auto result = hal::Hal::gpio().configure(static_cast<hal::PinNum>(pin),
                                                   pinModeFrom(mode));
    // Kod błędu wraca liczbą, a nie pułapką maszyny: nóżka zajęta przez inny
    // moduł to sytuacja, którą program ma prawo obsłużyć, a nie powód do
    // przerwania go w połowie.
    m3ApiReturn(result ? 0u : static_cast<uint32_t>(result.error()));
}

m3ApiRawFunction(wasmGpioWrite) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);
    m3ApiGetArg(uint32_t, level);

    const auto result = hal::Hal::gpio().write(static_cast<hal::PinNum>(pin), level != 0);
    m3ApiReturn(result ? 0u : static_cast<uint32_t>(result.error()));
}

m3ApiRawFunction(wasmGpioRead) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);

    const auto value = hal::Hal::gpio().read(static_cast<hal::PinNum>(pin));
    // Odczyt nieudany daje zero, bo funkcja zwraca stan nóżki, a nie wynik
    // operacji. Program, któremu to przeszkadza, ma `gpio_mode` z kodem błędu.
    m3ApiReturn(value && *value ? 1u : 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Zdarzenia
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Zdarzenie ze skryptu na magistralę.
 *
 * Ładunek to dwie liczby, nie dowolna struktura. Magistrala Hydry przenosi
 * zdarzenia POD do 32 bajtów; wpuszczenie tu bufora z pamięci modułu
 * wymagałoby kopii o rozmiarze znanym dopiero w czasie wykonania, a to jest
 * alokacja — czyli rzecz, której po `App::begin()` nie ma.
 */
m3ApiRawFunction(wasmEmit) {
    m3ApiGetArg(uint32_t, id);
    m3ApiGetArg(float,    value);
    m3ApiGetArg(int32_t,  data);

    // Identyfikator jest szesnastobitowy, bo taki jest `nameId` w magistrali.
    // Obcięcie jest jawne: moduł podający większą liczbę ma zobaczyć zdarzenie
    // pod innym identyfikatorem, a nie zastanawiać się, czemu go nie ma.
    EventBus::publish(ScriptSignal{static_cast<u16>(id), value, data});
    m3ApiSuccess();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Tablica importów
// ═══════════════════════════════════════════════════════════════════════════

struct Import {
    const char*   name;
    const char*   signature;
    M3RawCall     fn;
};

/*
 * Sygnatury w notacji wasm3: pierwszy znak to typ wyniku, w nawiasie
 * argumenty. `v` to brak wartości, `i` to i32, `*` to wskaźnik w pamięci
 * modułu. Rozjazd między sygnaturą tutaj a `m3ApiGetArg` niżej daje
 * przesunięcie odczytu stosu — dlatego jedno i drugie stoi obok siebie.
 */
constexpr Import kCore[] = {
    {"millis",   "i()",   &wasmMillis},
    {"delay_ms", "v(i)",  &wasmDelayMs},
};

constexpr Import kLog[] = {
    {"log", "v(i*i)", &wasmLog},
};

constexpr Import kGpio[] = {
    {"gpio_mode",  "i(ii)", &wasmGpioMode},
    {"gpio_write", "i(ii)", &wasmGpioWrite},
    {"gpio_read",  "i(i)",  &wasmGpioRead},
};

constexpr Import kEvent[] = {
    {"emit", "v(ifi)", &wasmEmit},
};

/** Nazwa przestrzeni importów. Ta sama po stronie modułu. */
constexpr const char* kNamespace = "hydra";

/**
 * Eksport, przez który moduł odbiera zdarzenia z magistrali.
 *
 * Odpowiednik `hydra.event.on` z Lua, tylko w drugą stronę: tam skrypt
 * rejestrował funkcję, tu gospodarz woła znaną nazwę. Rejestracja wymagałaby
 * przechowywania uchwytów funkcji modułu po stronie gospodarza i unieważniania
 * ich przy każdej podmianie programu — stała nazwa nie ma tego problemu.
 */
constexpr const char* kEventExport = "on_event";

/**
 * Wiąże grupę.
 *
 * `m3Err_functionLookupFailed` znaczy „moduł tego nie importuje" i **nie jest
 * błędem**: gospodarz oferuje, moduł bierze tyle, ile potrzebuje. Traktowanie
 * tego jako awarii zmuszałoby każdy moduł do importowania kompletu funkcji,
 * łącznie z tymi, o których nic nie wie.
 */
Status linkGroup(IM3Module module, const Import* imports, size_t count, const char** failed) {
    for (size_t i = 0; i < count; ++i) {
        const M3Result result = m3_LinkRawFunction(module, kNamespace, imports[i].name,
                                                   imports[i].signature, imports[i].fn);
        if (result == m3Err_none || result == m3Err_functionLookupFailed) continue;

        *failed = imports[i].name;
        return fail(Err::Protocol);
    }
    return ok();
}

}  // namespace

Status installWasmBindings(Wasm3Engine& engine, const BindingSet& set) {
    IM3Module module = static_cast<IM3Module>(engine.rawModule());
    if (module == nullptr) {
        // Bindingi wpina się w moduł, więc musi już być załadowany. Odwrotna
        // kolejność kończy się modułem bez importów, który wywala się dopiero
        // przy pierwszym wywołaniu.
        return fail(Err::NotInitialized);
    }

    const char* failed = nullptr;

    if (set.core)  HYDRA_CHECK(linkGroup(module, kCore,  sizeof(kCore) / sizeof(kCore[0]),  &failed));
    if (set.log)   HYDRA_CHECK(linkGroup(module, kLog,   sizeof(kLog) / sizeof(kLog[0]),   &failed));
    if (set.gpio)  HYDRA_CHECK(linkGroup(module, kGpio,  sizeof(kGpio) / sizeof(kGpio[0]),  &failed));
    if (set.event) {
        HYDRA_CHECK(linkGroup(module, kEvent, sizeof(kEvent) / sizeof(kEvent[0]), &failed));
        // Bez tego `dispatchWasmSignals()` nie ma czego rozsyłać: kolejkę
        // napełnia subskrybent magistrali, a nie samo wpięcie importów.
        HYDRA_CHECK(subscribeScriptSignals());
    }

    if (failed != nullptr) {
        HYDRA_LOGE("nie udalo sie zwiazac importu hydra.%s", failed);
        return fail(Err::Protocol);
    }

    // ADC, PWM i I2C świadomie poza etapem: każde z nich potrzebuje własnego
    // przemyślenia, co przechodzi przez granicę piaskownicy. Magistrala I2C
    // wystawiona modułowi wprost oznacza dostęp do wszystkich układów na niej,
    // łącznie z tymi, których program nie powinien tknąć.
    if (set.adc || set.pwm || set.i2c) {
        HYDRA_LOGW("grupy adc/pwm/i2c nie sa jeszcze dostepne dla modulow WebAssembly");
    }

    return ok();
}

Status installWasmBindings(Wasm3Engine& engine) {
    return installWasmBindings(engine, BindingSet{});
}

// ═══════════════════════════════════════════════════════════════════════════
//  Droga powrotna: magistrala → moduł
// ═══════════════════════════════════════════════════════════════════════════

u32 dispatchWasmSignals(Wasm3Engine& engine, u32 maxSignals) {
    if (!engine.ready()) return 0;

    IM3Function fn = nullptr;
    if (m3_FindFunction(&fn, static_cast<IM3Runtime>(engine.rawRuntime()), kEventExport)
            != m3Err_none || fn == nullptr) {
        // Moduł bez `on_event` nie jest błędem — po prostu nie słucha
        // magistrali. Sygnały zostają w kolejce dla tego, kto ich chce.
        return 0;
    }

    u32 handled = 0;
    ScriptSignal signal{};

    while (handled < maxSignals && popScriptSignal(signal)) {
        // `m3_Call` bierze argumenty jako tablicę wskaźników na wartości.
        const uint32_t id    = signal.nameId;
        const float    value = signal.value;
        const int32_t  data  = signal.data;
        const void*    args[] = {&id, &value, &data};

        const M3Result result = m3_Call(fn, 3, args);
        ++handled;

        if (result != m3Err_none) {
            // Pułapka w handlerze przerywa **ten** przebieg, nie rozsyłanie
            // w ogóle: sygnał jest już zdjęty, więc kolejka nie rośnie w kółko
            // przez jeden feralny wpis. Kolejne pójdą w następnym przebiegu.
            HYDRA_LOGE("on_event(): %s", result);
            break;
        }
    }
    return handled;
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
