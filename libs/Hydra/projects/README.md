# Projekty przykładowe

Kompletne projekty pokazujące pracę z Hydrą — w odróżnieniu od `examples/`,
gdzie leżą urywki kodu ilustrujące pojedyncze fragmenty API.

Każdy z nich jest prawdziwym projektem: plik `.hydra` jest źródłem prawdy,
`platformio.ini`, `CMakeLists.txt` i `boards/*.hpp` się z niego **generują**
(dlatego nie ma ich w repozytorium), a wsad daje się zbudować.

Progresja jest celowa — każdy następny da się przeczytać, znając poprzedni.

| Projekt | Wierszy `.hydra` | Co dokłada |
|---|---|---|
| [hello-blink](hello-blink/) | 25 | jeden cel, jeden moduł — czym w ogóle jest plik projektu |
| [weather-station](weather-station/) | 130 | paczki, układy na magistrali, telemetria, dwa warianty sprzętu, symulacja |
| [rover](rover/) | 190 | schemat → `boards/*.hpp`, napęd z pętlą czasu rzeczywistego, trzy warianty, farma testowa |

## Jak z nich korzystać

```bash
cd projects/hello-blink
hydra check .                          # sprawdza projekt i manifesty paczek
hydra plan .                           # pokazuje cele i wyprowadzone ustawienia
hydra gen . --hydra ../..              # generuje pliki budowania
hydra build . -e esp32s3               # buduje wsad w kontenerze
```

W Studiu wystarczy otworzyć plik `.hydra` — interfejs uruchamia się sam.

## Paczki

Wszystkie trzy projekty sięgają po wspólne paczki z [`../packs/`](../packs/):

| Paczka | Co wnosi |
|---|---|
| `bmp280`, `ina219`, `as5600` | sterowniki czujników: definicja układu, schemat konfiguracji do inspektora |
| `drv8833` | sterownik silników |
| `esp32s3-devkitc-1` | definicja płytki — **jedyne miejsce z numerami wyprowadzeń** |
| `resistor`, `power-input` | elementy bierne i złącze; potrzebne regułom elektrycznym |

Paczka spina cztery rzeczy, których nie spina żaden istniejący format: adapter
nad interfejsem Hydry, komponent do edytora schematów, schemat konfiguracji dla
inspektora i model symulacji. Prawdziwą bibliotekę producenta pobiera
PlatformIO — patrz pole `upstream` w manifeście.

## Czego te projekty uczą

**hello-blink** — że plik projektu może mieć dwadzieścia pięć wierszy i już
wystarczyć. Pokazuje cały cykl: opis → generowanie → wsad.

**weather-station** — co się zmienia, gdy dochodzą czujniki. Rejestracja
w hubie razem z filtrem i progami wiarygodności, pomiary przez magistralę
zdarzeń zamiast odpytywania, drugi wariant płytki z wyłączonym modułem OTA,
sekrety poza repozytorium. Sekcja `simulation` pozwala rozwijać telemetrię,
zanim cokolwiek zostanie przylutowane.

**rover** — pełny obieg. Wyprowadzenia biorą się ze schematu, więc numer
w kodzie i ścieżka na płytce nie mogą się rozjechać. Trzy warianty sprzętu
z jednego opisu, każdy z innym zestawem modułów — Pico 2 bez radia ma sieć
wyłączoną, Nucleo bez ekranu ma wyłączony interfejs. Pętla napędu ma jawny
termin i reakcję na jego przekroczenie. Sekcja `test.hil` opisuje, co i na
czym sprawdzać w nocy.
