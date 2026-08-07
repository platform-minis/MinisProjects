# Środowisko budowania

Wsady na pięć platform buduje obraz Dockera z wpieczonym PlatformIO Core
i kompletem toolchainów. Sama budowa nie rusza wtedy sieci, a to, co powstaje
na stanowisku i w CI, wychodzi z tego samego obrazu.

## Szybki start

```bash
./docker/hydra.sh pull                    # gotowy obraz z rejestru
./docker/hydra.sh test                    # testy hostowe, sanitizery, dokumentacja
./docker/hydra.sh fw esp32s3 blink-task   # jeden wsad
./docker/hydra.sh ci                      # komplet: testy + wszystkie wsady
```

Na serwerze to samo przez compose, bez zakładania konsoli:

```bash
UID=$(id -u) GID=$(id -g) docker compose -f docker/compose.yaml run --rm ci
```

Obraz jest dwuarchitekturowy — `linux/amd64` dla typowego serwera i
`linux/arm64` dla Maca z układem Apple oraz Windows 11 arm64 pod WSL.

## Dlaczego obraz, a nie instrukcja instalacji

Przed nim istniał obraz `mycastle-pico:local`, do którego odwołuje się
`src/RPiPico2DisplayDVI/build.sh` — i którego **nie ma w repozytorium**. Kto
stracił obraz, stracił możliwość zbudowania tamtego projektu. Dockerfile Hydry
jest wersjonowany razem z kodem, a wersje platform są w nim przypięte, bo obraz
zbudowany za miesiąc bez przypięcia zawierałby inny kompilator niż ten, na
którym wsad był sprawdzony.

Trzy rzeczy przejęte wprost z waszych wcześniejszych obrazów: katalog danych
pod `/opt` otwarty dla dowolnego UID, `WORKDIR /workspace` i uruchamianie
kontenera jako wywołujący (`--user`). To ostatnie usuwa u źródła potrzebę
`chown -R` po budowie i `sudo rm -rf` przy sprzątaniu.

## Platformy

| Cel | Platforma | Rdzeń | Uwagi |
|---|---|---|---|
| esp32s3 | espressif32 | arduino-esp32 | FreeRTOS z ESP-IDF, działa od resetu |
| esp32c3 | espressif32 | arduino-esp32 | jeden rdzeń RISC-V, pinowanie ignorowane |
| pico | fork platform-raspberrypi | earlephilhower | brak FPU — regulatory na Q16.16 |
| pico2 | fork platform-raspberrypi | earlephilhower | RP2350, FPU jest |
| stm32g4 | ststm32 | stm32duino | scheduler startuje ręcznie w `App::begin()` |

RP2040 i RP2350 wymagają **forka** platformy (`maxgerhardt/platform-raspberrypi`),
bo oficjalna ich nie pokrywa. To nie wyjątek, tylko normalny stan: PlatformIO
nadąża za tym, co popularne, a nie za tym, co nowe.

## Ustawienia, bez których build się nie udaje

Każde z nich kosztowało nieudaną kompilację:

| Gdzie | Co | Dlaczego |
|---|---|---|
| wszystkie | `lib_ldf_mode = deep+` | bez tego nie znajdzie zależności przechodnich (`Updater` → `MD5Builder` → `LittleFS`) |
| wszystkie | `lib_compat_mode = strict` | bez tego pakiet jednej platformy trafia do budowy innej |
| ESP32-C3 | `ARDUINO_USB_MODE=1` | inaczej rdzeń **nie deklaruje `Serial` w ogóle** |
| RP2040/RP2350 | `__FREERTOS=1` | nagłówek jądra celowo przerywa kompilację bez tego |
| RP2040/RP2350 | `board_build.core = earlephilhower` | inny rdzeń nie ma FreeRTOS |
| STM32 | FreeRTOS w `lib_deps` **środowiska** | filtr platform nie działa dla zależności z rejestru |
| ESP32 z PSRAM | `BOARD_HAS_PSRAM` obok `psram_type` | sam typ pamięci nie wystarcza |

Generator Studia zna to wszystko — patrz `src/model/emit/mcu.ts` w pakiecie
`@mhersztowski/hydra-studio`. Nikt nie musi odkrywać tego po raz drugi.

## Trzy sposoby budowania

### Przykłady frameworka

```bash
tools/build_example.sh esp32s3 blink-task
```

Wywołują to i `docker/hydra.sh fw`, i CI — jeden skrypt, żeby obie ścieżki nie
mogły się rozjechać. Wcześniej miały własne kopie i zdążyły. Skrypt wyprowadza
potrzebne moduły z włączeń w kodzie przykładu, dobiera plik płytki i pomija
kombinacje bez sensu (sieć na Nucleo bez radia) — zawsze z wypisanym powodem.

### Projekt z pliku `.hydra`

```bash
hydra gen  ~/projekt --hydra /ścieżka/do/Hydry
hydra build ~/projekt -e esp32s3
```

Generuje `platformio.ini`, `CMakeLists.txt` i `boards/*.hpp`, potem przekazuje
budowę do `docker/hydra.sh project`. Szczegóły: [project-file.md](project-file.md).

### Ręcznie, w kontenerze

```bash
./docker/hydra.sh project ~/projekt pio run -e esp32s3
```

Montuje projekt pod `/project`, a Hydrę pod `/hydra/Hydra` i ustawia
`HYDRA_LIB_DIR`. Ścieżka do biblioteki **nie** jest zapisywana w
`platformio.ini` na sztywno: ścieżka względna jest prawdziwa tylko na maszynie,
na której powstała — w kontenerze projekt leży pod `/project` i zapisane `../..`
wskazywało katalog główny, przez co PlatformIO zaczynało przeczesywać cały dysk.

## CMake

`CMakeLists.txt` jest **drugim równorzędnym wynikiem**, nie etapem pośrednim
w drodze do PlatformIO. Otwiera pico-sdk, ESP-IDF, Zephyra i build hostowy.

```bash
cmake -B build -D HYDRA_TARGET=esp32s3-main -D HYDRA_ROOT=/ścieżka/do/Hydry
cmake --build build --target hydra
```

Odwrotny kierunek nie działa: CMake nie zna pojęcia płytki PlatformIO,
frameworka, partycji ani portu wgrywania. Przepchnięcie tego przez niego
oznaczałoby zapisanie danych z `.hydra` w zmiennych CMake wyłącznie po to, by
zaraz je stamtąd wyjąć.

## Publikacja obrazu

Workflow `hydra-image` buduje i publikuje obraz dwuarchitekturowo do ghcr przy
zmianie w `docker/` albo `platformio.ini`. Serwery pobierają gotowy — budowanie
kilku gigabajtów na każdej maszynie osobno nie ma sensu.
