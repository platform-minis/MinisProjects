/*
 * Jednostka kompilacji dla pocketmoda.
 *
 * Biblioteka jest nagłówkiem z ciałem pod `POCKETMOD_IMPLEMENTATION` — ten
 * plik jest jedynym miejscem, w którym ta definicja jest ustawiona. Bez niego
 * każda jednostka włączająca nagłówek dostawałaby własną kopię funkcji,
 * a konsolidator zgłaszałby zduplikowane symbole.
 *
 * Kompilowany jako C, nie C++: to zwykłe C99 i nie ma powodu przepuszczać go
 * przez inne reguły przeciążania i rzutowania.
 */
#define POCKETMOD_IMPLEMENTATION
#include "pocketmod.h"
