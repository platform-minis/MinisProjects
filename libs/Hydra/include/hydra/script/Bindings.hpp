#pragma once
/**
 * Hydra — biblioteka `hydra` widoczna ze skryptu.
 *
 * Interpreter bez dostępu do sprzętu jest kalkulatorem. Ten nagłówek dokłada
 * do skryptu to, po co się go w ogóle osadza: piny, przetworniki, magistrale
 * i magistralę zdarzeń — wszystko przez interfejsy HAL, więc ten sam skrypt
 * działa na ESP32, RP2350 i STM32 bez zmiany ani jednego znaku.
 *
 * Zestaw jest wybierany, a nie narzucony. Skrypt liczący histerezę termostatu
 * nie potrzebuje I²C, a każda niewłączona grupa to funkcje, których nie ma
 * w tablicy globalnej — mniej powierzchni na pomyłkę i mniej pamięci.
 *
 * Widok ze skryptu:
 *
 *     hydra.millis()               -- czas od startu w ms
 *     hydra.micros()               -- czas od startu w µs
 *     hydra.delay(ms)              -- uśpienie taska skryptu
 *     hydra.platform               -- "esp32s3", "rp2350", "stm32", "host"
 *     hydra.version                -- wersja frameworka
 *
 *     hydra.log.info("tekst", 42)  -- także debug/trace/warn/error
 *
 *     hydra.gpio.mode(pin, "out")  -- "in", "in_pullup", "in_pulldown", "out", "out_od"
 *     hydra.gpio.write(pin, true)
 *     hydra.gpio.read(pin)         -- true/false
 *     hydra.gpio.toggle(pin)
 *
 *     hydra.adc.raw(pin)           -- surowy odczyt
 *     hydra.adc.mv(pin)            -- miliwolty po kalibracji
 *
 *     hydra.pwm.setup(pin, 50)     -- częstotliwość w Hz
 *     hydra.pwm.duty(pin, 500)     -- wypełnienie w promilach
 *     hydra.pwm.us(pin, 1500)      -- szerokość impulsu (serwo)
 *     hydra.pwm.release(pin)
 *
 *     hydra.i2c.scan()             -- tabela adresów
 *     hydra.i2c.ping(0x76)
 *     hydra.i2c.read(0x76, 0xD0, 1)  -- tabela bajtów
 *     hydra.i2c.write(0x76, 0xF4, {0x27})
 *
 *     hydra.event.emit("alarm", 21.5)
 *     hydra.event.on("alarm", function(value, data) ... end)
 *
 * Każda funkcja sięgająca po sprzęt zwraca przy niepowodzeniu `nil` i opis
 * błędu jako drugą wartość — czyli konwencję Lua, a nie kod błędu Hydry:
 *
 *     local v, err = hydra.gpio.read(99)
 *     if v == nil then hydra.log.warn(err) end
 */

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Events.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/script/Script.hpp"

namespace hydra {
namespace script {

/**
 * Sygnał wymieniany między skryptem a resztą systemu.
 *
 * Skrypty nie mogą publikować dowolnych typów zdarzeń — te są strukturami C++
 * znanymi w chwili kompilacji. Zamiast tego dostają jeden typ z nazwą zwiniętą
 * do identyfikatora i dwiema wartościami: jedną zmiennoprzecinkową i jedną
 * całkowitą. To pokrywa realne zastosowania (próg przekroczony, tryb zmieniony,
 * przycisk wciśnięty), a zdarzenie zostaje lekkim POD-em, jak wymaga rozdz. 4.3.
 */
struct ScriptSignal {
    u16   nameId;  ///< nameId(nazwa) — nazwa nie podróżuje, podróżuje jej skrót
    float value;
    i32   data;
};

/** Które grupy funkcji udostępnić skryptowi. */
struct BindingSet {
    bool core  = true;   ///< czas, wersja, platforma, delay
    bool log   = true;
    bool gpio  = true;
    bool adc   = false;
    bool pwm   = false;
    bool i2c   = false;
    bool event = true;
};

/**
 * Zakłada tabelę `hydra` i wypełnia ją wybranymi grupami.
 * Wołać po `Interp::open()`, przed wczytaniem skryptu.
 */
Status installBindings(Interp& interp, const BindingSet& set);
/** Wersja z domyślnym zestawem: rdzeń, log, GPIO i zdarzenia. */
Status installBindings(Interp& interp);

/**
 * Wykonuje handlery zarejestrowane przez `hydra.event.on`.
 *
 * Sygnały przychodzące z magistrali nie wołają skryptu od razu — trafiają do
 * kolejki, którą opróżnia ta funkcja w kontekście taska skryptu. Inaczej kod
 * Lua wykonywałby się w kontekście nadawcy, czyli potencjalnie w pętli
 * sterowania albo w tasku sieciowym, a interpreter nie jest wielobieżny.
 *
 * Zwraca liczbę obsłużonych sygnałów. Woła ją `ScriptModule` w każdym przebiegu.
 */
u32 dispatchSignals(Interp& interp, u32 maxSignals = 8);

/** Liczba sygnałów porzuconych z powodu pełnej kolejki. */
u32 droppedSignals();

/** Zwalnia subskrypcję magistrali. Wołane przy zamykaniu modułu. */
void removeBindings(Interp& interp);

}  // namespace script
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::script::ScriptSignal, "script/signal")
