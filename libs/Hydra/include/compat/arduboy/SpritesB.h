/**
 * @file SpritesB.h
 * @brief Wariant `Sprites` mniejszy kosztem szybkości.
 *
 * W oryginale to osobna implementacja: ten sam interfejs, o kilkaset bajtów
 * mniejszy kod, wolniejsze rysowanie. Wybór miał sens przy 32 KB pamięci
 * programu na ATmega. Na naszych celach nie ma czego oszczędzać, więc obie
 * nazwy prowadzą do jednej implementacji — gra korzystająca z `SpritesB`
 * zadziała, a nawet będzie szybsza.
 */
#ifndef HYDRA_COMPAT_SPRITESB_H
#define HYDRA_COMPAT_SPRITESB_H

#include "Arduboy2.h"

using SpritesB = hydra::arduboy::Sprites;

#endif  // HYDRA_COMPAT_SPRITESB_H
