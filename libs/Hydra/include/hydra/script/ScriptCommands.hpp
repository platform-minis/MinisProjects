#pragma once
/**
 * Hydra — komendy shella dla podsystemu skryptów.
 *
 * Rejestruje jedną komendę `lua` z podpoleceniami. Wartość jest w tym, że
 * pozwala dotknąć działającego urządzenia bez debuggera i bez przekompilowania:
 * sprawdzić stan zmiennej, wywołać funkcję, zobaczyć zużycie pamięci, wgrać
 * poprawkę.
 *
 *     lua print(counter)      -- wykonaj fragment w bieżącym stanie
 *     lua = 2 + 2             -- skrót: wypisz wartość wyrażenia
 *     lua mem                 -- zajętość puli i fragmentacja
 *     lua stat                -- statystyki modułu
 *     lua gc                  -- wymuś odśmiecanie
 *     lua reload              -- wczytaj skrypt od nowa
 *
 * Wyjście `print()` idzie na czas wykonania komendy do shella, a nie do logu —
 * bo kto pisze `lua print(x)`, chce zobaczyć wynik tam, gdzie pisał.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/script/ScriptModule.hpp"
#include "hydra/shell/Shell.hpp"

#if HYDRA_ENABLE_SCRIPT

namespace hydra {
namespace script {

/**
 * Rejestruje komendę `lua`. Moduł musi żyć dłużej niż shell.
 * Bez modułu (sam `Interp`) użyj wariantu niżej.
 */
Status registerScriptCommands(shell::Shell& shell, ScriptModule& module);

/** Wariant bez modułu — dostępne są `mem`, `gc` i wykonywanie fragmentów. */
Status registerScriptCommands(shell::Shell& shell, Interp& interp);

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
