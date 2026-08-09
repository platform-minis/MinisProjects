#pragma once
/**
 * Hydra — wyjście interpretera skryptów.
 *
 * `print()` i komunikaty krytyczne Lua idą normalnie do stdout i stderr.
 * Na urządzeniu nie ma ani jednego, ani drugiego, więc oba strumienie
 * przechwytuje Hydra i kieruje tam, gdzie akurat trzeba: do logu, na port
 * szeregowy albo do bufora shella, gdy skrypt wykonuje się z komendy `lua`.
 *
 * Odbiornik jest jeden na cały podsystem, bo taki jest kontrakt Lua — makra
 * `lua_writestring` rozwijają się do zwykłego wywołania funkcji, bez żadnego
 * kontekstu, przez który dałoby się przekazać stan.
 *
 * Domyślnie wyjście trafia do `Log` na poziomie Info, z modułem "lua". Lua
 * wypisuje `print()` kawałkami — osobno każdy argument, osobno tabulator,
 * osobno znak końca wiersza — więc warstwa scala je w całe wiersze, zanim odda
 * je dalej. Bez tego jeden `print("a", "b")` dałby pięć linii w logu.
 */

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace script {

/** Odbiornik gotowego wiersza. Tekst jest zakończony zerem, bez znaku nowej linii. */
using OutputSink = Delegate<void(const char*, size_t)>;

/**
 * Przekierowuje wyjście skryptów. Przekazanie pustego delegata przywraca
 * zapis do `Log`.
 */
void setOutput(OutputSink sink);

/** Przywraca domyślne wyjście (Log, moduł "lua", poziom Info). */
void resetOutput();

/**
 * Wypycha niedokończony wiersz, jeśli skrypt wypisał tekst bez `\n`.
 * Wołane po zakończeniu wykonania, żeby ostatni fragment nie utknął w buforze.
 */
void flushOutput();

}  // namespace script
}  // namespace hydra
