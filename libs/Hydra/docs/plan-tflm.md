# Plan — inferencja na urządzeniu (TensorFlow Lite Micro)

Detekcja anomalii w `sense` jest dziś **napisana**: progi, zamrożenie, skok
poza zakres (`AnomalyCfg`). Model robi to samo, tylko **nauczone** — i to jest
cała teza tej pracy. Reszta to zaplecze.

Kolejność jak przy WASM: szew → silnik → produkt. Każdy etap ma wartość osobno.

---

## Etap 0 — szew i element potoku ✅ ZROBIONE

| Rzecz | Gdzie |
|---|---|
| `IInferenceEngine` | `include/hydra/infer/IInferenceEngine.hpp` |
| Silnik bez modelu (energia okna) | `MockEngine` |
| Element potoku | `media::Inference` — ujście, wynik zdarzeniem |
| Zdarzenie `InferenceReady` | `EventBus`, tą samą drogą co `SensorAnomaly` |

**21 testów.** Trzy decyzje zapisane w kodzie:

- **Arena, nie alokacje.** Silnik dostaje ciągły bufor przed startem, tak jak
  `Heap` modułu skryptowego. Reguła „po `App::begin()` nie wolno alokować"
  zostaje bez wyjątków.
- **`invoke()` jest blokujące.** Inferencji nie da się wywłaszczyć w środku,
  inaczej niż skryptu z pułapką instrukcji. Element mierzy czas i liczy
  przekroczenia budżetu zamiast udawać, że model da się przerwać.
- **Werdykt to zdarzenie, nie strumień.** „Anomalia z pewnością 0,87" zdarza
  się rzadko i nieregularnie, a odbiorcą jest sterowanie albo łącze — nie
  kolejny element potoku.

Świadomie **nie** dodano `MediaKind::Tensor`: próbki mikrofonu i odczyty prądu
są tym samym — ciągiem wartości w czasie. Trzeci rodzaj formatu ma sens dopiero
przy cechach MFCC.

---

## Etap 1 — TFLM jako osobna biblioteka ✅ ZROBIONE

`libs/HydraTflm` (202 pliki, 5,3 MB) + `tools/vendor_tflm.sh`.
`TflmEngine` implementuje `IInferenceEngine`; **11 testów**, w tym inferencja
modelu `sin(x)` porównana z prawdziwym sinusem.

Osobna biblioteka, a nie `src/` — ta sama ściana, co przy WAMR: PlatformIO
kompiluje całe `src/`, więc 200 plików trafiałoby do projektów bez modelu.

Pułapki, żadnej nie dało się przewidzieć z dokumentacji — opisane
w `libs/HydraTflm/VENDOR.md`:

1. **`TF_LITE_STATIC_MEMORY` zmienia układ pól `TfLiteTensor`.** Jednostka
   Hydry i biblioteka muszą widzieć tę samą definicję. Objaw niezgodności:
   model wczytuje się, `inputs_size()` zwraca 1, a tensor jest pusty.
2. **Pliki o tych samych nazwach w różnych katalogach** (`common.cpp` ×2,
   `window.cpp` ×2, `kernel_util.cpp` ×3). `notdir` w Makefile sklei je
   w jeden obiekt, a build wygląda na udany aż do konsolidacji.
3. **Generator zamiast ręcznej listy plików.** `create_tflm_tree.py` wymaga
   GNU Make ≥ 3.82 (macOS ma 3.81), `numpy`, `pillow`, `curl`, `patch` —
   dlatego skrypt uruchamia go w kontenerze.

---

## Etap 2 — frontend cech (MFCC) ✅ ZROBIONE

| Rzecz | Gdzie |
|---|---|
| FFT radix-2 dla sygnału rzeczywistego | `util/Fft.hpp` — `powerSpectrum`, `applyHann` |
| Element MFCC | `media::MfccExtractor` |
| Nowy rodzaj danych na padzie | `MediaKind::Features` |

**15 testów**, wszystkie sprawdzalne matematycznie: sinusoida 1000 Hz przy
16 kHz i 256 punktach daje pik w prążku 16, cisza daje zerowe widmo, a ton
niski i wysoki — **różne cechy**. To ostatnie jest sednem: implementacja
zwracająca stałą przeszłaby każdy test na kształt i rozmiar.

### Własne FFT, nie kissfft z TFLM

TFLM niesie kissfft, ale widmo przydaje się także tam, gdzie modelu nie ma:
próg na paśmie, wykrycie tonu, podgląd. Element liczący widmo, który wymaga
pięciu megabajtów biblioteki uczącej, byłby w takim projekcie nie do przyjęcia.
Radix-2 to ~120 wierszy przenośnego C++ — bez tablicy współczynników obrotu,
bo na MCU brakuje najpierw pamięci, a nie taktów.

### `Features`, a nie `Tensor`

Nazwa mówi, czym to jest: to pierwsza rzecz w potoku, która **nie jest
strumieniem w czasie**. Próbki mikrofonu i odczyty prądu mieszczą się
w `Audio` — jedne i drugie są ciągiem wartości o znanej częstotliwości.
Współczynniki cepstralne opisują **okno**: nie mają częstotliwości
próbkowania ani kanałów, mają długość i stawkę okien na sekundę.

Jednostką jest cały wektor, nie pojedyncza liczba — blok z połową wektora
byłby danymi nie do zinterpretowania.

### Decyzje warte zapamiętania

- **Bank filtrów trzyma granice, nie wagi.** Wagi to `filterCount × bins`
  liczb — przy 26 filtrach i 129 prążkach ponad 13 kB. Granice zajmują
  156 bajtów, a wagę trójkąta liczy się w miejscu jednym dzieleniem.
- **Podłoga logarytmu.** `log(0)` to minus nieskończoność, a jedna taka
  wartość zatruwa całe DCT i model dostaje wektor złożony z NaN. Cisza jest
  najczęstszym stanem mikrofonu, więc to nie jest przypadek brzegowy.
- **Bufory w obiekcie, nie z zewnątrz** — inaczej niż okno w `Inference`.
  Ich rozmiar wynika z rozmiaru przekształcenia, czyli z cechy projektu znanej
  przy kompilacji, a nie z modelu, którego wtedy jeszcze nie ma.
- **MFCC nie miesza kanałów.** Średnia z dwóch mikrofonów to co innego niż
  wybór jednego, a element nie ma podstaw, żeby rozstrzygać za użytkownika.

---

## Etap 3 — łańcuch domknięty ✅ ZROBIONE (bez wytrenowanego modelu)

    źródło → MFCC → model → zdarzenie

**7 testów** na złożeniu wszystkich trzech elementów w jednym potoku.
`Inference` przyjmuje teraz `MediaKind::Features`, a nie tylko `Audio`: cechy
są dla modelu tym samym co próbki — ciągiem liczb. Rozdzielenie tego na dwa
elementy oznaczałoby dwie kopie składania okna i przesuwu.

### Czego tu nie ma i dlaczego

**Wytrenowanego modelu mowy.** `micro_speech` z TFLM oczekuje 40 kanałów ze
swojego frontendu, w swojej kwantyzacji i swoim układzie ramek — podanie mu
tych MFCC dałoby liczby, które nie znaczą nic. Model liczący na tych cechach
trzeba wytrenować na zbiorze nagrań, a to jest praca poza urządzeniem.

Silnik w testach jest więc atrapowy, ale **łańcuch jest prawdziwy**: te same
elementy, te same pule, ta sama droga danych. Podmiana `MockEngine` na
`TflmEngine` to jedna linijka, sprawdzona w `test_tflm.cpp`.

### Błąd, który wyszedł dopiero przy złożeniu

Element inferencji czytał wyjście **po jednej wartości**, a `readOutput()`
wymaga całego tensora co do bajta. Przy modelu jednowyjściowym — z etapów 0
i 1 — to było to samo. Przy pierwszym klasyfikatorze dwuklasowym odczyt
zaczął zawodzić, pętla przerywała się na pierwszym obiegu, a werdykt zawsze
wynosił zero.

Objaw był najgorszy z możliwych: łańcuch działał, liczniki rosły, zdarzenia
wychodziły, a wynik był stały. Wyłapał to jedyny test, który porównuje werdykt
dla tonu z werdyktem dla ciszy — reszta przechodziła.

Teraz element czyta całe wyjście naraz, obsługuje modele skwantyzowane
(przeliczenie przez skalę tensora) i odrzuca modele o więcej niż
`HYDRA_INFER_MAX_OUTPUTS` wyjściach z jasnym komunikatem zamiast obcinać.

### Co zostało do prawdziwego rozpoznawania słowa

1. **Zbiór nagrań i trening** — poza urządzeniem, wynik jako `.tflite`.
2. **Wejście I2S** zamiast `ToneSource` — element już jest (`I2sSource`).
3. **Wygładzanie werdyktów.** Pojedyncze okno bywa mylne; rozpoznawanie mowy
   liczy zwykle średnią z kilku kolejnych okien i próg. To kilkadziesiąt
   wierszy, ale nie ma sensu pisać ich przed modelem — próg dobiera się do
   rozkładu wyników, którego jeszcze nie ma.

---

## Etap 4 — `sense`: model obok `AnomalyDetector` ✅ ZROBIONE

| Rzecz | Gdzie |
|---|---|
| Detektor nauczony | `sense::ModelDetector` |
| Nowy rodzaj anomalii | `AnomalyKind::Learned` |
| Wpięcie w hub | `SensorHub::attachModel()` / `detachModel()` |

**16 testów.** Najważniejszy pokazuje przebieg, którego **żaden próg nie
łapie**, a model łapie: wartości drgające między 0,9 a 1,1 co próbkę. Czujnik
nie stoi (więc `Frozen` milczy), skoki mieszczą się w limicie (`Spike` milczy),
zakres zachowany (`OutOfRange` milczy) — a przebieg jest inny niż zwykle.

Przy okazji wyszła rzecz, która jest sednem różnicy: **energia okna tego nie
odróżnia** (1,005 wobec 1,000), bo mierzy poziom, a zmiana jest w kształcie.
Miara musi patrzeć na to, co się naprawdę zmienia — i właśnie tego uczy się
model zamiast dostać to wpisane progiem.

### Decyzje

- **To samo zdarzenie `SensorAnomaly`, nie osobne.** Osobne oznaczałoby, że
  każdy odbiorca anomalii musi subskrybować dwa tematy, żeby nie przegapić
  połowy. `AnomalyKind::Learned` pozwala rozróżnić źródło temu, kogo to
  interesuje — a różnica jest istotna: próg mówi **co** jest nie tak i jest
  tłumaczalny, model mówi tylko, że coś, i bywa w błędzie.
- **Próg ma pierwszeństwo.** Dwa zdarzenia o tej samej próbce znaczą dla
  odbiorcy „dwie anomalie", a nie „ta sama, widziana dwa razy".
- **Model dostaje próbkę zawsze**, także gdy próg już zgłosił. Pominięcie
  zostawiłoby dziurę w oknie i dało modelowi przebieg, którego nie widział
  przy uczeniu.
- **Detektor wpięty wskaźnikiem, nie wartością.** Wpisy czujników są tablicą
  statyczną; okno próbek w każdym wpisie kosztowałoby pamięć wszystkich
  czujników po to, żeby dać ją jednemu.
- **Model patrzy na jeden kanał.** Okno z przeplecionych kanałów wymagałoby
  modelu uczonego dokładnie na tym przeplocie.
- **`lastScore()` jest dostępny także poniżej progu** — bez tego nie da się
  progu dobrać do rozkładu wyników na danych bez usterki.

### Co zostaje do wdrożenia u siebie

Model. Detektor karmi go oknem próbek po kalibracji i filtrze, więc dane do
uczenia zbiera się tym samym łańcuchem, którym potem płyną: podpiąć `Sample`
z INA219 albo AS5600, nagrać przebieg bez usterki, wytrenować autoenkoder albo
klasyfikator jednoklasowy, przekonwertować do `.tflite`. Reszta jest gotowa.

---

## Etap 5 — model dostarczany przez sieć ✅ ZROBIONE (strona urządzenia)

`infer::ModelDelivery` na `script::ImageStore` — tym samym magazynie, którego
używa dostarczanie skryptu. **8 testów** z prawdziwym modelem i prawdziwym
silnikiem TFLM.

Powtórzenie mechaniki osobno dla modelu oznaczałoby drugie miejsce, w którym
trzeba pamiętać o kolejności „zweryfikuj, potem przełącz" — a to jest dokładnie
ta kolejność, której złamanie kosztuje wizytę z programatorem.

### Sedno: urządzenie nie może zostać bez modelu

Model wgrywany zdalnie różni się od wgranego z firmware jedną rzeczą: **można
go dostać zepsutego**. Uszkodzony w drodze, z innego konwertera, za duży na
arenę. Test „model odrzucony przez silnik nie zostawia urządzenia bez modelu"
sprawdza najgorszy przypadek: transfer bez zarzutu, skrót się zgadza, a treść
nie jest modelem. Poprzedni wraca i liczy dalej.

Urządzenie bez żadnego modelu przestaje robić to, po co je postawiono —
a transfer „się udał", więc nikt tego nie widzi.

### Czym się różni od dostarczania skryptu

**Nie ma okresu próbnego liczonego błędami wykonania.** Skrypt wywracający się
w `setup()` daje serię błędów i po `maxConsecutiveErrors` wraca do poprzedniej
wersji. Model albo się wczyta, albo nie — `AllocateTensors()` zawodzi
natychmiast. Próba jest jednorazowa i dzieje się przy podmianie.

**Model bywa większy.** Dwa sloty znaczą dwa razy tyle pamięci i to jest cena
możliwości wycofania. Przy detekcji anomalii to kilka kilobajtów, przy
rozpoznawaniu słowa — kilkadziesiąt.

### Co zostaje po stronie MyCastle

Rozszerzenie `ext/model` w backendzie i przycisk w Studiu — dokładnie ten sam
kształt, co `ext/script` dla modułów WebAssembly (`ScriptExtension.ts`,
`scriptUpload.ts`, panel „Moduł WASM"). Strona urządzenia jest gotowa
i przetestowana; brakuje ogniwa, które wyśle bajty.

---

## Czego świadomie nie ma

**CMSIS-NN i jądra optymalizowane pod rdzeń.** Wchodzą, gdy pomiar pokaże, że
są potrzebne — nie zanim pierwszy model ruszy na urządzeniu.

**Uczenie na urządzeniu.** TFLM go nie robi i nie ma planów; model powstaje
poza urządzeniem, a tu tylko liczy.
