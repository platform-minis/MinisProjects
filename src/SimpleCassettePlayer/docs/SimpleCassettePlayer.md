# 🎵 DIY Odtwarzacz Kasetowy — Dokumentacja Projektu

> **Wersja:** 1.0  
> **Data:** 2026-02-06  
> **Poziom trudności:** ⭐ Początkujący  
> **Szacowany czas montażu:** 2–4 godziny  
> **Szacowany koszt:** 40–80 zł  

---

## 1. Opis projektu

Celem projektu jest zbudowanie minimalistycznego odtwarzacza kaset magnetofonowych z jak najmniejszą liczbą elementów. Całość opiera się na mechanizmie kasetowym (głowica + silnik DC) oraz jednym układzie wzmacniacza audio LM386, który pozwala na bezpośrednie odsłuchanie muzyki przez głośnik lub słuchawki.

Projekt jest idealnym punktem wyjścia do nauki elektroniki analogowej — łączy mechanikę, elektroakustykę i podstawy wzmacniaczy w jednym, namacalnym urządzeniu.

### 1.1. Cele projektu

- Zbudowanie działającego odtwarzacza kasetowego z ~10 elementów.
- Zrozumienie podstaw zapisu i odczytu magnetycznego.
- Nauka lutowania / montażu na płytce stykowej.
- Stworzenie bazy do przyszłej rozbudowy (mikrokontroler, Bluetooth, wyświetlacz).

---

## 2. Teoria — jak działa zapis magnetyczny na kasecie

### 2.1. Budowa kasety kompaktowej

Kaseta kompaktowa (Compact Cassette), wynaleziona przez firmę Philips w 1963 roku, zawiera taśmę magnetyczną o szerokości 3,81 mm nawiniętą na dwie szpule. Taśma składa się z cienkiej folii poliestrowej (podłoże) pokrytej warstwą materiału magnetycznego — najczęściej tlenku żelaza (Fe₂O₃) w kasetach typu I (Normal), dwutlenku chromu (CrO₂) w kasetach typu II (Chrome) lub cząsteczek metalu w kasetach typu IV (Metal).

Standardowa kaseta C-60 mieści 30 minut nagrania na stronę (60 minut łącznie), a C-90 odpowiednio 45 minut na stronę.

### 2.2. Zasada zapisu magnetycznego

Zapis dźwięku na taśmie opiera się na zjawisku magnetyzmu resztkowego (remanencji). Sygnał audio (napięcie zmienne) przepływa przez cewkę w głowicy zapisującej, generując zmienne pole magnetyczne. Gdy taśma przesuwa się obok szczeliny głowicy, drobiny materiału magnetycznego na taśmie zostają namagnesowane proporcjonalnie do natężenia pola — a więc proporcjonalnie do sygnału audio. Po wyłączeniu pola, cząsteczki zachowują swoje namagnesowanie (remanencja), przechowując w ten sposób informację.

Aby zapis był wierny, stosuje się tzw. polaryzację (bias) — sygnał ultradźwiękowy o częstotliwości ok. 80–120 kHz dodawany do sygnału audio podczas nagrywania. Bias linearyzuje charakterystykę magnetyczną taśmy, eliminując zniekształcenia wynikające z nieliniowej krzywej histerezy materiału magnetycznego.

### 2.3. Zasada odczytu (odtwarzania)

Odtwarzanie jest procesem odwrotnym. Taśma z namagnesowanymi cząsteczkami przesuwa się obok szczeliny głowicy odczytującej. Zmienne pole magnetyczne taśmy indukuje napięcie w cewce głowicy (zgodnie z prawem Faradaya). To napięcie jest niezwykle słabe — rzędu 0,2–1 mV — i wymaga znacznego wzmocnienia, zanim trafi do głośnika.

Ważna właściwość: napięcie indukowane w głowicy jest proporcjonalne do szybkości zmian strumienia magnetycznego, co oznacza, że wyższe częstotliwości generują większe napięcie. Dlatego w profesjonalnych odtwarzaczach stosuje się korekcję equalizacji (krzywe IEC/NAB), aby wyrównać odpowiedź częstotliwościową. W naszym uproszczonym projekcie pomijamy tę korekcję — dźwięk będzie nieco jaśniejszy niż w odtwarzaczu hi-fi, ale w pełni słyszalny.

### 2.4. Prędkość przesuwu taśmy

Standard dla kasety kompaktowej to **4,76 cm/s** (1⅞ cala na sekundę). Utrzymanie stałej prędkości jest kluczowe — wahania prędkości powodują efekt „kołysania" dźwięku (wow & flutter). W profesjonalnych deckach prędkość jest stabilizowana elektronicznie, w naszym projekcie zależy od stabilności napięcia zasilającego silnik.

### 2.5. Ścieżki na taśmie

Taśma stereo zawiera 4 ścieżki — po dwie na każdą stronę kasety. Głowica stereo odczytuje jednocześnie 2 ścieżki (lewy i prawy kanał). Po odwróceniu kasety odczytywane są dwie pozostałe ścieżki w odwrotnym kierunku.

```
Strona A → [L] [R] ────────────────► kierunek taśmy
Strona B ◄──────────────── [R] [L]

Szerokość taśmy: 3.81 mm
Każda ścieżka: ~0.6 mm
Przerwy między ścieżkami: ~0.3 mm
```

---

## 3. Architektura układu

### 3.1. Schemat blokowy

```
┌──────────┐    ┌──────────┐    ┌───────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ BATERIA  │───►│ SILNIK   │───►│KASETA │───►│ GŁOWICA  │───►│ LM386    │───►│ GŁOŚNIK  │
│ 9V       │    │ DC 9V    │    │       │    │ stereo   │    │ wzmacn.  │    │ 8Ω       │
└──────────┘    └──────────┘    └───────┘    └──────────┘    └──────────┘    └──────────┘
     │                                            │               │
     │                                       C1 (100nF)     C2 (220µF)
     │                                            │               │
     └──────────────── GND (masa wspólna) ────────┴───────────────┘
```

### 3.2. Opis toru sygnałowego

Tor sygnałowy składa się z następujących etapów:

**Źródło sygnału** — głowica magnetyczna odczytuje namagnesowane cząsteczki z taśmy i generuje sygnał zmienny o amplitudzie ~0,2–1 mV.

**Kondensator sprzęgający C1 (100 nF)** — blokuje składową stałą (DC offset) z głowicy, przepuszczając jedynie sygnał audio (AC). Zapobiega to uszkodzeniu wzmacniacza i eliminuje trzaski.

**Potencjometr R1 (10 kΩ)** — działa jako dzielnik napięcia, pozwalając płynnie regulować głośność od zera do maksimum.

**Wzmacniacz LM386** — wzmacnia sygnał z domyślnym wzmocnieniem 20× (26 dB). Przy sygnale wejściowym 1 mV daje ~20 mV na wyjściu, co jest wystarczające do napędzenia małego głośnika.

**Kondensator wyjściowy C2 (220 µF)** — sprzęga wyjście wzmacniacza z głośnikiem, blokując składową stałą obecną na wyjściu LM386 (która wynosi około połowy napięcia zasilania).

**Kondensator bypass C3 (10 µF)** — filtruje zakłócenia na pinie zasilania wewnętrznego LM386 (Pin 7), poprawiając stabilność i redukując szumy.

---

## 4. Lista materiałów (BOM)

### 4.1. Mechanika

| # | Element | Parametry | Ilość | Cena orientacyjna |
|---|---------|-----------|-------|-------------------|
| 1 | Głowica magnetyczna | Stereo, do kasety kompaktowej | 1 szt. | 5–15 zł |
| 2 | Silnik DC | 9V, typ EG-530AD-2F (CCW, 2400 RPM) lub podobny capstan motor | 1 szt. | 8–20 zł |
| 3 | Kaseta magnetofonowa | C-60 lub C-90 z nagraną muzyką | 1+ szt. | 2–5 zł |

### 4.2. Elektronika

| # | Ozn. | Element | Wartość / typ | Ilość | Cena |
|---|------|---------|---------------|-------|------|
| 4 | U1 | Wzmacniacz audio | LM386N-1, obudowa DIP-8 | 1 szt. | 2–5 zł |
| 5 | C1 | Kondensator ceramiczny | 100 nF (0,1 µF) | 1 szt. | ~0,50 zł |
| 6 | C2 | Kondensator elektrolityczny | 220 µF / 16V | 1 szt. | ~0,50 zł |
| 7 | C3 | Kondensator elektrolityczny | 10 µF / 16V | 1 szt. | ~0,30 zł |
| 8 | R1 | Potencjometr obrotowy | 10 kΩ liniowy (B10K) | 1 szt. | 2–4 zł |
| 9 | SW1 | Wyłącznik | SPST ON/OFF, suwakowy lub toggle | 1 szt. | ~1 zł |

### 4.3. Wyjście audio (jedno z dwóch)

| # | Element | Parametry | Cena |
|---|---------|-----------|------|
| 10a | Głośnik | 8 Ω, 0,5–1 W, średnica 40–57 mm | 3–5 zł |
| 10b | Gniazdo jack 3,5 mm | Do podłączenia słuchawek (alternatywa) | ~2 zł |

### 4.4. Zasilanie

| # | Element | Parametry | Cena |
|---|---------|-----------|------|
| 11 | Bateria 9V | Typ 6F22 (kostka) | 5–8 zł |
| 12 | Zatrzask na baterię 9V | Z przewodami | ~2 zł |

### 4.5. Montaż i narzędzia

| # | Element | Opis | Cena |
|---|---------|------|------|
| 13 | Płytka stykowa | Breadboard 400 lub 830 punktów | 5–10 zł |
| 14 | Kabelki połączeniowe | Jumper wires M-M, zestaw | ~5 zł |
| 15 | Multimetr (opcja) | Do pomiarów napięcia — bardzo przydatny | 20–40 zł |

### 4.6. Podsumowanie kosztów

| Kategoria | Koszt |
|-----------|-------|
| Mechanika (głowica + silnik + kaseta) | 15–40 zł |
| Elektronika (LM386 + pasywne) | 7–12 zł |
| Zasilanie | 7–10 zł |
| Montaż | 10–15 zł |
| **RAZEM** | **~40–80 zł** |

---

## 5. Gdzie kupić części — poradnik

### 5.1. AliExpress (najtaniej, ale 2–4 tygodnie czekania)

Najlepsze źródło głowicy i silnika — w Polsce trudno je znaleźć.

**Głowica magnetyczna** — szukaj fraz: `cassette tape head stereo`, `cassette player head`, `tape recorder head`. Ceny: 3–15 zł. Wybieraj głowice z 4 pinami (stereo). Unikaj głowic „erase head" (do kasowania) — potrzebujesz „playback head" lub „R/P head" (read/play).

**Silnik DC** — szukaj: `EG-530AD-2F`, `cassette player motor`, `cassette deck DC motor 9V`. Ceny: 5–20 zł. Upewnij się, że napięcie nominalne to 9V (lub 6–12V). Kierunek: CCW (counter-clockwise) jest standardem dla odtwarzaczy.

**Moduł LM386** — szukaj: `LM386 module`, `LM386 amplifier board`. Za ~3–5 zł dostajesz gotową płytkę z wszystkimi kondensatorami — wystarczy podłączyć zasilanie, sygnał i głośnik. To najprostsza opcja na start.

**Zatrzask baterii 9V** — szukaj: `9V battery snap connector`. Cena: ~1–2 zł.

### 5.2. Botland.com.pl (szybko, polski sklep)

Specjalizuje się w elektronice dla hobbystów. Znajdziesz tu LM386 (układ DIP-8), kondensatory, potencjometry, płytki stykowe, kabelki, głośniki i gniazda jack. Wysyłka zazwyczaj 1–3 dni robocze. Ceny wyższe niż na Ali, ale bez czekania.

### 5.3. TME.eu (profesjonalny dystrybutor)

Ogromny asortyment elementów elektronicznych po cenach hurtowych. Świetne ceny na kondensatory, rezystory i układy scalone. Minimalne zamówienie: brak, ale opłaca się zamawiać więcej sztuk (rezystory/kondensatory kosztują grosze). Idealne źródło dla C1, C2, C3, R1 i LM386.

### 5.4. OLX / Allegro

Dobre miejsce na tanie kasety z muzyką (2–5 zł/szt.) oraz stare walkmany do rozebrania (10–30 zł). Stary walkman daje kompletny mechanizm — głowicę, silnik, pasek, koło zamachowe i obudowę.

### 5.5. Porady zakupowe

Zamów kilka sztuk kluczowych elementów (2× LM386, 2× głowica) — w razie uszkodzenia jednego nie musisz czekać kolejnych tygodni. Rozważ zakup gotowego mechanizmu kasetowego na Ali (szukaj: `cassette tape mechanism`, `cassette deck mechanism`, cena ~15–30 zł) — dostajesz głowicę, silnik, koło zamachowe, pasek i obudowę w jednym, co znacznie ułatwia pierwszy build. Zamów „zestaw startowy" kondensatorów i rezystorów — kosztuje ~15–25 zł na Ali, a masz zapas na wiele projektów.

---

## 6. Schemat połączeń pin-po-pinie

### 6.1. Pinout LM386

```
        ┌────╮────┐
 GAIN  1│●        │8  GAIN
  IN-  2│ LM386  │7  BYPASS
  IN+  3│        │6  Vs+
  GND  4│        │5  OUT
        └────────┘
```

### 6.2. Tabela połączeń

| Z (skąd) | Do (dokąd) | Przez element | Uwagi |
|-----------|-----------|---------------|-------|
| Bateria 9V (+) | SW1 wejście | — | Czerwony przewód zatrzasku |
| SW1 wyjście | Silnik DC (+) | — | Jeden z przewodów silnika |
| SW1 wyjście | LM386 Pin 6 (Vs+) | — | Zasilanie wzmacniacza |
| Bateria 9V (−) | Szyna GND na breadboardzie | — | Czarny przewód zatrzasku |
| Silnik DC (−) | Szyna GND | — | Drugi przewód silnika |
| LM386 Pin 4 (GND) | Szyna GND | — | Masa wzmacniacza |
| Głowica pin sygnału | LM386 Pin 3 (IN+) | C1 (100 nF) → R1 (środek) | Sygnał audio przez kondensator i potencjometr |
| Głowica pin masy | Szyna GND | — | Masa głowicy |
| R1 skrajny pin (góra) | Wyjście C1 | — | Sygnał wchodzi na potencjometr |
| R1 skrajny pin (dół) | Szyna GND | — | Drugi koniec dzielnika |
| R1 środkowy pin (wiper) | LM386 Pin 3 (IN+) | — | Regulowany sygnał do wzmacniacza |
| LM386 Pin 2 (IN−) | Szyna GND | — | Wejście odwracające na masę |
| LM386 Pin 5 (OUT) | Głośnik 8Ω (+) | C2 (220 µF, **+** do Pin 5) | Wyjście audio, uwaga na polaryzację C2! |
| Głośnik 8Ω (−) | Szyna GND | — | Drugi przewód głośnika |
| LM386 Pin 7 (BYPASS) | Szyna GND | C3 (10 µF, **+** do Pin 7) | Stabilizacja, uwaga na polaryzację C3! |
| LM386 Pin 1 | LM386 Pin 8 | — (brak połączenia) | Niepodłączone = wzmocnienie 20×. Opcjonalnie: C4 10 µF między nimi = wzmocnienie 200× |

### 6.3. Głowica stereo — identyfikacja pinów

Głowica stereo ma zazwyczaj 4 piny. Patrząc od przodu (od strony szczeliny):

```
    [szczelina głowicy]
     ┌──────────────┐
     │  ①  ②  ③  ④  │
     └──────────────┘

① Lewy kanał (L)
② Masa (GND) — wspólna
③ Prawy kanał (R)  
④ Masa (GND) — wspólna
```

Na start podłącz pin ① (lewy) lub ③ (prawy) do C1. Oba piny masy (② i ④) podłącz do szyny GND. Jeśli nie jesteś pewien który pin jest który — użyj multimetru do pomiaru rezystancji między pinami. Cewki kanałów mają typowo 300–600 Ω.

---

## 7. Przewodnik montażu krok po kroku

### 7.1. Przygotowanie

Przed rozpoczęciem montażu upewnij się, że masz wszystkie elementy z listy BOM. Przygotuj czyste, dobrze oświetlone stanowisko pracy. Jeśli masz multimetr — sprawdź napięcie baterii 9V (powinna pokazywać 8,5–9,5 V na nowej baterii).

### 7.2. Krok 1 — Przygotowanie płytki stykowej

Umieść LM386 na płytce stykowej tak, aby nóżki były po obu stronach rowka centralnego. Wycięcie na obudowie DIP-8 wskazuje stronę pinu 1. Upewnij się, że każdy pin trafia do osobnego rzędu na breadboardzie.

### 7.3. Krok 2 — Zasilanie LM386

Podłącz pin 6 (Vs+) do szyny „+" na breadboardzie. Podłącz pin 4 (GND) do szyny „−". Jeszcze nie podłączaj baterii.

### 7.4. Krok 3 — Kondensator bypass C3

Podłącz kondensator elektrolityczny 10 µF między pin 7 a szynę GND. Dłuższa nóżka kondensatora (+) idzie do pinu 7, krótsza (−) do GND.

### 7.5. Krok 4 — Wejście audio (potencjometr + kondensator)

Podłącz kondensator C1 (100 nF, ceramiczny — brak polaryzacji) jednym końcem do przewodu sygnałowego z głowicy. Drugi koniec C1 podłącz do jednego ze skrajnych pinów potencjometru R1. Drugi skrajny pin R1 podłącz do GND. Środkowy pin R1 (wiper) podłącz do pinu 3 LM386 (IN+). Pin 2 LM386 (IN−) podłącz do GND.

### 7.6. Krok 5 — Wyjście audio

Podłącz kondensator elektrolityczny C2 (220 µF) pinem „+" do pinu 5 LM386 (OUT). Pin „−" kondensatora C2 podłącz do jednego przewodu głośnika. Drugi przewód głośnika podłącz do GND.

### 7.7. Krok 6 — Silnik

Podłącz jeden przewód silnika do szyny „+" (przez wyłącznik SW1). Drugi przewód silnika do GND. Jeśli silnik kręci w złą stronę — zamień przewody.

### 7.8. Krok 7 — Zasilanie i test

Podłącz zatrzask baterii 9V: czerwony do szyny „+" (przez SW1), czarny do szyny „−". Włóż kasetę do mechanizmu, upewnij się, że taśma dotyka głowicy. Włącz SW1. Silnik powinien zacząć kręcić. Powoli odkręcaj potencjometr — powinieneś usłyszeć muzykę z głośnika.

### 7.9. Krok 8 — Korekty

Jeśli dźwięk jest zbyt cichy nawet przy maksymalnej głośności, dodaj kondensator 10 µF między pin 1 a pin 8 LM386 — wzmocnienie wzrośnie z 20× do 200×. Jeśli silnik kręci zbyt szybko lub zbyt wolno, możesz dodać potencjometr 100 Ω–1 kΩ w szereg z silnikiem do regulacji prędkości.

---

## 8. Troubleshooting — najczęstsze problemy

### 8.1. Brak dźwięku

| Możliwa przyczyna | Diagnoza | Rozwiązanie |
|-------------------|----------|-------------|
| Brak zasilania | Zmierz napięcie na pinie 6 LM386 — powinno być ~9V | Sprawdź baterię, wyłącznik, połączenia |
| Głowica nie dotyka taśmy | Sprawdź wizualnie | Dociśnij głowicę do taśmy, wyreguluj pozycję |
| Zły pin głowicy | Zmierz rezystancję między pinami | Spróbuj podłączyć inny pin sygnałowy |
| C2 odwrotnie podłączony | Sprawdź polaryzację (+/−) | Odwróć kondensator |
| LM386 odwrotnie włożony | Sprawdź orientację wycięcia | Wyjmij i włóż poprawnie |

### 8.2. Dźwięk jest bardzo cichy

Domyślne wzmocnienie LM386 to zaledwie 20×, co przy sygnale 0,5 mV z głowicy daje ~10 mV — to mało. Dodaj kondensator 10 µF między pin 1 a pin 8, aby zwiększyć wzmocnienie do 200×. Upewnij się, że potencjometr jest odkręcony na maksimum. Sprawdź, czy głowica jest poprawnie ustawiona względem taśmy.

### 8.3. Dźwięk jest zniekształcony / buczy

Przenoszenie 50 Hz z sieci — jeśli zasilasz z zasilacza, spróbuj z baterii. Odległość głowicy od silnika — silnik DC generuje zakłócenia elektromagnetyczne; odsuń głowicę jak najdalej od silnika. Dodaj kondensator 100 nF równolegle do silnika (bezpośrednio na jego piny) — tłumi to szpilki napięciowe generowane przez komutator silnika.

### 8.4. Silnik nie kręci / kręci za wolno

Sprawdź napięcie baterii — bateria 9V „kostka" ma niską pojemność (~500 mAh) i szybko się rozładowuje pod obciążeniem silnika. Rozważ przejście na 6× AA (9V, ale ~2000 mAh pojemności). Jeśli silnik jest na 12V, a zasilasz 9V — będzie kręcił wolniej. Przy 6V może nie ruszyć wcale.

### 8.5. Dźwięk „kołysze się" (wow & flutter)

Pasek napędowy jest za luźny lub za ciasny. Koło zamachowe (flywheel) nie obraca się płynnie — sprawdź łożysko. Bateria jest rozładowana — napięcie spada, silnik zwalnia. Wymień baterię.

### 8.6. Słychać tylko szum

Taśma może być rozmagnesowana lub pusta. Głowica może być brudna — wyczyść ją wacikiem nasączonym alkoholem izopropylowym. Głowica może być odwrócona — szczelina musi być skierowana w stronę taśmy.

---

## 9. Co dalej — pomysły na rozbudowę

### 9.1. Poziom 2 — Dodanie mikrokontrolera

Dodaj Arduino Nano lub ESP32 do projektu, aby uzyskać sterowanie silnikiem przez PWM (precyzyjna regulacja prędkości), przyciski Play / Stop / Przewijanie, odczyt prędkości silnika przez enkoder lub tachometr.

Przykładowy prosty kod Arduino do sterowania silnikiem:

```cpp
// Pin 9 - PWM do silnika (przez tranzystor NPN lub moduł L9110)
const int motorPin = 9;
const int playBtn = 2;
int motorSpeed = 180; // 0-255, dostosuj do prędkości 4.76 cm/s

void setup() {
  pinMode(motorPin, OUTPUT);
  pinMode(playBtn, INPUT_PULLUP);
}

void loop() {
  if (digitalRead(playBtn) == LOW) {
    analogWrite(motorPin, motorSpeed);
  } else {
    analogWrite(motorPin, 0);
  }
}
```

### 9.2. Poziom 3 — Wyświetlacz OLED

Dodaj wyświetlacz OLED 0,96" (I2C, SSD1306) do wyświetlania stanu odtwarzania (Play/Stop), poziomu głośności (odczyt z potencjometru przez ADC), prostego VU-metru (odczyt poziomu sygnału audio), czasu odtwarzania (szacowanego na podstawie obrotów silnika).

### 9.3. Poziom 4 — Bluetooth audio

Dodaj moduł Bluetooth audio (np. BT201 lub KCX_BT_EMITTER, ~8–15 zł na Ali) aby strumieniować dźwięk z kasety do słuchawek / głośnika Bluetooth. Moduł podłączasz do wyjścia LM386 (przed głośnikiem). Możliwość słuchania kaset na nowoczesnych słuchawkach bezprzewodowych.

### 9.4. Poziom 5 — Digitalizacja

Użyj przetwornika ADC w mikrokontrolerze (ESP32 ma 12-bitowy ADC) do digitalizacji sygnału audio z głowicy. Zapis na kartę microSD jako pliki WAV. Stwórz „ripper kaset" — urządzenie do archiwizacji starych nagrań w formacie cyfrowym.

### 9.5. Poziom 6 — Własny PCB

Zaprojektuj własną płytkę drukowaną w programie KiCad (darmowy). Zamów produkcję PCB w JLCPCB lub PCBWay (~$2 za 5 sztuk). Dodaj profesjonalną obudowę drukowaną w 3D. Stwórz kompletny, estetyczny produkt, którym można się pochwalić.

---

## 10. Słowniczek pojęć

| Pojęcie | Wyjaśnienie |
|---------|-------------|
| **AC (Alternating Current)** | Prąd zmienny — prąd, którego kierunek i natężenie zmieniają się cyklicznie. Sygnał audio jest sygnałem AC. |
| **ADC (Analog-to-Digital Converter)** | Przetwornik analogowo-cyfrowy — zamienia napięcie analogowe na wartość liczbową zrozumiałą dla mikrokontrolera. |
| **Bias (polaryzacja)** | Sygnał ultradźwiękowy dodawany podczas nagrywania na taśmę, poprawiający liniowość zapisu magnetycznego. |
| **Breadboard (płytka stykowa)** | Płytka do prototypowania obwodów bez lutowania. Elementy wkłada się w otwory połączone wewnętrznie. |
| **Capstan** | Wałek napędowy w mechanizmie kasetowym, który ciągnie taśmę ze stałą prędkością. Napędzany przez silnik DC. |
| **CCW (Counter-Clockwise)** | Obroty w kierunku przeciwnym do ruchu wskazówek zegara — standardowy kierunek silnika capstan. |
| **Cewka** | Zwój drutu, który generuje pole magnetyczne (przy przepływie prądu) lub w którym indukuje się napięcie (przy zmianie pola). |
| **Condensator (kondensator)** | Element przechowujący ładunek elektryczny. Kondensator sprzęgający przepuszcza AC, blokuje DC. |
| **DAC (Digital-to-Analog Converter)** | Przetwornik cyfrowo-analogowy — zamienia wartość liczbową na napięcie analogowe. |
| **DC (Direct Current)** | Prąd stały — prąd płynący w jednym kierunku. Baterie dostarczają prąd DC. |
| **DIP-8** | Typ obudowy układu scalonego z 8 nóżkami (4 po każdej stronie), do montażu w otworach. |
| **Dzielnik napięcia** | Układ dwóch rezystorów (lub potencjometru) zmniejszający napięcie — w naszym przypadku reguluje głośność. |
| **Equalizacja** | Korekcja odpowiedzi częstotliwościowej — wyrównywanie poziomu różnych częstotliwości. |
| **Flywheel (koło zamachowe)** | Ciężki dysk na osi capstanu, wygładzający obroty silnika i redukujący wow & flutter. |
| **GND (Ground)** | Masa — punkt odniesienia napięcia w obwodzie (0V). Wszystkie elementy dzielą wspólną masę. |
| **Głowica magnetyczna** | Elektromagnes ze szczeliną, odczytujący lub zapisujący sygnał magnetyczny z/na taśmę. |
| **Histereza** | Nieliniowa zależność namagnesowania materiału od przyłożonego pola — przyczyna zniekształceń zapisu. |
| **LM386** | Popularny, tani wzmacniacz mocy audio w jednym chipie. Wymaga minimalnej liczby elementów zewnętrznych. |
| **Polaryzacja kondensatora** | Kondensatory elektrolityczne mają „+" i „−" — podłączenie odwrotne może je uszkodzić. |
| **Potencjometr** | Rezystor z regulowaną wartością (pokrętło). Trzy piny: dwa skrajne i środkowy (wiper). |
| **PWM (Pulse Width Modulation)** | Modulacja szerokości impulsu — technika sterowania mocą przez szybkie włączanie/wyłączanie napięcia. |
| **Remanencja** | Zdolność materiału magnetycznego do zachowania namagnesowania po usunięciu zewnętrznego pola. Podstawa zapisu na taśmie. |
| **SPST** | Single Pole, Single Throw — najprostszy typ wyłącznika: jedno wejście, jedno wyjście, dwa stany (ON/OFF). |
| **Wow & flutter** | Wahania prędkości taśmy powodujące „kołysanie" lub „drżenie" dźwięku. Mierzone w procentach. |
| **Wzmocnienie (gain)** | Stosunek sygnału wyjściowego do wejściowego. LM386: 20× domyślnie, do 200× z kondensatorem. |

---

## 11. Historia zmian dokumentu

| Wersja | Data | Opis zmian |
|--------|------|------------|
| 1.0 | 2026-02-06 | Pierwsza wersja dokumentacji. |

---

## 12. Licencja i autor

Projekt open-source, do dowolnego użytku. Stworzony z pomocą Claude (Anthropic) jako materiał edukacyjny.

> *„Kaseta magnetofonowa to nie tylko nośnik dźwięku — to lekcja fizyki, elektroniki i mechaniki zamknięta w plastikowej obudowie."*
