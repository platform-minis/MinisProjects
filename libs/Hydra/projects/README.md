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
| [wav-player](wav-player/) | 82 | odtwarzacz `test.wav` z ekranem; trzy cele, jedno źródło |
| [file-journal](file-journal/) | 50 | dziennik na plikach: odczyt przy starcie, dopisywanie, rotacja |
| [media-player](media-player/) | 72 | potok audio i ekran z przyciskami; źródło i ujście wybiera cel |
| [desktop-preview](desktop-preview/) | 71 | cel `native`: ten sam ekran w oknie SDL na PC i na OLED-zie |
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

Cel `native` idzie inną drogą — buduje go CMake lokalnie, bo jego wynik ma
zależeć od maszyny (to program dla tego systemu i tej architektury, zlinkowany
z tutejszym SDL). Maszynę wskazuje `--host`, a domyślnie jest nią ta, na której
stoimy:

```bash
cd projects/desktop-preview
hydra gen   . --hydra ../..
hydra build . --hydra ../.. -e podglad                    # okno na tej maszynie
hydra build . --hydra ../.. -e podglad --host win-arm64   # dla innej
```

Poza `platformio.ini` i `CMakeLists.txt` generuje się wtedy także
`CMakePresets.json` — po jednym presecie na maszynę.

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

**desktop-preview** — że maszyna deweloperska jest celem budowy jak każdy
inny. Jedna funkcja rysująca przyjmuje `gfx::ISurface` i nie wie, czy pod
spodem jest okno, czy panel na I²C — dzięki temu ekran projektuje się w pętli
„popraw i zobacz" trwającej sekundy, zamiast „skompiluj, wgraj, zmruż oczy".
Pokazuje też, gdzie przebiega granica wątków: task liczy dane, pętla rysująca
maluje, i jest to ta sama granica na obu celach.

**wav-player** — że „ten sam kod wszędzie" znaczy naprawdę wszędzie: plik,
graf, przycisk i pasek postępu są wspólne dla trzech celów, a różni je jeden
obiekt — ujście. Pokazuje też, dlaczego format musi pochodzić **z pliku**,
a nie z konfiguracji: nagłówek WAV odczytujemy przed przygotowaniem potoku,
bo od niego zależy i ustawienie kontrolera I2S, i rozmiar bloku w puli.
Plik 44,1 kHz odtworzony jako 16 kHz brzmi jak nagranie zwolnione i wygląda
na usterkę przetwornika.

**file-journal** — pełny cykl życia danych na nośniku, a nie sam `write()`:
odczyt poprzedniego przebiegu przy starcie, dopisywanie w trakcie i rotacja,
gdy plik urośnie. Na celu `native` dziennik ląduje w katalogu, z którego
uruchomiono program, więc format zapisu poprawia się z edytorem otwartym obok,
zamiast wyjmowania karty. Pokazuje też, dlaczego plik zamyka się po każdym
wpisie: uchwyt trzymany między wpisami oznacza dane w buforze systemu,
których nie ma na nośniku w chwili zaniku zasilania.

**media-player** — że różnica między celami mieści się w dwóch obiektach,
a nie w dwóch programach. Na PC dźwięk idzie z wejścia karty i wraca na
głośniki, na układzie — z pliku WAV na wzmacniacz I2S; graf, pula, sterowanie
i cały interfejs są wspólne. Pokazuje też, po co są domeny czasowe: blok audio
trwa dwanaście milisekund, a narysowanie ekranu dłużej, więc wspólny task
oznaczałby przerwę w dźwięku przy każdej klatce.

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
