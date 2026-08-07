# Pułapki

Zebrane w jednym miejscu rzeczy, które już raz kogoś kosztowały wieczór.
Każda jest prawdziwa — wyszła przy budowaniu albo uruchamianiu tego projektu.

## Budowanie

**„`Serial` was not declared" na ESP32-C3.** Układ nie ma USB-OTG, tylko
sprzętowe USB Serial/JTAG. Przy `ARDUINO_USB_CDC_ON_BOOT=1` bez
`ARDUINO_USB_MODE=1` rdzeń nie deklaruje `Serial` **nigdzie**: HardwareSerial
oddaje tę nazwę na rzecz CDC, a HWCDC deklaruje ją dopiero przy tej fladze.

**„#define __FREERTOS 1 to use FreeRTOS" na RP2040/RP2350.** Rdzeń Philhowera
kompiluje FreeRTOS dopiero po tym przełączniku. Nagłówek jądra celowo przerywa
kompilację, żeby powiedzieć to wprost.

**Brakujące zależności przechodnie.** `Updater` → `MD5Builder` → `LittleFS`.
Wypisywanie ich po jednej to droga donikąd — właściwe narzędzie to
`lib_ldf_mode = deep+`.

**Pakiet jednej platformy w budowie innej.** Filtr `platforms` w `library.json`
**nie działa** dla zależności z rejestru. Rozwiązaniem jest
`lib_compat_mode = strict` plus deklaracja w środowisku celu.

**PlatformIO przeczesuje cały dysk.** `lib_extra_dirs` ze ścieżką względną:
w kontenerze projekt leży pod `/project`, więc zapisane `../..` wskazuje
katalog główny. Ścieżka idzie przez zmienną `HYDRA_LIB_DIR`, nie przez plik.

**`lib_extra_dirs` wskazuje katalog zawierający bibliotekę**, a nie samą
bibliotekę. Podmontowanie Hydry wprost pod `/hydra` sprawiało, że PlatformIO
jej nie widziało, a konsolidacja kończyła się brakiem symboli.

**„Source `ota_2app.csv' not found".** Schemat partycji podaje się nazwą
logiczną, nie plikiem — właściwa tablica zależy od rozmiaru pamięci i wersji
rdzenia. `ota_2app` przy 16 MB to `default_16MB.csv`.

**Nagłówek płytki nie znaleziony.** PlatformIO dodaje na ścieżkę włączeń tylko
`include/` i `src/`. Katalog projektu trzeba dołożyć: `-I $PROJECT_DIR`.
Zapis `${platformio.project_dir}` jest błędny — taka opcja nie istnieje
i PlatformIO odrzuca cały plik.

**Atrybut przerwania w złym miejscu.** `void HYDRA_ISR_ATTR Klasa::metoda()`
GCC odrzuca. Atrybut musi poprzedzać typ zwracany.

**`IRAM_ATTR` nieznany w warstwach frameworka.** Symbol pochodzi z ESP-IDF
i normalnie przychodzi przez nagłówki Arduino, których rdzeń Hydry nie widzi.
`Config.hpp` sięga po `esp_attr.h` wprost, z wariantem zapasowym.

## Docker

**„Path is not writable" przy budowie projektu.** Maszyna wirtualna Dockera
widzi tylko wybrane katalogi hosta — colima domyślnie udostępnia katalog
domowy. Projekt spoza tej listy montuje się jako pusty katalog należący do
roota. `docker/hydra.sh` sprawdza to i mówi wprost.

**Artefakty należące do roota.** Kontener uruchamiany bez `--user` pisze jako
root; potem trzeba `chown -R`, a sprzątanie wymaga `sudo`. Uruchomienie jako
wywołujący usuwa to u źródła.

**TSan przerywa działanie zaraz po starcie.** Potrzebuje wyłączyć losowanie
układu pamięci, co blokuje domyślny profil seccomp. Wymagane
`--security-opt seccomp=unconfined --cap-add SYS_PTRACE`.

**Binarka z macOS uruchamiana w kontenerze.** Katalog `build/` współdzielony
przez montowanie: Linux uznaje wynik kompilacji z macOS za aktualny. Katalogi
budowy są rozdzielone po systemie i architekturze.

## Kod

**`HYDRA_TRY` w argumencie innego makra.** GCC nadaje wszystkim wystąpieniom
numer linii domykającej całe wywołanie — trzy użycia w domknięciu przekazanym
do `HYDRA_CHECK` dostawały tę samą nazwę zmiennej. Clang numeruje inaczej,
więc na hoście problem nie występował. Rozwiązanie: `__COUNTER__`.

**Makra o nazwach pól struktury.** pico-sdk definiuje `i2c0`, `spi0`, `uart0`.
Pole struktury o takiej nazwie rozwija się w środku deklaracji i daje błędy
wskazujące na nagłówki SDK.

**`portYIELD_FROM_ISR` ma różne sygnatury.** Na ESP32 bez argumentu, na portach
ARM z flagą.

**Brakujące nagłówki standardowe.** Clang na macOS ciągnie `atol`, `strtol`
i `snprintf` przechodnio, GCC na Linuksie nie.

**`= Setup{}` jako domyślny argument** dla typu zagnieżdżonego nie kompiluje
się w ciele klasy w C++17. Trafiło się trzykrotnie; rozwiązanie to dodatkowe
przeciążenie.

## Pliki YAML

**Przecinek w niecytowanej wartości zapisu jednowierszowego.**
`{ description: Kanał A, kierunek 1 }` rozpada się na dwa klucze. W zapisie
`{ }` przecinek, dwukropek i klamry rozdzielają wpisy — wartość, która je
zawiera, musi być w cudzysłowach.

**Usunięcie nazwy pola bez wcięcia.** Zostawia wiersz z samymi spacjami,
a YAML wchłania następną sekcję do poprzedniej. Usuwając pole, usuwaj cały
wiersz.

**„off" jako wartość tekstowa.** YAML czyta to jako fałsz. Nazwa hosta `off`
wymaga cudzysłowów; `2.0.0` już nie, bo dwie kropki to nie liczba.

## Integracja z edytorem

**MUI 6 nie udostępnia podmodułów przez `exports`.** Node nie potrafi wczytać
`@mui/material/Box` bezpośrednio, bundler potrafi. Panele Studia ładowane są
leniwie, co rozwiązuje to i przy okazji oszczędza edytorowi wczytywania
całego interfejsu.

**Sprawdzanie typów pod niewłaściwą wersję.** Pakiet miał React 19 i MUI 7,
aplikacja daje React 18.2 i MUI 6.5. Weryfikacja pod nowsze API niczego nie
dowodzi — błąd wyszedłby dopiero u użytkownika.

**`readonly` w kontrakcie modelu Monaco.** Prawdziwe `ITextModel` przyjmuje
tablicę modyfikowalną, więc metoda Monaco nie pasowała do interfejsu
zadeklarowanego z `readonly`. Atrapa w testach spełniała jedno i drugie.
