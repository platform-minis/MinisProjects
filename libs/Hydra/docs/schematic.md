# Schemat połączeń

Źródło prawdy dla wyprowadzeń. Bez niego `boards/*.hpp` pisze się ręcznie,
a numer w kodzie i ścieżka na płytce są dwoma niezależnymi zapisami tej samej
rzeczy — rozjeżdżają się po pierwszej poprawce, której ktoś nie przeniósł,
i objawiają jako urządzenie, które się kompiluje i nie działa.

## Format

```yaml
hsch: "0.1"

sheet:
  name: rover-s3
  grid_mm: 2.54

components:
  U1: { part: esp32s3-devkitc-1, at: [40, 50] }
  U2: { part: bmp280,            at: [130, 20] }
  R1: { part: resistor, value: "4.7k", at: [100, 10] }

nets:
  3V3:
    class: power
    nodes: [U1.3V3, U2.VCC, R1.A]
  I2C0_SDA:
    bus: i2c0
    role: sda
    nodes: [U1.IO8, U2.SDA, R1.B]
  LED_STATUS:
    nodes: [U1.IO48]
    pin_name: StatusLed
```

**To lista węzłów, nie rysunek.** Położenia symboli (`at`) służą wyłącznie do
wyświetlania na płótnie i nie wpływają na nic, co się generuje. Dzięki temu
schemat czyta się w recenzji zmian i scala jak każdy inny plik tekstowy —
czego o formatach binarnych z programów do projektowania płytek powiedzieć
się nie da.

Oznaczenia układów mają postać `U1`, `DS1`, `R3`. Węzeł zapisuje się jako
`układ.wyprowadzenie`.

Sieć z `pin_name` nie łączy dwóch układów — nadaje nazwę wyprowadzeniu, żeby
kod mówił `Pin::StatusLed` zamiast „GPIO 48". Jeden węzeł jest tam stanem
docelowym, nie niedokończoną pracą.

## Nagłówek płytki z połączeń

Sieć `I2C0_SDA` dotyka pinu `IO8` mikrokontrolera, więc:

```c
#define HYDRA_BOARD_I2C0_SDA 8
```

Mikrokontroler rozpoznaje się po tym, że jego wyprowadzenia mają **numery**
(`gpio` w definicji) — czujnik ma SDA i SCL, ale numeruje je producent płytki,
nie on.

Dioda jest jedyną rzeczą rozpoznawaną po nazwie sieci (`LED*`), bo
`hal::board::led` jest częścią API frameworka. Sieć diody daje **i** `board::led`,
**i** stałą `Pin::…`, a nie jedno albo drugie.

Poprawka na schemacie przenosi się do kodu przy następnym `hydra gen`.

## Reguły elektryczne

`hydra check` sprawdza schemat razem z resztą — schemat i plik projektu opisują
to samo urządzenie i nie ma powodu pytać o nie osobno.

| Reguła | Co wychwytuje | Waga |
|---|---|---|
| istnienie wyprowadzeń | literówka w nazwie pinu, z listą dostępnych | błąd |
| istnienie układów | odwołanie do układu spoza schematu | błąd |
| kompletność podłączeń | zapomniana masa wyświetlacza | błąd |
| zwarcie wyjść | dwa wyjścia przeciwsobne na jednej sieci | błąd |
| jedno wyprowadzenie, jedna sieć | skopiowany wiersz z niepoprawioną nazwą | błąd |
| kompletność magistrali | I²C bez SCL, UART bez RX | błąd |
| brak definicji układu | paczka nie została dodana | błąd |
| podciągnięcia I²C | otwarty dren bez rezystora | ostrzeżenie |
| sieci wiszące | połączenie zaczęte i nieskończone | ostrzeżenie |
| sieć bez źródła | wejścia, których nic nie steruje | ostrzeżenie |

### Sterowanie a zwarcie

To rozróżnienie jest istotne i łatwo je pomylić. Wyprowadzenie dwukierunkowe
i otwarty dren **sterują** siecią, ale ich zwarcie nie jest zwarciem — na tym
polega I²C, gdzie na jednej linii siedzi kilkanaście układów. Konfliktem są
tylko wyjścia przeciwsobne: przy przeciwnych stanach płynie prąd ograniczony
wyłącznie rezystancją tranzystorów wyjściowych.

### Podciągnięcia

Otwarty dren nie potrafi wystawić stanu wysokiego. Bez rezystora podciągającego
magistrala I²C nigdy nie ruszy, a widać to dopiero oscyloskopem — dlatego
rezystory są na schemacie jako osobne komponenty. Jeśli podciągnięcie jest
wewnętrzne albo na module, wystarczy zadeklarować to w projekcie:

```yaml
hardware:
  buses:
    i2c0: { pullups: internal }
```

## Import z KiCada i EasyEDA

```bash
hydra import płytka.net -o hardware/plytka.hsch
```

Nikt nie rysuje płytki od zera w edytorze frameworka — projekt istnieje
wcześniej. Import zamienia netlistę na `.hsch`, dzięki czemu reguły elektryczne
i generowanie nagłówka działają na czymś, co już powstało.

Import **nie zgaduje**, która paczka odpowiada któremu układowi, i mówi to
wprost:

```
uzupełnij pole „part" dla: bmp280, esp32-s3-devkitc-1
```

Zgadywanie dałoby schemat wyglądający na gotowy i generujący zły nagłówek —
najgorszy możliwy wynik. Wyjątkiem są elementy bierne: `R1` to rezystor
niezależnie od tego, co stoi w polu wartości, bo to konwencja wszystkich
narzędzi, nie domysł.

Format rozpoznawany jest po rozszerzeniu: `.net` to KiCad, `.json` — EasyEDA.
Wynik idzie na standardowe wyjście albo do pliku wskazanego przez `-o`, żeby
dało się go obejrzeć przed zapisaniem.

## Przydział wyprowadzeń

Czujnik I²C wystarczy dopiąć do magistrali. Sterownik silnika potrzebuje
czterech osobnych pinów — Studio dobiera je z definicji mikrokontrolera:
wolne, pasujące kierunkiem, w kolejności numerów, bo tak leżą na złączu.

Wynik jest **propozycją pokazywaną przed zatwierdzeniem**, nie decyzją.
Automat ma oszczędzić klikania, a nie odebrać wybór.

Wyjście układu nie trafi na wyjście mikrokontrolera — to byłoby zwarcie, które
reguły elektryczne i tak by zgłosiły. Brak miejsca na wyprowadzenie opcjonalne
nie jest przeszkodą; na obowiązkowe — jest, i mówi o tym wprost.

## Płótno

Rysowane na `@xyflow/react`. Widok nie podejmuje decyzji: rozmieszczenie,
strony wyprowadzeń i zamiana sieci na krawędzie liczy warstwa układu, więc
ma testy, a komponent zostaje rysunkiem.

Sieci idą przez węzeł pośredni, a nie połączenie każdego z każdym: sieć
zasilania z ośmioma odbiornikami dałaby 28 krawędzi zamiast ośmiu i schemat
stałby się nieczytelny dokładnie tam, gdzie najbardziej trzeba go rozumieć.

Zasilanie i masa idą po lewej stronie symbolu, sygnały po prawej — tak rysuje
się schematy od zawsze i dzięki temu da się je czytać bez śledzenia każdej linii.

Zgłoszenia reguł trafiają na obwódki symboli i do dymków. Przesunięcie symbolu
zapisuje położenie jednym przedziałem tekstu, dopiero po puszczeniu — zapis
przy każdej klatce przeciągania zasypałby historię zmian.
