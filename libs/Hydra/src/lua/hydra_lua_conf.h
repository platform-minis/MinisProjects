/*
** Hydra — konfiguracja osadzenia Lua.
**
** Wciągany na górze luaconf.h (jedyna łatka na źródłach z lua.org), więc widzi
** go każdy plik sięgający po lua.h — zarówno jednostki samego Lua kompilowane
** jako C, jak i kod C++ Hydry. To jest tu sedno: LUA_32BITS zmienia typ
** lua_Number i układ TValue, więc ustawienie go z build_flags oznaczałoby
** ciche rozjechanie ABI w chwili, gdy ktoś skompiluje własny plik bez tych flag.
** Nagłówek nakłada konfigurację raz i dla wszystkich.
**
** Plik należy do Hydry — tools/vendor_lua.sh go nie nadpisuje.
*/

#ifndef hydra_lua_conf_h
#define hydra_lua_conf_h

#include <stddef.h>

/*
** Detekcja platformy i profil pamięciowy pochodzą z nagłówków Hydry. Oba to
** wyłącznie dyrektywy preprocesora — nie ma tam ani jednej konstrukcji C++ —
** więc wciągnięcie ich z jednostki C jest poprawne i oszczędza drugiej,
** rozjeżdżającej się z czasem kopii tych samych progów.
*/
#include "hydra/script/Profile.hpp"

/*
** ===================================================================
** Reprezentacja liczb
** ===================================================================
*/

/*
@@ LUA_32BITS — liczby całkowite 32-bitowe i float zamiast double.
**
** Ustawione na wszystkich platformach, także na hoście. To celowe: testy
** hostowe mają sprawdzać ten sam kod, który pojedzie na urządzeniu, a nie
** jego szerszy wariant. Skutki, które trzeba znać:
**   - liczba całkowita mieści zakres int32 (±2,1 mld),
**   - liczba zmiennoprzecinkowa ma ~7 cyfr znaczących,
**   - TValue zajmuje 8 bajtów zamiast 16, co zmniejsza zużycie pamięci
**     przez tabele i stos mniej więcej o połowę,
**   - matematyka idzie przez sinf/cosf/sqrtf, a nie przez warianty double —
**     na Cortex-M4F i M33 wykonuje je sprzęt, a na M0+ tańsza emulacja.
*/
#if !defined(LUA_32BITS)
#define LUA_32BITS	1
#endif

/*
** ===================================================================
** Budżety pamięci
** ===================================================================
**
** Profil (HYDRA_SCRIPT_LARGE_PROFILE) ustala hydra/script/Profile.hpp.
*/

/*
@@ LUAI_MAXSTACK — górny limit stosu Lua w slotach.
**
** Domyślne 1 000 000 z upstreamu to limit dla maszyn z pamięcią wirtualną.
** Tutaj jego rolą jest zatrzymać niekończącą się rekurencję komunikatem
** "stack overflow", zanim skrypt wyczerpie całą pulę sterty i wywróci się
** znacznie mniej czytelnym "not enough memory".
*/
#if !defined(LUAI_MAXSTACK)
#  if HYDRA_SCRIPT_LARGE_PROFILE
#    define LUAI_MAXSTACK	32000
#  else
#    define LUAI_MAXSTACK	6000
#  endif
#endif

/*
@@ LUAI_MAXCCALLS — głębokość zagnieżdżenia wywołań korzystających ze stosu C.
**
** Wywołania Lua→Lua nie schodzą w rekurencję maszyny wirtualnej, więc ten limit
** dotyczy funkcji natywnych i metametod. Liczy się stosem taska, a nie stertą
** skryptu: przekroczenie objawiłoby się nadpisaniem stosu FreeRTOS-a, czyli
** awarią bez żadnego komunikatu. Wartości dobrane pod domyślny stos taska
** skryptu (HYDRA_SCRIPT_STACK_WORDS w ScriptModule.hpp).
*/
#if !defined(LUAI_MAXCCALLS)
#  if HYDRA_SCRIPT_LARGE_PROFILE
#    define LUAI_MAXCCALLS	120
#  else
#    define LUAI_MAXCCALLS	48
#  endif
#endif

/*
@@ LUAL_BUFFERSIZE — bufor roboczy lauxlib, leżący na stosie C.
**
** Używają go string.format, table.concat i sklejanie napisów. Upstream liczy
** go jako 16*sizeof(void*)*sizeof(lua_Number), co przy 32-bitowych wskaźnikach
** i float daje 256 bajtów — akurat. Zapisane wprost, żeby zmiana reprezentacji
** liczb nie przesunęła po cichu zużycia stosu taska.
*/
#if !defined(LUAL_BUFFERSIZE)
#  if HYDRA_SCRIPT_LARGE_PROFILE
#    define LUAL_BUFFERSIZE	512
#  else
#    define LUAL_BUFFERSIZE	256
#  endif
#endif

/*
** ===================================================================
** Wyjście
** ===================================================================
**
** Upstream kieruje print() do stdout, a komunikaty krytyczne do stderr.
** Na urządzeniu nie ma ani jednego, ani drugiego — jest port szeregowy,
** shell diagnostyczny i bufor logów. Oba strumienie przechwytujemy tutaj
** i przekazujemy warstwie Hydry, która decyduje, gdzie je posłać.
**
** Definicje w lauxlib.h są osłonięte strażnikiem #if !defined, więc nadpisanie
** ich stąd nie wymaga łatania upstreamu.
**
** Funkcje mają linkowanie C, bo woła je kod Lua kompilowany jako C;
** implementacja siedzi w src/script/LuaOutput.cpp.
*/

#if defined(__cplusplus)
extern "C" {
#endif

/** Strumień zwykły: print() i zapisy bibliotek standardowych. */
void hydraLuaWrite(const char* text, size_t len);
/** Strumień błędów: panic, ostrzeżenia interpretera. Format zawsze z jednym %s. */
void hydraLuaWriteError(const char* format, const char* arg);

#if defined(__cplusplus)
}
#endif

#define lua_writestring(s, l)      hydraLuaWrite((s), (l))
#define lua_writeline()            hydraLuaWrite("\n", 1)
#define lua_writestringerror(s, p) hydraLuaWriteError((s), (const char*)(p))

#endif /* hydra_lua_conf_h */
