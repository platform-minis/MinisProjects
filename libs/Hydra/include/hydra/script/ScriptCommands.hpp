#pragma once
/**
 * Hydra — komendy shella dla podsystemu skryptów.
 *
 * Rejestruje komendę `script` z podpoleceniami. Wartość jest w tym, że pozwala
 * dotknąć działającego urządzenia bez debuggera i bez przekompilowania:
 * sprawdzić stan zmiennej, wywołać funkcję, zobaczyć zużycie pamięci, wgrać
 * poprawkę.
 *
 *     script print(counter)   -- wykonaj fragment w bieżącym stanie
 *     script = 2 + 2          -- skrót: wypisz wartość wyrażenia
 *     script mem              -- zajętość puli i fragmentacja
 *     script stat             -- statystyki modułu
 *     script gc               -- wymuś odśmiecanie
 *     script reload           -- wczytaj skrypt od nowa
 *
 * `lua` zostaje aliasem: komenda jest starsza niż podział na silniki, a wpisany
 * z palca skrót nie ma powodu przestać działać. Wykonywanie fragmentów wymaga
 * silnika, który potrafi `eval()` — dla modułu binarnego kończy się odmową,
 * a `mem`, `stat`, `gc` i `reload` działają niezależnie od silnika.
 *
 * Wyjście `print()` idzie na czas wykonania komendy do shella, a nie do logu —
 * bo kto pisze `script print(x)`, chce zobaczyć wynik tam, gdzie pisał.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/script/ScriptModule.hpp"
#include "hydra/shell/Shell.hpp"

#if HYDRA_ENABLE_SCRIPT

namespace hydra {
namespace script {

/**
 * Rejestruje komendę `script` (z aliasem `lua`). Moduł musi żyć dłużej niż
 * shell i mieć zawołane `configure()` — stamtąd bierze się silnik.
 */
Status registerScriptCommands(shell::Shell& shell, ScriptModule& module);

/** Wariant bez modułu — bez `stat` i `reload`, reszta działa. */
Status registerScriptCommands(shell::Shell& shell, IScriptEngine& engine);

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
