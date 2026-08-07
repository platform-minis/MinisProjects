# Plik projektu `.hydra`

Źródło prawdy dla urządzenia. Z niego powstają `platformio.ini`,
`CMakeLists.txt` i `boards/*.hpp` — a Studio czyta go, żeby zbudować formularze
inspektora, ocenić zgodność komponentów z płytką i zasilić symulację.

Plik jest YAML-em i pozostaje tekstem: da się go poprawić ręcznie, obejrzeć
w recenzji zmian i scalić. Studio zapisuje zmiany **przedziałami tekstu**, więc
kliknięcie w formularzu zmienia jeden wiersz i nie rusza komentarzy ani
wyrównania.

Najkrótszy sensowny plik ma 25 wierszy — patrz
[`projects/hello-blink`](../projects/hello-blink/).

## Szkielet

```yaml
hydra: "0.4"            # wersja schematu pliku

project:                # metadane
targets:                # cele sprzętowe — jeden na wariant urządzenia
modules:                # konfiguracja modułów frameworka
dependencies:           # paczki
hardware:               # magistrale, układy, schemat
simulation:             # skąd czujniki biorą dane bez sprzętu
test:                   # trzy poziomy testów
secrets:                # co ma leżeć poza repozytorium
studio:                 # ustawienia edytora, bez wpływu na wsad
```

Wymagane są tylko `hydra`, `project` i `targets`.

## `project`

```yaml
project:
  name: rover-01            # małe litery, cyfry, kropka, myślnik, podkreślenie
  version: 1.3.0            # wersja semantyczna
  description: "Łazik telemetryczny"
  authors: ["Marcin <m@example.com>"]
  license: MIT
  framework: ">=0.4.2 <0.5" # wymagany zakres wersji frameworka
```

## `targets`

Każdy cel to jedno środowisko budowania. `default` wskazuje ten, który
zbuduje się bez podawania nazwy.

```yaml
targets:
  default: esp32s3-main

  esp32s3-main:
    mcu: esp32s3                  # wybiera profil: platforma, toolchain, flagi
    board: boards/rover_s3.hpp    # plik z pinami
    platformio: { board: esp32-s3-devkitc-1, f_cpu: 240000000 }
    capabilities: [i2c, spi, wifi, psram]   # co ta płytka potrafi
    memory:
      psram: opi                  # off | quad | opi
      flash: 16MB
      partitions: ota_2app        # nazwa logiczna, nie plik
    smp:
      pin_tasks:                  # przydział tasków do rdzeni
        motion.control: 1
        net.worker: 0
    modules:                      # nadpisania tylko dla tego celu
      ui: off
```

### Schematy partycji

| Nazwa | Co daje |
|---|---|
| `single_app` | jeden obraz, bez OTA |
| `ota_2app` | dwa obrazy z OTA plus dane |
| `ota_2app_small` | dwa obrazy, mało miejsca na dane |
| `ota_2app_fat` | dwa obrazy plus system plików FAT |
| `huge_app` | jeden wielki obraz, bez OTA |

Właściwa tablica zależy od rozmiaru pamięci — `ota_2app` przy 16 MB to
`default_16MB.csv`.

### Możliwości

`i2c` `spi` `uart` `pwm` `adc` `dac` `wifi` `ble` `ethernet` `psram` `sdcard`
`usb-host` `usb-device` `fpu` `smp` `can` `rtc`

Brak listy oznacza „nie wiadomo", nie „nie potrafi nic" — wtedy możliwości
bierze się z profilu układu, a niezgodność jest ostrzeżeniem zamiast błędu.

## `modules`

Moduł z sekcją w `modules` jest **włączony**. Nadpisanie w celu jest
ważniejsze: `off` wyłącza, zagnieżdżona mapa zmienia ustawienia.

```yaml
modules:
  core:
    log:
      default: info
      per_module: { motion: debug, net: warn }
      sinks: [uart0, ringbuf]
      ringbuf_kb: 8
    eventbus: { queue_depth: 32 }
    watchdog: { task_timeout_ms: 5000, hw: true }
    shell: { transports: [usb], history: 20 }

  sense:
    hub: { max_sensors: 16, timestamp: us }
    calibration_store: nvs        # nvs | file | none

  net:
    hostname: "rover-01"
    wifi:
      credentials: secrets        # NIGDY wprost
      fallback_ap: { ssid: "rover-01-setup", timeout_s: 300 }
    mqtt:
      broker: "mqtt://broker.local:1883"
      base_topic: "hydra/rover-01"
      lwt: { topic: "sys/online", payload: "0", qos: 1, retain: true }
      tls: { ca: certs/local-ca.pem }
    ota:
      channel: "https://ota.local/rover"
      verify: ed25519:keys/ota_pub.pem

  ui:
    backend: lvgl9                # hydra | lvgl9
    theme: { base: dark, accent: "#e0993f" }
    screens: [StatusScreen, DriveScreen]
    home: StatusScreen

  motion:
    kinematics: { model: differential, wheel_mm: 68, track_mm: 142 }
    control:
      period_us: 2000
      deadline_policy: { miss_limit: 10, on_breach: degrade }
    safety:
      estop: { gpio: Pin.EStop, active: low }
      cmd_watchdog_ms: 250
      current_limit_a: 2.5
```

## `hardware`

```yaml
hardware:
  schematic: hardware/rover.hsch  # źródło prawdy dla wyprowadzeń
  codegen:
    boards_header: true
    fail_on_erc: true

  buses:
    i2c0: { hz: 400000, pullups: external, recovery: true }

  components:
    baro:
      part: BMP280 @ i2c0:0x76    # układ @ magistrala:adres
      hub: { period_ms: 2000, filter: "ema(0.3)", topic: sense/baro }
      measure: { mode: normal, osrs_p: x16, iir: 4 }
```

Pola `hub`, `measure`, `pwm` i `alerts` sprawdza **schemat z paczki**, nie ten
plik — inaczej dodanie nowego czujnika wymagałoby zmiany w rdzeniu Studia.

## `simulation`

```yaml
simulation:
  engine: functional              # functional | qemu | renode
  timestep_us: 1000
  sources:
    baro: { model: atmosphere, p_hpa: 1013.2, noise: 0.3 }
    tof:  { model: playback, file: sim/corridor.csv }
  record:
    vcd: [i2c0]
    eventbus: true
```

Modele: `constant`, `atmosphere`, `ramp`, `sine`, `random`, `playback`.
Wszystkie są funkcjami czasu, więc symulację da się przewinąć w dowolne
miejsce, a ten sam czas i ziarno zawsze dają tę samą wartość.

## `test`

```yaml
test:
  host:
    env: native
    sanitizers: [address, undefined, thread]
  target:
    envs: [esp32s3-main, pico2-dev]
  hil:
    runner: proxmox-hil
    fixtures:
      esp32s3-main: { probe: usb-jtag, power: "uhubctl -l 1-1 -p 2" }
    suites:
      smoke: { on: push, timeout_s: 120 }
      soak:  { on: nightly, duration_h: 8, monitor: [heap_hwm, deadline_miss] }
```

## `secrets`

```yaml
secrets:
  source: .hydra-secrets.yaml     # w .gitignore
  required: [WIFI_SSID, WIFI_PASS, MQTT_USER, MQTT_PASS]
```

Walidator zgłasza wartości wyglądające na sekret wpisany wprost — plik projektu
trafia do repozytorium i to najprostszy sposób na wyciek.

## Co sprawdza walidacja

Poza zgodnością ze schematem sprawdzane są zależności między polami. To one
łapią pomyłki, które naprawdę się zdarzają:

| Reguła | Co wychwytuje |
|---|---|
| `targets.default` | cel domyślny wskazujący nieistniejące środowisko |
| możliwości płytki | moduł sieciowy na płytce bez radia, z gotową poprawką |
| magistrale | komponent podpięty do niezadeklarowanej magistrali |
| adresy I²C | dwa układy pod jednym adresem — usterka widoczna dopiero na sprzęcie |
| `modules.ui.home` | ekran startowy spoza listy ekranów |
| `test.target.envs` | test wskazujący nieistniejący cel |
| sekrety | hasło wpisane wprost |

Każde zgłoszenie niesie ścieżkę, pozycję w pliku i podpowiedź. Przy literówkach
dochodzi „czy chodziło o…".

## Polecenia

```bash
hydra check .        # sprawdza projekt i manifesty paczek
hydra plan .         # pokazuje cele i wyprowadzone ustawienia
hydra gen .          # generuje pliki budowania
hydra build . -e esp32s3
hydra import płytka.net -o hardware/plytka.hsch
```

Kod wyjścia zero oznacza powodzenie, więc `hydra check` nadaje się do zaczepu
przed zatwierdzeniem zmian.
