# 🎵🔴 DIY Odtwarzacz i Rejestrator Kasetowy — Dokumentacja Projektu

> **Wersja:** 1.0
> **Data:** 2026-02-06
> **Poziom trudności:** ⭐⭐ Średniozaawansowany
> **Szacowany czas montażu:** 4–6 godzin
> **Szacowany koszt:** 80–150 zł
> **Projekt bazowy:** [SimpleCassettePlayer](../../SimplestCassettePlayer/docs/SimpleCassettePlayer.md)

---

## 1. Opis projektu

Projekt jest rozszerzeniem [SimpleCassettePlayer](../../SimplestCassettePlayer/docs/SimpleCassettePlayer.md) o pełną funkcjonalność **nagrywania** na taśmę magnetyczną. Oprócz odtwarzania (identycznego jak w projekcie bazowym), urządzenie umożliwia rejestrację dźwięku z mikrofonu elektretowego na kasetę magnetofonową.

Nagrywanie na taśmę wymaga trzech dodatkowych bloków, których nie było w odtwarzaczu:

1. **Przedwzmacniacz mikrofonowy** — wzmacnia słaby sygnał z mikrofonu (~2–20 mV) do poziomu ok. 100–500 mV wymaganego do namagnesowania taśmy.
2. **Oscylator bias/erase** — generuje sygnał ultradźwiękowy (~60–100 kHz), który linearyzuje zapis magnetyczny (bias) i jednocześnie kasuje poprzednie nagranie (erase).
3. **Mieszacz audio** — sumuje sygnał audio z sygnałem bias przed podaniem na głowicę zapisującą.

### 1.1. Cele projektu

- Rozbudowa odtwarzacza kasetowego o funkcję nagrywania.
- Zrozumienie pełnego procesu zapisu magnetycznego (bias, kasowanie, mieszanie sygnałów).
- Nauka budowy przedwzmacniacza tranzystorowego (wspólny emiter).
- Poznanie oscylatorów LC (oscylator Colpittsa).
- Zrozumienie przełączania torów sygnałowych (tryb Play/Record).

### 1.2. Wymagania wstępne

Przed rozpoczęciem tego projektu zaleca się ukończenie [SimpleCassettePlayer](../../SimplestCassettePlayer/docs/SimpleCassettePlayer.md) — ten projekt zakłada, że masz już działający odtwarzacz i rozumiesz tor odtwarzania (głowica → C1 → R1 → LM386 → głośnik).

---

## 2. Teoria — jak działa nagrywanie na taśmę

### 2.1. Przypomnienie — zasada zapisu magnetycznego

Zapis dźwięku na taśmie polega na przepuszczeniu prądu audio przez cewkę głowicy zapisującej. Prąd generuje zmienne pole magnetyczne w szczelinie głowicy, a przejeżdżająca taśma zostaje namagnesowana proporcjonalnie do sygnału — cząsteczki magnetyczne „zamrażają" swoje namagnesowanie dzięki remanencji (magnetyzmowi resztkowemu).

### 2.2. Problem nieliniowości — dlaczego potrzebujemy bias

Materiał magnetyczny na taśmie ma nieliniową charakterystykę magnesowania (krzywą histerezy). Gdybyśmy nagrywali sam sygnał audio bezpośrednio, małe amplitudy byłyby praktycznie niesłyszalne (martwa strefa wokół zera), a duże — silnie zniekształcone.

```
Namagnesowanie (M)
    │         ╱───── nasycenie
    │        ╱
    │       ╱
    │     ╱    ← strefa liniowa (chcemy tu pracować!)
    │   ╱
    │  ╱
────┼─╱────────── Pole (H)
    │╱  ↑
    │   martwa strefa (zniekształcenia!)
```

Rozwiązaniem jest **polaryzacja AC (AC bias)** — dodanie do sygnału audio sygnału ultradźwiękowego o częstotliwości znacznie przekraczającej pasmo słyszalne (typowo 60–100 kHz). Sygnał bias „przesuwa" punkt pracy na liniowy fragment krzywej histerezy, drastycznie poprawiając jakość zapisu.

### 2.3. Sygnał bias — parametry

| Parametr | Wartość typowa | Uwagi |
|----------|----------------|-------|
| Częstotliwość | 60–100 kHz | Minimum 4–5× najwyższa częstotliwość audio (20 kHz) |
| Kształt fali | Sinusoida | Możliwa fala prostokątna, ale daje więcej szumów |
| Amplituda | 30–80 mV na głowicy | Zależy od typu taśmy; zbyt mało = zniekształcenia, zbyt dużo = kasowanie sygnału |

### 2.4. Kasowanie (erase) — przygotowanie taśmy

Przed nagraniem nowego materiału taśma musi zostać skasowana. Kasowanie polega na przepuszczeniu taśmy obok **głowicy kasującej (erase head)**, zasilanej tym samym sygnałem ultradźwiękowym co bias, ale o znacznie większej amplitudzie. Silne zmienne pole magnetyczne wielokrotnie przemagnesowuje cząsteczki taśmy, a gdy taśma oddala się od szczeliny głowicy — pole maleje i cząsteczki zostają w stanie zdemagnetyzowanym (losowym), czyli „czystym".

W naszym projekcie ten sam oscylator zasila zarówno głowicę kasującą (wysoka amplituda), jak i tor bias (niska amplituda, przez dzielnik napięcia).

### 2.5. Głowica R/P (Record/Playback)

W prostych magnetofonach ta sama głowica służy zarówno do odczytu, jak i do zapisu — nazywamy ją głowicą R/P. Szerokość szczeliny głowicy R/P jest kompromisem między wymaganiami odtwarzania (wąska szczelina = lepsza odpowiedź wysokich częstotliwości) a nagrywania (szersza szczelina = głębsze namagnesowanie taśmy).

Profesjonalne decki mają oddzielne głowice — jedną do nagrywania, drugą do odtwarzania (deck 3-głowicowy). W naszym projekcie używamy jednej głowicy R/P, co jest standardem w prostych magnetofonach.

### 2.6. Przełącznik Play/Record

Kluczowym elementem jest przełącznik DPDT (Double Pole, Double Throw), który zmienia routing sygnałów:

```
Tryb PLAY (odtwarzanie):
  Głowica R/P → [C1] → [R1] → LM386 → Głośnik
  Oscylator bias: WYŁĄCZONY
  Głowica kasująca: ODŁĄCZONA

Tryb RECORD (nagrywanie):
  Mikrofon → [Preamp] → [Mieszacz + Bias] → Głowica R/P
  Oscylator bias: WŁĄCZONY → Głowica kasująca + Bias do mieszacza
  LM386: ODŁĄCZONY od głowicy (opcjonalnie: monitoring z preamp)
```

---

## 3. Architektura układu

### 3.1. Schemat blokowy — tryb odtwarzania (Play)

```
┌──────────┐    ┌──────────┐    ┌───────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ BATERIA  │───►│ SILNIK   │───►│KASETA │───►│ GŁOWICA  │───►│ LM386    │───►│ GŁOŚNIK  │
│ 9V       │    │ DC 9V    │    │       │    │ R/P      │    │ wzmacn.  │    │ 8Ω       │
└──────────┘    └──────────┘    └───────┘    └──────────┘    └──────────┘    └──────────┘
                                                  ▲
                                           Przełącznik DPDT
                                           pozycja: PLAY
```

### 3.2. Schemat blokowy — tryb nagrywania (Record)

```
┌──────────┐    ┌──────────┐    ┌───────┐    ┌──────────┐
│ BATERIA  │───►│ SILNIK   │───►│KASETA │◄───│ GŁOWICA  │◄──── MIESZACZ
│ 9V       │    │ DC 9V    │    │       │    │ R/P      │      ▲     ▲
└──────────┘    └──────────┘    └───────┘    └──────────┘      │     │
                                                                │     │
┌──────────┐    ┌──────────┐                              ┌─────┘     └─────┐
│ MIKROFON │───►│ PREAMP   │──────────────────────────────┘                 │
│ elektret │    │ Q1 (CE)  │                                          ┌─────┴─────┐
└──────────┘    └──────────┘                                          │ OSCYLATOR │
                                                                      │ BIAS/ERASE│
                  ┌──────────┐                                        │ ~80 kHz   │
                  │ GŁOWICA  │◄───────────────────────────────────────┘
                  │ KASUJĄCA │  (wysoka amplituda)
                  └──────────┘
```

### 3.3. Opis torów sygnałowych

#### Tor nagrywania

**Mikrofon elektretowy** — przetwarza dźwięk na sygnał elektryczny o amplitudzie ~2–20 mV. Wymaga zasilania polaryzacyjnego (1–10V) przez rezystor (R2 = 4,7–10 kΩ).

**Przedwzmacniacz (Q1)** — tranzystor NPN (np. 2N3904 lub BC547) w konfiguracji wspólnego emitera. Wzmocnienie ~50–100×. Wzmacnia sygnał z mikrofonu do poziomu ~100–500 mV, odpowiedniego do namagnesowania taśmy.

**Potencjometr poziomu nagrywania (R_REC)** — reguluje amplitudę sygnału podawanego na głowicę. Zbyt niski poziom = cichy zapis, zbyt wysoki = przesterowanie i zniekształcenia.

**Mieszacz** — prosty sumator rezystorowy łączący sygnał audio z sygnałem bias. Rezystory mieszacza dobierają proporcje audio/bias.

**Oscylator bias/erase** — oscylator Colpittsa na tranzystorze NPN (Q2) z obwodem LC (cewka + kondensatory). Generuje sinusoidę ~60–100 kHz. Jeden tor (wysoka amplituda) zasila głowicę kasującą, drugi (niska amplituda, przez dzielnik) dodaje bias do sygnału audio.

**Głowica kasująca (erase head)** — oddzielna głowica umieszczona przed głowicą R/P (w kierunku ruchu taśmy). Kasuje poprzednie nagranie zanim taśma dotrze do głowicy zapisującej.

**Głowica R/P** — w trybie Record przyjmuje sygnał z mieszacza i namagnesowuje taśmę.

#### Tor odtwarzania

Identyczny jak w [SimpleCassettePlayer](../../SimplestCassettePlayer/docs/SimpleCassettePlayer.md):
Głowica R/P → C1 (100 nF) → R1 (10 kΩ pot.) → LM386 → C2 (220 µF) → Głośnik.

---

## 4. Lista materiałów (BOM)

### 4.1. Elementy z projektu bazowego (SimpleCassettePlayer)

| # | Ozn. | Element | Wartość / typ | Ilość |
|---|------|---------|---------------|-------|
| 1 | — | Głowica magnetyczna R/P | Stereo, do kasety kompaktowej | 1 szt. |
| 2 | — | Silnik DC | 9V, typ EG-530AD-2F (CCW) | 1 szt. |
| 3 | — | Kaseta magnetofonowa | C-60 lub C-90 | 1+ szt. |
| 4 | U1 | Wzmacniacz audio | LM386N-1, DIP-8 | 1 szt. |
| 5 | C1 | Kondensator ceramiczny | 100 nF | 1 szt. |
| 6 | C2 | Kondensator elektrolityczny | 220 µF / 16V | 1 szt. |
| 7 | C3 | Kondensator elektrolityczny | 10 µF / 16V | 1 szt. |
| 8 | R1 | Potencjometr obrotowy | 10 kΩ liniowy (B10K) | 1 szt. |
| 9 | SW1 | Wyłącznik zasilania | SPST ON/OFF | 1 szt. |
| 10 | — | Głośnik | 8 Ω, 0,5–1 W | 1 szt. |
| 11 | — | Bateria 9V + zatrzask | 6F22 | 1 kpl. |
| 12 | — | Płytka stykowa | Breadboard 830 punktów | 1 szt. |
| 13 | — | Kabelki połączeniowe | Jumper wires M-M | 1 zestaw |

### 4.2. Dodatkowe elementy — tor nagrywania

#### 4.2.1. Głowica kasująca i mikrofon

| # | Element | Parametry | Ilość | Cena orientacyjna |
|---|---------|-----------|-------|-------------------|
| 14 | Głowica kasująca (erase head) | Do kasety kompaktowej, 2 piny | 1 szt. | 5–15 zł |
| 15 | Mikrofon elektretowy | Kapsułka Ø 6–10 mm, 2 piny | 1 szt. | 1–3 zł |

#### 4.2.2. Przedwzmacniacz mikrofonowy

| # | Ozn. | Element | Wartość / typ | Ilość | Cena |
|---|------|---------|---------------|-------|------|
| 16 | Q1 | Tranzystor NPN | 2N3904 lub BC547 | 1 szt. | ~0,50 zł |
| 17 | R2 | Rezystor | 4,7 kΩ (zasilanie mikrofonu) | 1 szt. | ~0,10 zł |
| 18 | R3 | Rezystor | 100 kΩ (polaryzacja bazy Q1) | 1 szt. | ~0,10 zł |
| 19 | R4 | Rezystor | 10 kΩ (kolektor Q1) | 1 szt. | ~0,10 zł |
| 20 | R5 | Rezystor | 1 kΩ (emiter Q1) | 1 szt. | ~0,10 zł |
| 21 | C4 | Kondensator elektrolityczny | 10 µF / 16V (sprzęgający wejście preamp) | 1 szt. | ~0,30 zł |
| 22 | C5 | Kondensator elektrolityczny | 10 µF / 16V (sprzęgający wyjście preamp) | 1 szt. | ~0,30 zł |
| 23 | C6 | Kondensator elektrolityczny | 100 µF / 16V (bypass emitera) | 1 szt. | ~0,30 zł |
| 24 | R_REC | Potencjometr obrotowy | 10 kΩ liniowy (B10K) — poziom nagrywania | 1 szt. | 2–4 zł |

#### 4.2.3. Oscylator bias/erase

| # | Ozn. | Element | Wartość / typ | Ilość | Cena |
|---|------|---------|---------------|-------|------|
| 25 | Q2 | Tranzystor NPN | 2N2222 lub BC547 | 1 szt. | ~0,50 zł |
| 26 | L1 | Cewka indukcyjna | 1–2,2 mH (rdzeń ferrytowy) | 1 szt. | 1–3 zł |
| 27 | C7 | Kondensator ceramiczny | 2,2–4,7 nF (obwód rezonansowy z L1) | 1 szt. | ~0,20 zł |
| 28 | C8 | Kondensator ceramiczny | 10–22 nF (sprzężenie zwrotne Colpittsa) | 1 szt. | ~0,20 zł |
| 29 | C9 | Kondensator ceramiczny | 10–22 nF (sprzężenie zwrotne Colpittsa) | 1 szt. | ~0,20 zł |
| 30 | R6 | Rezystor | 10 kΩ (polaryzacja bazy Q2) | 1 szt. | ~0,10 zł |
| 31 | R7 | Rezystor | 1 kΩ (emiter Q2) | 1 szt. | ~0,10 zł |
| 32 | R8 | Rezystor | 47 kΩ (polaryzacja bazy Q2) | 1 szt. | ~0,10 zł |

#### 4.2.4. Mieszacz i routing

| # | Ozn. | Element | Wartość / typ | Ilość | Cena |
|---|------|---------|---------------|-------|------|
| 33 | R9 | Rezystor | 47 kΩ (mieszacz — sygnał audio) | 1 szt. | ~0,10 zł |
| 34 | R10 | Rezystor | 220 kΩ (mieszacz — bias, tłumienie) | 1 szt. | ~0,10 zł |
| 35 | R11 | Rezystor | 100 Ω (ogranicznik prądu erase head) | 1 szt. | ~0,10 zł |
| 36 | C10 | Kondensator ceramiczny | 100 nF (sprzęgający wyjście mieszacza) | 1 szt. | ~0,20 zł |
| 37 | SW2 | Przełącznik DPDT | Double Pole, Double Throw (Play/Record) | 1 szt. | 2–5 zł |

### 4.3. Podsumowanie kosztów

| Kategoria | Koszt |
|-----------|-------|
| Elementy z projektu bazowego | ~40–80 zł |
| Głowica kasująca + mikrofon | 6–18 zł |
| Przedwzmacniacz (Q1 + pasywne) | 4–6 zł |
| Oscylator bias/erase (Q2 + LC + pasywne) | 3–5 zł |
| Mieszacz + przełącznik | 3–6 zł |
| **RAZEM** | **~80–150 zł** |

---

## 5. Schemat połączeń

### 5.1. Przedwzmacniacz mikrofonowy (Q1 — wspólny emiter)

```
         Vcc (9V)
          │
         [R2]  4,7 kΩ        Vcc (9V)
          │                    │
          ├─── MIC (+) ──┐   [R4]  10 kΩ
          │               │    │
         MIC (−)          │    ├──── C5 (+) ──── do R_REC (pot. poziomu nagryw.)
          │               │    │     10 µF
         GND        C4 (+)│    │
                    10 µF │  ┌─┤ C (kolektor)
                          │  │ │
                 [R3]─────┴──┤ Q1 (2N3904/BC547)
                100 kΩ       │ │
                    ┌────────┤ E (emiter)
                    │        └─┤ B (baza)
                    │          │
                   [R5]  1 kΩ  └── z C4 (−)
                    │
                   [C6]  100 µF (bypass)
                    │
                   GND
```

**Objaśnienie:**

- **R2 (4,7 kΩ)** — rezystor zasilający mikrofon elektretowy. Mikrofon elektretowy potrzebuje napięcia polaryzacyjnego ~1–9V przepływającego przez rezystor.
- **C4 (10 µF)** — kondensator sprzęgający oddzielający DC z mikrofonu od bazy tranzystora.
- **R3 (100 kΩ)** — polaryzacja bazy Q1. Ustala punkt pracy tranzystora na ~0,6–0,7V na bazie.
- **R4 (10 kΩ)** — rezystor kolektora. Sygnał wzmocniony pojawia się na kolektorze Q1.
- **R5 (1 kΩ)** — rezystor emitera. Stabilizuje punkt pracy i ustala wzmocnienie na ~R4/R5 = 10 (bez bypass) lub wyższe z C6.
- **C6 (100 µF)** — kondensator bypass emitera. Dla sygnałów AC omija R5, zwiększając wzmocnienie AC do ~50–100×.
- **C5 (10 µF)** — kondensator sprzęgający wyjście preampa, blokujący DC.

### 5.2. Oscylator bias/erase (Colpitts, Q2)

```
           Vcc (9V)
            │
           [R6]  10 kΩ
            │
            ├──────────── do L1 (cewki)
            │                │
           [R8] 47 kΩ       [L1]  1–2,2 mH
            │                │
            │        ┌───────┤
            │        │       │
            └──┤ B   │      [C7]  3,3 nF
               │     │       │
         Q2 (2N2222) │       ├──── WYJŚCIE BIAS ──┬── R10 (220 kΩ) ── do mieszacza
               │     │       │                     │
            E ─┤     │      GND                    └── R11 (100 Ω) ── do erase head
               │     │
              [C9]   [C8]
            22 nF    22 nF
               │      │
              [R7]    GND
             1 kΩ
               │
              GND
```

**Objaśnienie:**

Oscylator Colpittsa wykorzystuje obwód rezonansowy LC złożony z cewki L1 i kondensatora C7. Częstotliwość oscylacji:

```
f = 1 / (2π × √(L1 × C7))

Przykład: L1 = 1 mH, C7 = 3,3 nF
f = 1 / (2π × √(0,001 × 0,0000000033))
f ≈ 88 kHz ✓ (w zakresie 60–100 kHz)
```

Kondensatory C8 i C9 tworzą dzielnik pojemnościowy zapewniający sprzężenie zwrotne do emitera Q2 — warunek konieczny do podtrzymania oscylacji.

**R6 i R8** tworzą dzielnik napięcia polaryzujący bazę Q2. **R7** jest rezystorem emitera stabilizującym punkt pracy.

### 5.3. Mieszacz (sumator rezystorowy)

```
Sygnał audio ──── [R_REC wiper] ──── [R9]  47 kΩ ────┐
(z preamp C5)                                          ├──── [C10] 100 nF ──── Głowica R/P
                                                       │
Sygnał bias  ──── [R10] 220 kΩ ───────────────────────┘
(z oscylatora)
```

**R9 (47 kΩ)** przepuszcza sygnał audio, a **R10 (220 kΩ)** tłumi sygnał bias do odpowiednio niskiego poziomu. Proporcja R10/R9 ≈ 4,7:1 oznacza, że sygnał bias na głowicy jest ~5× słabszy niż audio — to typowa proporcja dla dobrego zapisu.

**C10 (100 nF)** — kondensator sprzęgający blokujący składową DC przed głowicą.

### 5.4. Przełącznik Play/Record (SW2 — DPDT)

```
                         SW2 (DPDT)
                    ┌────────────────────┐
                    │  PLAY      RECORD  │
Głowica R/P ───────┤A ●──1A      2A──●  ├─── Wyjście mieszacza (C10)
                    │     │        │     │
                    │    1A'      2A'    │
                    │     │        │     │
Do C1 (wejście  ◄──┤B ●──1B      2B──●  ├─── Vcc (zasilanie oscylatora Q2)
LM386)              │     │        │     │
                    │    1B'      2B'    │
                    │     │        │     │
                    │    N/C      GND    │  (1B' niepodłączone, 2B' do GND)
                    └────────────────────┘

Pozycja PLAY:
  A: Głowica R/P ──► do C1 ──► LM386 (odtwarzanie)
  B: Oscylator Q2 odłączony od zasilania (nie oscyluje)

Pozycja RECORD:
  A: Głowica R/P ◄── z mieszacza (nagrywanie)
  B: Oscylator Q2 zasilany ──► bias + erase aktywne
```

**Kluczowe:** Przełącznik SW2 jednocześnie:
1. Przełącza głowicę R/P między torem odczytu (→ LM386) a torem zapisu (← mieszacz).
2. Włącza/wyłącza oscylator bias/erase (przez podłączenie/odłączenie zasilania).

### 5.5. Tabela połączeń — nowe elementy

| Z (skąd) | Do (dokąd) | Przez element | Uwagi |
|-----------|-----------|---------------|-------|
| Vcc (9V) | MIC (+) | R2 (4,7 kΩ) | Zasilanie mikrofonu elektretowego |
| MIC (−) | Szyna GND | — | Masa mikrofonu |
| MIC (+) / punkt R2-MIC | Baza Q1 | C4 (10 µF, + do MIC) | Sprzężenie AC sygnału mikrofonu |
| Vcc (9V) | Baza Q1 | R3 (100 kΩ) | Polaryzacja bazy |
| Vcc (9V) | Kolektor Q1 | R4 (10 kΩ) | Rezystor kolektora |
| Emiter Q1 | GND | R5 (1 kΩ) | Rezystor emitera |
| Emiter Q1 | GND | C6 (100 µF, + do emitera) | Bypass emitera (równolegle z R5) |
| Kolektor Q1 | R_REC (skrajny pin) | C5 (10 µF, + do kolektora) | Wyjście preamp, sprzężenie AC |
| R_REC (skrajny pin 2) | GND | — | Drugi koniec potencjometru |
| R_REC (wiper) | Punkt mieszacza | R9 (47 kΩ) | Sygnał audio do mieszacza |
| Oscylator (wyjście) | Punkt mieszacza | R10 (220 kΩ) | Bias do mieszacza (stłumiony) |
| Oscylator (wyjście) | Głowica kasująca (+) | R11 (100 Ω) | Erase signal (wysoka amplituda) |
| Głowica kasująca (−) | GND | — | Masa erase head |
| Punkt mieszacza | SW2 pin 2A | C10 (100 nF) | Sygnał Record do przełącznika |
| Głowica R/P (sygnał) | SW2 pin A (wspólny) | — | Głowica podłączona do przełącznika |
| SW2 pin 1A | C1 (wejście LM386) | — | Pozycja PLAY: głowica → odtwarzanie |
| SW2 pin 1B | N/C | — | Pozycja PLAY: oscylator wyłączony |
| SW2 pin 2B | Zasilanie Vcc oscylatora | — | Pozycja RECORD: oscylator włączony |

---

## 6. Przewodnik montażu krok po kroku

### 6.1. Przygotowanie

Upewnij się, że masz działający układ odtwarzacza z projektu SimpleCassettePlayer. Nowe elementy będziemy dodawać **obok** istniejącego układu na tej samej płytce stykowej (potrzebujesz breadboardu 830 punktów lub dwóch połączonych 400-punktowych).

### 6.2. Krok 1 — Budowa przedwzmacniacza mikrofonowego

1. Umieść tranzystor Q1 (2N3904 lub BC547) na breadboardzie. Zidentyfikuj piny (patrząc od przodu — płaska strona): Emiter (E), Baza (B), Kolektor (C).
2. Podłącz R4 (10 kΩ) od Vcc do kolektora Q1.
3. Podłącz R5 (1 kΩ) od emitera Q1 do GND.
4. Podłącz C6 (100 µF, + do emitera) równolegle z R5 (od emitera do GND).
5. Podłącz R3 (100 kΩ) od Vcc do bazy Q1.
6. Podłącz R2 (4,7 kΩ) od Vcc — drugi koniec R2 to punkt zasilania mikrofonu.
7. Podłącz mikrofon elektretowy: pin (+) do punktu R2, pin (−) do GND.
8. Podłącz C4 (10 µF, + do strony mikrofonu) od punktu R2-MIC do bazy Q1.
9. Podłącz C5 (10 µF, + do kolektora) od kolektora Q1 do jednego skrajnego pinu R_REC.
10. Drugi skrajny pin R_REC do GND, środkowy pin (wiper) — to wyjście audio preampa.

**Test:** Podłącz zasilanie, dotknij palcem bazy Q1 — powinnaś usłyszeć brum w głośniku (jeśli podłączysz wiper R_REC do wejścia LM386 tymczasowo). To potwierdza, że preamp działa.

### 6.3. Krok 2 — Budowa oscylatora bias/erase

1. Umieść tranzystor Q2 (2N2222 lub BC547) na breadboardzie.
2. Podłącz R7 (1 kΩ) od emitera Q2 do GND.
3. Podłącz cewkę L1 (1–2,2 mH) od Vcc do kolektora Q2.
4. Podłącz C7 (3,3 nF) od kolektora Q2 do GND — to obwód rezonansowy z L1.
5. Podłącz C8 (22 nF) od kolektora Q2 do bazy Q2.
6. Podłącz C9 (22 nF) od bazy Q2 do GND (przez R7 — od bazy do punktu emitera).
7. Podłącz R6 (10 kΩ) od Vcc do bazy Q2.
8. Podłącz R8 (47 kΩ) od bazy Q2 do GND.

**Test:** Podłącz zasilanie oscylatora. Jeśli masz oscyloskop — zmierz sygnał na kolektorze Q2. Powinieneś zobaczyć sinusoidę ~60–100 kHz. Bez oscyloskopu: podłącz radio AM w pobliżu — oscylator powinien powodować słyszalne zakłócenia (dowód, że generuje sygnał RF).

### 6.4. Krok 3 — Mieszacz

1. Podłącz R9 (47 kΩ) od wipera R_REC (wyjście preampa) do wspólnego punktu mieszacza.
2. Podłącz R10 (220 kΩ) od wyjścia oscylatora (kolektor Q2) do tego samego punktu.
3. Podłącz C10 (100 nF) od punktu mieszacza — drugi koniec C10 to sygnał Record gotowy do podania na głowicę.

### 6.5. Krok 4 — Głowica kasująca

1. Podłącz R11 (100 Ω) od wyjścia oscylatora (kolektor Q2) do jednego pinu głowicy kasującej.
2. Drugi pin głowicy kasującej podłącz do GND.
3. Umieść głowicę kasującą **przed** głowicą R/P w kierunku ruchu taśmy — taśma najpierw mija głowicę kasującą, potem głowicę R/P.

### 6.6. Krok 5 — Przełącznik Play/Record (SW2)

Przełącznik DPDT ma 6 pinów: 2 wspólne (środkowe), 4 pozycyjne (po 2 na stronę).

```
     1A ── A ── 2A
     1B ── B ── 2B
```

1. Pin A (wspólny, górna sekcja) → przewód sygnałowy głowicy R/P.
2. Pin 1A (pozycja PLAY) → do kondensatora C1 (wejście toru odtwarzania LM386).
3. Pin 2A (pozycja RECORD) → wyjście C10 (sygnał z mieszacza).
4. Pin B (wspólny, dolna sekcja) → Vcc (zasilanie oscylatora, linia R6).
5. Pin 1B (pozycja PLAY) → niepodłączony (oscylator wyłączony).
6. Pin 2B (pozycja RECORD) → Vcc główne (oscylator zasilony).

**Uwaga:** Połączenie zasilania oscylatora przez SW2 oznacza, że oscylator działa TYLKO w trybie Record. W trybie Play jest odłączony od zasilania i nie generuje zakłóceń.

### 6.7. Krok 6 — Test nagrywania

1. Przełącz SW2 w pozycję RECORD.
2. Włóż kasetę (najlepiej pustą lub taką, którą możesz skasować).
3. Włącz zasilanie (SW1). Silnik powinien zacząć kręcić.
4. Mów do mikrofonu lub puść muzykę w pobliżu.
5. Ustaw R_REC na ~50% (środek zakresu).
6. Nagraj 30–60 sekund materiału.
7. Przewiń kasetę na początek.
8. Przełącz SW2 w pozycję PLAY.
9. Powinieneś usłyszeć nagrany materiał z głośnika.

### 6.8. Krok 7 — Strojenie

**Poziom nagrywania (R_REC):** Zacznij od niskiego poziomu i zwiększaj. Jeśli nagranie jest zniekształcone — zmniejsz R_REC. Jeśli zbyt ciche — zwiększ.

**Bias:** Optymalny poziom bias jest krytyczny dla jakości zapisu. Zbyt mało bias = zniekształcenia harmoniczne. Zbyt dużo bias = kasowanie wysokich częstotliwości (nagranie brzmi głucho). Regulacja: zmień wartość R10 (mniejszy R10 = więcej bias, większy R10 = mniej bias). Eksperymentuj, nagrywając krótkie fragmenty z różnymi wartościami R10 (100 kΩ, 220 kΩ, 470 kΩ).

**Częstotliwość oscylatora:** Jeśli słyszysz pisk podczas nagrywania — częstotliwość oscylatora jest zbyt niska (w paśmie słyszalnym). Zmniejsz C7 lub użyj cewki L1 o mniejszej indukcyjności, aby podnieść częstotliwość powyżej 40 kHz.

---

## 7. Troubleshooting

### 7.1. Problemy z przedwzmacniaczem

| Problem | Możliwa przyczyna | Rozwiązanie |
|---------|-------------------|-------------|
| Brak sygnału z mikrofonu | Mikrofon odwrotnie podłączony | Mikrofon elektretowy jest spolaryzowany — zamień piny (+) i (−) |
| Brak sygnału z mikrofonu | R2 nie podłączony | Mikrofon elektretowy MUSI mieć zasilanie przez R2 |
| Silne zniekształcenia | Zły punkt pracy Q1 | Zmierz napięcie na kolektorze Q1 — powinno być ~3–6V (ok. połowy Vcc). Dostosuj R3 |
| Cichy sygnał | C6 brak lub odwrotnie | Sprawdź kondensator bypass emitera — bez niego wzmocnienie jest ~10× zamiast ~100× |
| Brum 50 Hz | Brak C4 | Kondensator sprzęgający C4 odcina DC z mikrofonu — bez niego baza Q1 jest zalewana zakłóceniami |

### 7.2. Problemy z oscylatorem

| Problem | Możliwa przyczyna | Rozwiązanie |
|---------|-------------------|-------------|
| Oscylator nie startuje | Zła polaryzacja Q2 | Zmierz Vbe — powinno być ~0,6V. Sprawdź R6 i R8 |
| Oscylator nie startuje | Cewka L1 uszkodzona | Zmierz rezystancję L1 — powinna być niska (1–50 Ω). Nieskończoność = przerwany zwój |
| Słyszalny pisk | Częstotliwość za niska | Zmniejsz C7 lub użyj L1 o mniejszej indukcyjności |
| Niestabilna częstotliwość | Luźne połączenia | Obwód LC jest wrażliwy na pojemności pasożytnicze — skróć przewody, upewnij się o dobrych stykach |

### 7.3. Problemy z nagrywaniem

| Problem | Możliwa przyczyna | Rozwiązanie |
|---------|-------------------|-------------|
| Nagranie jest puste (cisza) | SW2 nie przełączony | Upewnij się, że przełącznik jest w pozycji RECORD |
| Nagranie jest puste | Głowica nie dotyka taśmy | Dociśnij głowicę R/P do taśmy podczas nagrywania |
| Nagranie jest puste | Oscylator nie działa | Sprawdź czy oscylator generuje sygnał (sekcja 7.2) |
| Nagranie jest zniekształcone | Zbyt wysoki poziom | Zmniejsz R_REC (potencjometr poziomu nagrywania) |
| Nagranie jest zniekształcone | Zbyt mało bias | Zmniejsz R10, aby zwiększyć poziom bias |
| Nagranie brzmi głucho | Zbyt dużo bias | Zwiększ R10, aby zmniejszyć poziom bias |
| Poprzednie nagranie przebija | Erase head nie działa | Sprawdź podłączenie głowicy kasującej i R11 |
| Poprzednie nagranie przebija | Erase head źle ustawiona | Głowica kasująca musi dotykać taśmy i być PRZED głowicą R/P |

### 7.4. Problemy z przełączaniem Play/Record

| Problem | Możliwa przyczyna | Rozwiązanie |
|---------|-------------------|-------------|
| W trybie Play słychać szumy | Oscylator nie wyłączony | Sprawdź, czy SW2 odcina zasilanie oscylatora w pozycji PLAY |
| W trybie Record brak odtwarzania | To normalne! | Proste magnetofony nie pozwalają na jednoczesny zapis i odsłuch (brak monitoringu) |
| Po przełączeniu trzaski | Brak C10 | Kondensator sprzęgający C10 powinien blokować DC przy przełączaniu |

---

## 8. Co dalej — pomysły na rozbudowę

### 8.1. Monitoring nagrywania

Dodaj trzecią pozycję przełącznika (lub osobny obwód) umożliwiający odsłuch sygnału z preampa na głośniku/słuchawkach podczas nagrywania. Wymaga podłączenia wyjścia R_REC do wejścia LM386 równolegle z torem zapisu.

### 8.2. Wskaźnik poziomu nagrywania (VU-metr)

Dodaj diodę LED podłączoną do wyjścia preampa przez rezystor — jasność LED będzie proporcjonalna do poziomu sygnału. Bardziej zaawansowana wersja: układ LM3914/LM3915 z 10 diodami LED tworzącymi słupkowy wskaźnik poziomu.

### 8.3. Automatyczna regulacja poziomu (ALC)

Zastąp ręczny potencjometr R_REC układem automatycznej regulacji wzmocnienia (AGC/ALC). Prosty ALC można zbudować z diody, kondensatora i FET-a jako zmiennego rezystora — stabilizuje on poziom nagrywania niezależnie od głośności źródła.

### 8.4. Wejście liniowe (Line In)

Dodaj gniazdo jack 3,5 mm jako alternatywne wejście (zamiast mikrofonu). Wejście liniowe ma wyższy poziom (~200 mV–1V) niż mikrofon, więc wymaga tłumienia zamiast wzmocnienia — użyj dzielnika rezystorowego na wejściu.

### 8.5. Filtr szumów (DNR)

Dodaj prosty filtr dolnoprzepustowy (RC) na wyjściu toru odtwarzania, aby zredukować szum taśmy (hiss). Częstotliwość odcięcia ~10–12 kHz. Bardziej zaawansowane: dynamiczny filtr szumów sterowany poziomem sygnału.

---

## 9. Porównanie z projektem bazowym

| Cecha | SimpleCassettePlayer | SimpleCassettePlayerRecorder |
|-------|---------------------|-------------------------------|
| Odtwarzanie | ✅ | ✅ |
| Nagrywanie | ❌ | ✅ |
| Kasowanie taśmy | ❌ | ✅ (automatyczne) |
| Liczba tranzystorów | 0 | 2 (Q1, Q2) |
| Liczba układów scalonych | 1 (LM386) | 1 (LM386) |
| Całkowita liczba elementów | ~10 | ~30 |
| Poziom trudności | ⭐ Początkujący | ⭐⭐ Średniozaawansowany |
| Koszt | 40–80 zł | 80–150 zł |
| Czas montażu | 2–4 h | 4–6 h |
| Wymagana wiedza | Montaż na breadboardzie | + tranzystory, oscylatory LC |

---

## 10. Słowniczek pojęć (nowe)

Pełny słowniczek bazowy: [SimpleCassettePlayer — Słowniczek](../../SimplestCassettePlayer/docs/SimpleCassettePlayer.md#10-słowniczek-pojęć).

| Pojęcie | Wyjaśnienie |
|---------|-------------|
| **AC Bias (polaryzacja AC)** | Sygnał ultradźwiękowy dodawany do sygnału audio podczas nagrywania. Linearyzuje zapis magnetyczny, drastycznie poprawiając jakość. |
| **ALC (Automatic Level Control)** | Automatyczna regulacja poziomu nagrywania — utrzymuje stały poziom zapisu niezależnie od głośności źródła. |
| **Colpitts (oscylator)** | Typ oscylatora LC wykorzystujący dzielnik pojemnościowy (dwa kondensatory) jako sprzężenie zwrotne. Generuje stabilną sinusoidę. |
| **DPDT (Double Pole, Double Throw)** | Przełącznik z dwoma niezależnymi sekcjami, każda z trzema pinami (wspólny + dwie pozycje). Pozwala przełączać dwa obwody jednocześnie. |
| **Erase head (głowica kasująca)** | Głowica zasilana sygnałem ultradźwiękowym o wysokiej amplitudzie, demagnetyzująca taśmę przed nowym nagraniem. |
| **Mikrofon elektretowy** | Miniaturowy mikrofon pojemnościowy z wbudowanym FET-em. Wymaga zasilania polaryzacyjnego (1–10V) przez rezystor. Tani, dobra jakość. |
| **Obwód rezonansowy LC** | Układ cewki (L) i kondensatora (C) oscylujący na częstotliwości rezonansowej f = 1/(2π√(LC)). Podstawa oscylatorów. |
| **R/P head (głowica R/P)** | Record/Playback head — głowica uniwersalna służąca zarówno do zapisu, jak i odczytu. Standard w prostych magnetofonach. |
| **Wspólny emiter (CE)** | Konfiguracja tranzystora, w której emiter jest punktem wspólnym dla wejścia i wyjścia. Daje wysokie wzmocnienie napięciowe i odwraca fazę sygnału. |

---

## 11. Historia zmian dokumentu

| Wersja | Data | Opis zmian |
|--------|------|------------|
| 1.0 | 2026-02-06 | Pierwsza wersja dokumentacji. |

---

## 12. Licencja i autor

Projekt open-source, do dowolnego użytku. Stworzony z pomocą Claude (Anthropic) jako materiał edukacyjny.

Projekt bazowy: [SimpleCassettePlayer](../../SimplestCassettePlayer/docs/SimpleCassettePlayer.md).

> *„Nagrywanie na taśmę to dialog z fizyką — sygnał audio spotyka pole magnetyczne, a bias sprawia, że rozmowa jest czysta i wierna."*
