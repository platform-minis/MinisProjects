# Płytki i platformy

Piny nazywa się w jednym miejscu. Reszta kodu mówi `hal::board::led` albo
`Pin::MotorLeftPwm` — nigdy liczbą. Ten dokument opisuje, gdzie to miejsce jest
i ile kosztuje dołożenie nowej płytki albo całej platformy.

## Trzy poziomy kosztu

| Sytuacja | Co trzeba napisać |
|---|---|
| Ta sama platforma, inne piny | jeden nagłówek w `include/hydra/boards/` |
| Ten sam framework, inne MCU | nagłówek płytki + wpis środowiska w `platformio.ini` |
| Zupełnie inny framework (pico-sdk, ESP-IDF, Zephyr) | backend HAL + backend RTOS |

Trzeci przypadek to **11% kodu** — 1183 z 11004 linii `src/`. Pozostałe 89%
nie wie, co jest pod spodem, i CI tego pilnuje. Port na nowy SDK to praca na
dzień albo dwa, nie przepisywanie frameworka.

## Nagłówek płytki

```c
#define HYDRA_BOARD_NAME "rover-s3"
#define HYDRA_BOARD_LED 48

#define HYDRA_BOARD_I2C0_ENABLE 1
#define HYDRA_BOARD_I2C0_SDA 8
#define HYDRA_BOARD_I2C0_SCL 9
#define HYDRA_BOARD_I2C0_HZ 400000

namespace Pin {
constexpr ::hydra::hal::PinNum MotorLeftPwm = 17;
}
```

Wskazuje się go flagą kompilacji:

```
-D HYDRA_BOARD_HEADER='"hydra/boards/esp32s3_pico.hpp"'
```

### Numery, nie nazwy wariantu

W nagłówku muszą być **liczby**, a nie `LED_BUILTIN` czy `PA5`. Powód: plik
trafia do każdej jednostki kompilacji, także takiej, która nie widzi nagłówków
Arduino — i widzieć ich nie może, bo zabrania tego reguła zależności. Na
Nucleo-G474RE `LED_BUILTIN` rozwija się do 13; tyle właśnie stoi w nagłówku,
z komentarzem, że to D13 czyli PA5.

### Generowanie ze schematu

Nagłówek pisany ręcznie i ścieżka na płytce to dwa niezależne zapisy tej samej
rzeczy — rozjeżdżają się po pierwszej poprawce, której ktoś nie przeniósł,
i objawiają jako urządzenie, które się kompiluje i nie działa.

Dlatego jeśli projekt ma schemat, nagłówek **powstaje z połączeń**: sieć
`I2C0_SDA` dotyka pinu `IO8`, więc `HYDRA_BOARD_I2C0_SDA` to 8. Szczegóły:
[schematic.md](schematic.md).

## Dodanie płytki na znanej platformie

1. Skopiuj najbliższy nagłówek z `include/hydra/boards/`.
2. Popraw nazwy i numery.
3. Dopisz środowisko w `platformio.ini` albo cel w pliku `.hydra`.

Nagłówek może deklarować możliwości płytki:

```c
#define HYDRA_BOARD_HAS_WIFI 1
#define HYDRA_BOARD_HAS_PSRAM 1
```

Studio używa ich do wyszarzania komponentów, które do tej płytki nie pasują —
zawsze z podanym powodem.

## Dodanie platformy

Trzeba dostarczyć dwa backendy:

**Backend HAL** (`src/hal/<nazwa>/`) — implementacje `IGpio`, `II2cBus`,
`ISpiBus`, `IUart`, `IPwm`, `IAdc`, `IStorage`, `ITime` oraz funkcję montującą
je w `Hal`. Wzorzec: `src/hal/arduino/`.

**Backend RTOS** (`src/core/rtos_<nazwa>.cpp`) — taski, kolejki, sekcje
krytyczne, czas. Wzorce: `rtos_freertos.cpp` i `rtos_host.cpp`.

Do tego wpis w profilach układów Studia, jeśli ma je znać generator.

### Na co uważać

Trzy rzeczy, które wyszły przy pierwszych prawdziwych buildach i wyjdą znowu:

**Makra o tej samej nazwie co pola.** pico-sdk definiuje `i2c0`, `spi0`,
`uart0` jako makra. Pole struktury o takiej nazwie rozwija się w środku
deklaracji i daje błędy wskazujące na nagłówki SDK, nie na twój kod.

**Sygnatury makr różnią się między portami.** `portYIELD_FROM_ISR()` na ESP32
nie przyjmuje argumentu, na portach ARM przyjmuje flagę. Jedno wywołanie nie
obsłuży obu.

**Atrapy nagłówków dają fałszywą pewność.** Kontrola składni na własnoręcznie
napisanych atrapach przepuszczała kod, który przy prawdziwej budowie się nie
kompilował — bo atrapa deklarowała `setRX` na klasie bazowej, a rdzeń ma je
na klasie pochodnej. Atrapa ma odwzorowywać rdzeń, a nie ułatwiać życie.

## Jak wybrać płytkę w projekcie

W pliku `.hydra`:

```yaml
targets:
  main:
    mcu: esp32s3
    board: boards/rover_s3.hpp
    platformio: { board: esp32-s3-devkitc-1 }
    capabilities: [i2c, spi, wifi, psram]
```

`mcu` wybiera profil (platforma, toolchain, flagi wymagane przez układ),
`platformio.board` — konkretną płytkę, `board` — plik z pinami. `capabilities`
opisuje, co ta płytka potrafi; brak listy oznacza „nie wiadomo" i wtedy
możliwości bierze się z profilu układu.

Rozróżnienie ma znaczenie: profil opisuje **układ**, deklaracja opisuje
**płytkę**. STM32G4 nie ma radia, ale płytka z tym układem mogła dostać moduł
Wi-Fi na magistrali — dlatego brak możliwości wyprowadzony z profilu jest
ostrzeżeniem, a z deklaracji błędem.
