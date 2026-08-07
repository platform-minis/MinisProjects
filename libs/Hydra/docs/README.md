# Dokumentacja Hydry

Framework robotyczno-IoT dla ESP32, RP2040/RP2350 i STM32 wraz ze środowiskiem
projektowym. Ta strona jest mapą — każdy dokument da się czytać osobno.

## Od czego zacząć

Jeśli chcesz **napisać wsad** i nie interesuje cię reszta, wystarczą dwa:
[architektura](architecture.md), żeby wiedzieć, gdzie co leży, i
[przegląd API](api.md) jako podręczna ściąga.

Jeśli **dokładasz płytkę albo platformę**, zacznij od
[płytek i platform](boards.md) — jest tam koszt każdego z trzech przypadków.

Jeśli **coś nie działa**, [pułapki](troubleshooting.md) zbierają wszystko,
co już raz kogoś kosztowało wieczór.

## Framework

| Dokument | Zawartość |
|---|---|
| [architecture.md](architecture.md) | Warstwy, reguły zależności, decyzje projektowe i ich powody |
| [api.md](api.md) | Przegląd publicznego API warstwa po warstwie |
| [boards.md](boards.md) | Pliki płytek, dodawanie nowej płytki i nowej platformy |
| [build.md](build.md) | Środowisko budowania: PlatformIO, Docker, CMake |
| [testing.md](testing.md) | Testy hostowe, sanitizery, atrapy, testy na sprzęcie |
| [troubleshooting.md](troubleshooting.md) | Znane pułapki i ich przyczyny |

## Środowisko projektowe

| Dokument | Zawartość |
|---|---|
| [project-file.md](project-file.md) | Format `.hydra` — pełne odniesienie |
| [packs.md](packs.md) | Paczki: co wnoszą i czego celowo nie robią |
| [schematic.md](schematic.md) | Schemat `.hsch`, reguły elektryczne, generowanie nagłówka płytki |
| [studio.md](studio.md) | Hydra Studio: wtyczka edytora, panele, obieg pracy |

Kod Studia mieszka w innym repozytorium — `MyCastle/packages/hydra-studio`,
bo jest wtyczką tamtejszego edytora. Formaty, które czyta, opisane są tutaj,
przy frameworku, którego dotyczą.

## Przykłady

| Miejsce | Co to jest |
|---|---|
| [`../examples/`](../examples/) | Urywki kodu — pojedyncze fragmenty API, budowane przez CI na pięciu platformach |
| [`../projects/`](../projects/) | Kompletne projekty z plikiem `.hydra`, od 25-wierszowego `hello-blink` po `rover` ze schematem |
| [`../templates/starter/`](../templates/starter/) | Punkt wyjścia do skopiowania |

## Zasada, która obowiązuje wszędzie

Fragmenty kodu w [api.md](api.md) są **kompilowane** przez `make -C test docs`.
Dokumentacja opisująca nieistniejące funkcje wygląda wiarygodnie i kosztuje
czytelnika godzinę, zanim zorientuje się, że problem jest w tekście, a nie
w jego kodzie. Przy pierwszym uruchomieniu tego sprawdzenia osiem przykładów
z pamięci opisywało API, którego nie ma.
