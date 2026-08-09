/**
 * @file EEPROM.h
 * @brief Pamięć nieulotna — najwyższe wyniki i ustawienia gier.
 *
 * Gry na Arduboya zapisują tu rekordy i konfigurację, zwykle od adresu 16
 * wzwyż (niżej oryginał trzymał ustawienia systemowe). Interfejs jest
 * arduinowy: `EEPROM.read()`, `EEPROM.write()`, `EEPROM.get()`, `EEPROM.put()`.
 *
 * Nośnikiem jest plik `eeprom.bin` w katalogu roboczym, o ile projekt podpiął
 * system plików; inaczej dane żyją w pamięci do wyłączenia programu. Rozmiar
 * to 1024 bajty oryginału — gra licząca na `EEPROM.length()` dostanie tę samą
 * liczbę co na sprzęcie, więc jej rachunki adresów pozostaną poprawne.
 */
#ifndef HYDRA_COMPAT_EEPROM_H
#define HYDRA_COMPAT_EEPROM_H

#include "Arduboy2.h"
#include "hydra/arduboy/Eeprom.hpp"

/**
 * Nazwa `EEPROM` jako makro, nie obiekt.
 *
 * Gra pisze `EEPROM.read(0)`, a to rozwija się do wywołania funkcji zwracającej
 * jedyną instancję. Dzięki temu obiekt powstaje przy pierwszym użyciu i nie
 * ma go w programach, które pamięci nieulotnej nie tykają — a jednocześnie
 * runtime sięga po tę samą instancję bez włączania tego nagłówka.
 */
#define EEPROM (hydra::arduboy::eeprom())

#endif  // HYDRA_COMPAT_EEPROM_H
