# Paczki

Paczka spina cztery rzeczy, których nie spina żaden istniejący format:

1. **adapter** — kod łączący bibliotekę producenta z interfejsem Hydry,
2. **komponent** — symbol i wyprowadzenia dla edytora schematów,
3. **schemat konfiguracji** — z niego Studio buduje formularz w inspektorze,
4. **model symulacji** — skąd czujnik bierze dane, gdy nie ma sprzętu.

`library.json` Arduino zna tylko pierwszą pozycję, komponent ESP-IDF też.
Reszta jest luką i to ona jest jedynym powodem, dla którego ten format istnieje.

## Czym paczka nie jest

**Nie jest menedżerem pakietów.** Rejestr, rozwiązywanie zależności, semver,
mirrory, hosting — to osobny produkt na pełen etat, a cmentarzysko porzuconych
menedżerów pakietów w świecie systemów wbudowanych jest spore. Ta warstwa już
istnieje i nazywa się PlatformIO: `lib_deps` ogarnia rejestr Arduino, dowolne
repozytorium gitowe i wersjonowanie.

Paczka **przekazuje robotę w dół**: pole `upstream.lib_deps` trafia wprost do
`platformio.ini`. Pobieraniem, wersjami i pamięcią podręczną zajmuje się
PlatformIO albo git.

## Manifest

```yaml
# packs/bmp280/hydra-pack.yaml
pack: bmp280
version: 1.2.0
description: Ciśnienie i temperatura, I²C
provides: [sense.driver]
requires: [i2c]                    # możliwości płytki, nie rodzina układu

upstream:
  lib_deps: "adafruit/Adafruit BMP280 Library@^2.6"
  build_flags: ["-DBMP280_FAST"]

adapter: src/Bmp280Adapter.cpp
component: bmp280.hcomp
config_schema: bmp280.schema.json
sim: models/atmosphere.lua

defaults:
  address: "0x76, 0x77"            # adresy do wyboru — pierwszy wolny wygrywa
```

### `provides`

`sense.driver` `ui.widget` `ui.display` `motion.motor` `motion.encoder`
`net.transport` `core.extension` `board`

Decyduje o grupie w bibliotece komponentów Studia.

### `requires` opisuje możliwości, nie układ

To jest istotne rozróżnienie. Nucleo-G474RE i płytka z tym samym STM32G4 plus
warstwą fizyczną Ethernetu różnią się tym, **co potrafią**, a nie tym, jaki
mają procesor. Czujnik I²C działa wszędzie, gdzie jest I²C.

Studio wyszarza w bibliotece komponenty niepasujące do wybranego celu
**i podaje powód**:

```
bmp280  →  płytka „nucleo" nie ma: i2c
bmp280  →  układ rp2040 nie ma: wifi — jeśli płytka to ma, wypisz w capabilities
```

Ukrycie byłoby gorsze: użytkownik szukałby czujnika, którego nie widzi.

## Definicja układu (`.hcomp`)

Jedyne miejsce, w którym opisane jest, jakie nóżki ma układ i co każda robi.

```yaml
hcomp: "0.1"
component: bmp280
name: BMP280
package: LGA-8
pins:
  - { name: VCC, kind: power_in,      description: "Zasilanie 1.8–3.6 V" }
  - { name: GND, kind: ground }
  - { name: SDA, kind: bidirectional, bus: i2c, role: sda }
  - { name: SCL, kind: input,         bus: i2c, role: scl }
  - { name: SDO, kind: input,         optional: true }
```

Rodzaje: `input` `output` `bidirectional` `power_in` `power_out` `ground`
`open_drain` `passive` `unconnected`.

`kind` nie jest ozdobnikiem — z niego wynika, czy dwa połączone piny mogą ze
sobą współpracować. Domyślnie każdy pin jest **wymagany**: łatwiej odznaczyć
te kilka opcjonalnych, niż odkryć na płytce, że o którymś zapomniano.

Definicje płytek dokładają `gpio` z numerem wyprowadzenia. To jedyne miejsce
z numerami w całym projekcie — i po nich rozpoznaje się mikrokontroler na
schemacie.

## Schemat konfiguracji

JSON Schema, z którego powstaje formularz w inspektorze:

```json
{
  "type": "object",
  "title": "BMP280 — ciśnienie i temperatura",
  "properties": {
    "address": { "type": "string", "enum": ["0x76", "0x77"], "default": "0x76",
                 "description": "Wybierany zworką SDO" },
    "period_ms": { "type": "integer", "minimum": 10, "maximum": 60000,
                   "default": 1000, "unit": "ms" }
  },
  "required": ["address"]
}
```

Obsługiwany jest podzbiór: typy proste, `enum`, zakresy, `default`,
`description`, zagnieżdżone obiekty oraz własne pole `unit`.

Konstrukcja spoza podzbioru (`oneOf`, `$ref`, warunki) **nie unieważnia
schematu** — takie pole trafia do edycji tekstowej, a Studio wypisuje jego
nazwę. Odrzucenie pliku zablokowałoby cały komponent, a cisza wyglądałaby jak
usterka inspektora.

Wartość domyślna jest pokazywana jako podpowiedź, ale nie udaje zapisanej —
inaczej nie dałoby się odróżnić ustawienia świadomego od pominiętego.

## Projekt to nie paczka

Częste pytanie: czy projekt w `projects/` potrzebuje `hydra-pack.yaml`? Nie —
i asymetria jest celowa:

| Plik | Opisuje | Kto go ma |
|---|---|---|
| `.hydra` | urządzenie, które budujesz | projekt |
| `hydra-pack.yaml` | rzecz wielokrotnego użytku | paczka |

Projekt **korzysta** z paczek, paczka jest **udostępniana**. Płytka jest
paczką (`provides: [board]`), bo używa jej wiele projektów; łazik nie jest,
bo jest jeden.

Nic nie stoi na przeszkodzie, żeby katalog był jednym i drugim — na przykład
projekt referencyjny, którego definicję płytki chcesz udostępnić dalej. Wtedy
leżą tam oba pliki i opisują dwie różne rzeczy.

## Użycie w projekcie

```yaml
dependencies:
  bmp280: { path: ../../packs/bmp280 }   # katalog
  vl53l0x: "^1.0.0"                       # domyślnie packs/vl53l0x
```

Źródła zdalne (`git`) nie są jeszcze pobierane — zgłaszane jako brak, a nie
pomijane po cichu.

## Dystrybucja

Paczka to katalog albo adres gitowy. Rejestru nie ma i na razie nie potrzeba:
gdyby kiedyś był potrzebny, wystarczy jedno repozytorium-indeks z listą
manifestów, wzorem Homebrew tap czy rejestru vcpkg. Zero infrastruktury
serwerowej.

Format manifestu warto mieć **teraz**, nawet jeśli przez rok wszystkie paczki
będą żyły w tym repozytorium — dorabianie go później do istniejących
sterowników to refaktor wszystkiego.

## Paczki w tym repozytorium

| Paczka | Co wnosi |
|---|---|
| `bmp280`, `ina219`, `as5600` | sterowniki czujników z definicją układu i schematem konfiguracji |
| `drv8833` | sterownik dwóch silników |
| `esp32s3-devkitc-1` | definicja płytki — jedyne miejsce z numerami wyprowadzeń |
| `resistor`, `power-input` | elementy bierne i złącze; potrzebne regułom elektrycznym |
