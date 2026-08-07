# Testowanie

Cztery poziomy, każdy odpowiada na inne pytanie. Trzy pierwsze działają bez
sprzętu i wykonują się w sekundach — to nie jest wygoda, tylko warunek, żeby
błędy w rodzaju „reakcja na usterkę spóźnia się o jeden cykl" dało się
w ogóle zauważyć.

| Poziom | Pytanie | Czas |
|---|---|---|
| Testy hostowe | czy logika jest poprawna | < 1 s |
| Sanitizery | czy nie ma wyścigów i błędów pamięci | kilka sekund |
| Budowa wsadów | czy kompiluje się na każdej platformie | kilka minut |
| Testy na sprzęcie | czy działa na prawdziwej płytce | minuty do godzin |

## Poziom 1 — testy hostowe

```bash
make -C test          # 428 przypadków, 2566 asercji
```

Cały framework działa na maszynie deweloperskiej dzięki atrapom każdej warstwy:
HAL, sieci, interfejsu, napędu, magazynu wsadu. Testowany jest **prawdziwy
kod** — protokół MQTT rozbiera prawdziwe ramki, potok renderowania rysuje do
bufora w pamięci, łańcuch bezpieczeństwa napędu reaguje na atrapowe enkodery.

Działa to dlatego, że funkcje pracujące w pętli przyjmują czas argumentem.
Backoff połączenia, który na urządzeniu trwa minuty, w teście trwa
mikrosekundy — i sprawdza dokładnie tę samą logikę.

### Co te testy już wyłapały

Rzeczy, których na sprzęcie nie widać:

- **pierwsze przekroczenie terminu ginęło** — ogranicznik częstości zgłoszeń
  startował z zerem, więc spóźnienia w pierwszej sekundzie nie były publikowane;
  a to właśnie one zdarzają się najczęściej;
- **podwójne buforowanie zostawiało ślady** — po pierwszej pełnej klatce
  każda następna była pełna, bo zapamiętywano obszar narysowany zamiast
  unieważnionego;
- **reakcja na usterkę enkodera spóźniała się o cykl** — pięć milisekund jazdy
  na ślepo;
- **`stopMotors()` pomijało stan początkowy** — pojazd na pochyłości zjeżdżałby
  swobodnie.

## Poziom 2 — sanitizery

```bash
make -C test asan     # błędy pamięci i zachowania niezdefiniowane
make -C test tsan     # wyścigi
```

Osobne katalogi budowy, bo flagi zmieniają układ obiektów.

Wykrywanie wyścigów wymaga wyłączenia losowania układu pamięci, co w kontenerze
blokuje domyślny profil seccomp — `docker/hydra.sh` dokłada
`--security-opt seccomp=unconfined` i `--cap-add SYS_PTRACE`. Bez tego TSan
przerywa działanie zaraz po starcie.

Znalezione wyścigi: liczniki magistrali zdarzeń poza blokadą, deskryptor taska
odczytywany przed zapisem, statystyki taska kopiowane w trakcie zapisu przez
komendę `ps`.

## Poziom 3 — budowa wsadów

```bash
make -C test examples   # przykłady na backendzie hostowym
make -C test stub       # backend Arduino na atrapach nagłówków
make -C test docs       # fragmenty z docs/api.md
./tools/check_includes.sh
./docker/hydra.sh fw all all    # 5 platform × 6 przykładów
```

**Kontrola na atrapach nie zastępuje prawdziwej budowy.** Przez sześć etapów
raportowałem „backend Arduino przechodzi kontrolę składni dla wszystkich
platform" — a pierwszy prawdziwy build pokazał, że framework nie kompiluje się
na żadnej z pięciu. Atrapy, które sam napisałem, były zbyt pobłażliwe.

Od tej pory atrapa ESP32 jest sprawdzana **dwukrotnie**: pod rdzeń 2.x i 3.x,
bo API LEDC różni się między nimi, a bez drugiego przejścia jedna z gałęzi
nigdy nie przechodziłaby przez kompilator.

### Fragmenty dokumentacji

`make -C test docs` kompiluje `docs/snippets.cpp` — te same fragmenty, które
stoją w [api.md](api.md). Przy pierwszym uruchomieniu **osiem** przykładów
opisywało API, którego nie ma (`hal::Pin`, `Secret` zamiast `SecretString`,
`Pipeline{}.then()`, zły podpis `publishOn`). Dokumentacja, która nie kompiluje
się w CI, opisuje framework, którego nie ma.

## Poziom 4 — testy na sprzęcie

```bash
tools/hil_run.py --port /dev/ttyUSB0 --suite all
```

Harness steruje urządzeniem przez shell diagnostyczny i rozbiera odpowiedzi.
To dlatego każda komenda shella wypisuje wynik **także** jako pary
`klucz=wartość`: człowiek czyta tabelkę, skrypt rozbiera to samo jednym
wyrażeniem.

| Zestaw | Co sprawdza |
|---|---|
| `basic` | odpowiedź, taski, zapas stosu, gubione zdarzenia, zapas sterty |
| `i2c` | obecność układów na magistrali |
| `storage` | czy konfiguracja przeżywa zapis |
| `leak` | czy sterta maleje po serii komend |

Zestaw `leak` opiera się na założeniu frameworka: po `App::begin()` nie ma
alokacji, więc każdy stały ubytek jest błędem.

Uruchamiane workflow `hydra-hil` na własnym runnerze z podłączonymi płytkami —
nocnie i ręcznie, nie przy każdym pushu: fizyczny sprzęt jest jeden, a wgranie
wsadu trwa dłużej niż cały zestaw testów hostowych.

## Czego nadal nie ma

**Nic nie zostało uruchomione na fizycznej płytce.** Wsady się budują i mieszczą
w pamięci, ale zachowanie na sprzęcie zweryfikuje dopiero pierwszy przebieg
`hil_run.py` na prawdziwym urządzeniu. To jest największa niewiadoma tego
projektu i warto ją mieć na uwadze przy czytaniu wszystkich pozostałych liczb.
