#pragma once
/**
 * @file WasmBindings.hpp
 * @brief Funkcje gospodarza widziane przez moduł WebAssembly.
 *
 * Odpowiednik `Bindings.hpp` dla drugiego silnika. Ten sam zakres możliwości
 * co w Lua — czas, log, GPIO, zdarzenia — wystawiony jako **importy** modułu.
 *
 * ## Piaskownica przechodzi tędy i nigdzie indziej
 *
 * Moduł WebAssembly nie ma dostępu do niczego poza własną pamięcią liniową.
 * Wszystko, co potrafi zrobić światu, przechodzi przez funkcje wymienione tutaj
 * — i to jest jedyne miejsce, w którym granica jest naprawdę pilnowana.
 *
 * Stąd zasada, od której nie ma odstępstw: **każdy wskaźnik przychodzący
 * z modułu jest sprawdzany przed użyciem.** Moduł podaje adresy jako liczby
 * całkowite w swojej przestrzeni; wzięcie takiej liczby za wskaźnik gospodarza
 * bez sprawdzenia zakresu oddaje całą pamięć urządzenia dowolnemu modułowi,
 * jaki ktoś wgra.
 *
 * Sprawdza to makro `m3ApiCheckMem` z wasm3, a nie własna klasa — i jest to
 * decyzja, nie lenistwo. Pamięć liniowa **rośnie** w trakcie pracy przez
 * instrukcję `memory.grow`, więc zapamiętany rozmiar bufora starzeje się
 * po pierwszym takim wywołaniu: zapis mieszczący się w powiększonej pamięci
 * zostałby odrzucony, a co gorsza — cache wskaźnika bazowego po przeniesieniu
 * bufora wskazywałby w zwolnioną pamięć. Makro pyta środowisko uruchomieniowe
 * o stan bieżący przy każdym wywołaniu.
 *
 * ## Nazwy importów
 *
 * Wszystko w przestrzeni `hydra`, nazwy z podkreśleniami:
 *
 *     (import "hydra" "log_info"   (func $log_info (param i32 i32)))
 *     (import "hydra" "gpio_write" (func $gpio_write (param i32 i32)))
 *
 * Płaska przestrzeń zamiast zagnieżdżonej, bo taki kształt mają importy
 * WebAssembly — kropka w nazwie byłaby ozdobnikiem, a nie strukturą.
 * W AssemblyScripcie odpowiada temu `@external("hydra", "log_info")`.
 *
 * ## Import nieużywany nie kosztuje
 *
 * Linkujemy wszystkie funkcje z włączonych grup, ale moduł importuje tylko te,
 * których używa. wasm3 wiąże po nazwie w chwili ładowania; nadmiarowe wpisy
 * po stronie gospodarza są bezkosztowe, a brakujący import to błąd ładowania
 * z nazwą funkcji w komunikacie.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/Bindings.hpp"
#include "hydra/script/Wasm3Engine.hpp"

namespace hydra {
namespace script {

/**
 * Wpina funkcje gospodarza w załadowany moduł.
 *
 * Wołać **po** `loadBinary()`, a przed pierwszym wywołaniem: wasm3 wiąże
 * importy z modułem, którego jeszcze nie ma, jeśli zrobić to wcześniej.
 *
 * Funkcje, których moduł nie importuje, są pomijane — gospodarz oferuje,
 * moduł bierze tyle, ile potrzebuje. Wymuszanie kompletu importów zmuszałoby
 * każdy program do deklarowania funkcji, o których nic nie wie.
 *
 * Import **wymagany przez moduł, a niezwiązany** — bo wyłączono jego grupę —
 * nie jest tu wykrywany, i nie jest to przeoczenie: z tej strony nie da się
 * odróżnić „moduł tego nie chce" od „grupa wyłączona". Wychodzi za to przy
 * pierwszym `hasFunction()` albo `startJob()`, bo wasm3 kompiluje ciało
 * funkcji dopiero wtedy, a niezwiązany import przerywa kompilację. Jest to
 * moment wcześniejszy niż wywołanie, więc program nie zdąży wykonać połowy
 * pracy, zanim się zorientuje.
 */
Status installWasmBindings(Wasm3Engine& engine, const BindingSet& set);
/** Wersja z domyślnym zestawem: rdzeń, log, GPIO i zdarzenia. */
Status installWasmBindings(Wasm3Engine& engine);

/**
 * Podaje modułowi zdarzenia czekające w kolejce.
 *
 * Woła eksport `on_event(i32 nameId, f32 value, i32 data)`, jeśli moduł go ma.
 * Odpowiednik `dispatchSignals()` dla Lua i korzysta z **tej samej** kolejki —
 * sygnał z magistrali trafia tam niezależnie od języka programu.
 *
 * Wołać z kontekstu taska skryptu; zwraca liczbę obsłużonych sygnałów.
 */
u32 dispatchWasmSignals(Wasm3Engine& engine, u32 maxSignals = 8);

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
