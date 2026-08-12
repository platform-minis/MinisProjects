# pocketmod — osadzony odtwarzacz modułów Amigi

Źródło: <https://github.com/rombankzero/pocketmod>, licencja MIT (pełna treść
na końcu `pocketmod.h`).

Jeden nagłówek, 871 wierszy, bez zależności poza biblioteką standardową C.
Kontekst odtwarzacza waży **1728 bajtów** — mieści się w RAM-ie każdej płytki,
na którą Hydra celuje, i dlatego biblioteka leży w `src/`, a nie jako osobny
pakiet PlatformIO jak WAMR czy TFLM. Granicą jest tam kilkadziesiąt tysięcy
wierszy trafiających do każdego projektu; tutaj chodzi o jeden plik.

## Aktualizacja

    curl -L -o src/pocketmod/pocketmod.h \
        https://raw.githubusercontent.com/rombankzero/pocketmod/master/pocketmod.h

Po podmianie sprawdź `make -C test`: `ModSource` zakłada, że `pocketmod_render`
zwraca pary `float` w zakresie [-1, 1] i że kontekst da się umieścić w polu
o rozmiarze `HYDRA_MOD_CONTEXT_BYTES`.

## Czego pocketmod nie robi

**Nie czyta plików.** Dostaje wskaźnik na cały moduł w pamięci i tam zostaje —
`ModSource` wymaga więc bufora, który przeżyje odtwarzanie. Na urządzeniu
oznacza to wczytanie pliku z VFS do RAM-u albo wskazanie tablicy w pamięci
programu; moduł z lat 90. to zwykle 50–300 kB, więc na małych płytkach jest to
decyzja projektowa, a nie szczegół.

**Nie miksuje do mono.** Wyjście jest zawsze stereo; zejście do jednego kanału
należy do miksera, bo uśrednienie i wybór kanału to dwie różne decyzje.

## Dlaczego w `src/`, a nie osobną biblioteką

WAMR i TFLM leżą w osobnych pakietach PlatformIO, bo PlatformIO kompiluje całe
`src/` i kilkadziesiąt tysięcy wierszy trafiałoby do każdego projektu. Tutaj
chodzi o **jeden plik i 871 wierszy**, a kontekst odtwarzacza waży 1728 bajtów.
Granica przebiega gdzieś między tymi wielkościami i pocketmod jest po tej
tańszej stronie.
