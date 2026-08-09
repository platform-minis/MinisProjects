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

## Cel `native` — okno na PC zamiast wsadu

`mcu: native` to pełnoprawny cel budowy, nie tryb podglądu. Chodzi ten sam
rdzeń, te same taski i ta sama magistrala; wymienione są wyłącznie backendy:
atrapy HAL zamiast Arduino, scheduler na pthreadach zamiast FreeRTOS-a, okno
SDL zamiast panelu na SPI.

```yaml
targets:
  podglad:
    mcu: native
    native:
      window: { width: 128, height: 64, scale: 6, format: mono1, title: "OLED" }
```

Rysowanie idzie przez to samo `gfx::ISurface`, co na sprzęcie:

```cpp
static u8 vram[hydra::gfx::SdlDisplay::bytesNeeded(320, 240,
                                                   hydra::gfx::PixelFormat::Rgb565)];
hydra::gfx::SdlDisplay display;
hydra::gfx::SdlDisplay::Cfg cfg;
cfg.width = 320; cfg.height = 240; cfg.scale = 3;
HYDRA_CHECK(display.begin(hydra::ByteSpan{vram, sizeof(vram)}, cfg));

while (display.pump()) {
    draw(display.surface());
    display.surface().flush();
}
```

Przykład: `examples/native-gfx/`.

### Ten cel nie idzie przez kontener

Kontener istnieje po to, żeby wynik **nie** zależał od maszyny. Cel `native`
jest odwrotnością: wynik ma zależeć od maszyny, bo to program dla tego systemu
i tej architektury, zlinkowany z tutejszym SDL. Dlatego buduje go CMake
lokalnie, presetem wybranym po systemie:

```bash
hydra build . --host win-arm64      # albo mac-arm64, win-x64, linux-x64…
cmake --preset native-linux-x64 -D HYDRA_TARGET=podglad
cmake --build --preset native-linux-x64
```

Presety wypisuje generator do `CMakePresets.json`, po jednym na maszynę.
Katalog budowy jest osobny dla każdej (`build/native-win-arm64`), bo jedno
drzewo źródeł bywa widziane naraz przez dwa systemy.

### Katalog uruchomienia jest systemem plików

Backend hostowy rejestruje w HAL-u `IFileSystem` zakorzeniony w katalogu,
**z którego uruchomiono program**:

```cpp
if (hal::Hal::hasFileSystem()) {
    auto file = hal::Hal::fileSystem().open("notatka.txt", hal::OpenMode::Write);
}
```

Pliki powstają obok binarki i otwiera się je zwykłym edytorem — to jest cała
różnica między debugowaniem zapisu na PC a wyjmowaniem karty z urządzenia.
Wyjście poza korzeń (`..`) jest odrzucane, więc program nie skasuje niczego
obok.

Ścieżkę odczytujemy raz. Późniejsze `chdir()` nie przesuwa korzenia: aplikacja
zapisuje tam, gdzie ją uruchomiono, a nie tam, gdzie zawędrowała.

To **jedyny** system plików, który HAL wystawia sam. Na układzie wyboru nie ma
— karta czy flash to decyzja urządzenia — i tam `Hal::hasFileSystem()` zwraca
`false`, dopóki projekt nie zarejestruje własnej implementacji.

Projekt: [`projects/file-journal`](../projects/file-journal/) — dziennik
z odczytem przy starcie i rotacją pliku.

### SDL jest opcjonalny

Brak SDL2 nie zatrzymuje konfiguracji — powstaje build bez okna, a
`SdlDisplay::begin()` zwraca `Err::NotSupported`. Tak wygląda ta sama budowa
w CI i przez ssh. Instalacja: `apt install libsdl2-dev`, `brew install sdl2`,
`vcpkg install sdl2`.

Nagłówki SDL wolno włączać wyłącznie w `src/gfx/sdl/` — reguła pilnowana przez
`tools/check_includes.sh`, ta sama co dla Arduino i LVGL.

### Budowa ze Studia

Studio wykrywa system i architekturę przeglądarki i buduje dla nich, a gotowy
plik pobiera się sam. Dwie rzeczy warto o tym wiedzieć:

* **Wykrywanie architektury bywa niemożliwe.** Windows on ARM podaje w nagłówku
  User-Agent „Win64; x64", a Safari na Apple Silicon — „Intel Mac OS X". Studio
  pyta wtedy o Client Hints, a na macOS sięga po nazwę układu graficznego
  z WebGL. Gdy pewności nie ma, wybiera **x64** — bo ta pomyłka jest odwracalna
  (emulacja), a odwrotna daje plik, który nie uruchomi się w ogóle. Wykryta
  maszyna jest widoczna na pasku stanu i da się ją zmienić.
* **Na Windows pobiera się archiwum**, nie samo `.exe`: SDL2.dll leży
  w katalogu pakietu, a nie przy pliku wykonywalnym.

## Publikacja obrazu

Workflow `hydra-image` buduje i publikuje obraz dwuarchitekturowo do ghcr przy
zmianie w `docker/` albo `platformio.ini`. Serwery pobierają gotowy — budowanie
kilku gigabajtów na każdej maszynie osobno nie ma sensu.
