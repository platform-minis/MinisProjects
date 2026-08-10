#pragma once
/**
 * Hydra — kolejka sygnałów między magistralą a skryptem.
 *
 * Nagłówek wewnętrzny warstwy skryptowej: nie wchodzi do `include/`, bo to
 * szczegół implementacji, a nie API frameworka.
 *
 * **Po co osobno.** Kolejka powstała w bindingach Lua i była tam wpisana razem
 * z pętlą wywołującą handlery. Silnik WebAssembly potrzebuje dokładnie tego
 * samego pierścienia i tej samej subskrypcji, ale zupełnie innej pętli — moduł
 * nie ma tabeli handlerów, ma eksportowaną funkcję `on_event`. Wspólna jest
 * droga sygnału, nie sposób jego interpretacji.
 *
 * **Dlaczego sygnał nie woła skryptu wprost.** Callback magistrali wykonuje się
 * w kontekście nadawcy, a tym bywa pętla sterowania albo task sieciowy. Kod
 * skryptu o nieznanym czasie wykonania zatrzymałby cudzą pętlę, a interpreter
 * nie jest wielobieżny. Dlatego callback odkłada POD do pierścienia, a resztę
 * robi task skryptu.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/Bindings.hpp"

/** Głębokość pierścienia sygnałów. */
#ifndef HYDRA_SCRIPT_SIGNAL_QUEUE
#  define HYDRA_SCRIPT_SIGNAL_QUEUE 16
#endif

namespace hydra {
namespace script {
namespace detail {

/**
 * Podpina się pod magistralę, jeśli jeszcze nie jest podpięta.
 * Wołane przy instalowaniu bindingów grupy `event`.
 */
Status signalQueueSubscribe();

/** Odpina subskrypcję i czyści pierścień. Wołane przy zamykaniu silnika. */
void signalQueueRelease();

/** Zdejmuje najstarszy sygnał. `false`, gdy pierścień jest pusty. */
bool signalQueuePop(ScriptSignal& out);

/** Ile sygnałów porzucono z powodu pełnego pierścienia. */
u32 signalQueueDropped();

}  // namespace detail
}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
