# Moduł `media` — potok multimedialny

Odpowiednik GStreamera dla mikrokontrolera: graf elementów, przez który płyną
dane. Audio (I2S, PWM, ADC, DAC), pojedyncze klatki obrazu, pliki, sieć,
filtry i efekty — i to samo w oknie SDL na buildzie natywnym.

## Etapy

| Etap | Zakres | Stan |
|---|---|---|
| **1. Rdzeń** | `Block`/`BlockPool`, `Pad`, `Element`, `Pipeline`, domeny, PTS, negocjacja formatów, elementy programowe | **gotowe** |
| **2. Audio sprzętowe** | `I2sSource`/`I2sSink`, `PwmAudioSink`, `DacAudioSink`, `AdcAudioSource` + HAL `II2s`/`IDac` | **gotowe** |
| **3. Pliki i sieć** | `FileSource`/`FileSink` (WAV i surowe), `NetSource`/`NetSink` | **gotowe** |
| **4. Natywne SDL** | `SdlAudioSink`/`SdlAudioSource`, `SdlVideoSink` | **gotowe** |
| **5. Wideo** | `CameraSource`, `Scaler`, `ColorConvert`, `jpegInfo()` + HAL `ICamera` | **gotowe** |

Etapy 2–5 to **wyłącznie elementy**. Rdzeń nie zmienił się ani razu — i po to
został zaprojektowany tak, jak został.

## Cztery decyzje, z których wynika reszta

### Własność bloku jest asynchroniczna

Element **nie musi** wyprodukować wyjścia w tym samym wywołaniu, w którym
dostał wejście.

To nie jest wygoda, tylko warunek istnienia etapów 2 i 5. Enkoder H.264
w ESP32-P4, kontroler I2S z DMA i kamera na MIPI-CSI działają tak samo:
przyjmują bufor i oddają go po przerwaniu kilkanaście milisekund później. Przy
interfejsie `process(Block&) → Block` jedynym sposobem na wpięcie ich byłoby
zablokowanie się na semaforze — czyli zamiana akceleratora na najdroższy
możliwy sposób czekania.

Element synchroniczny jest przypadkiem szczególnym: bierze z wejścia i oddaje
na wyjście w tym samym `process()`. Odwrotnie się nie da, więc kierunek
rozstrzygnięto raz, na początku.

### Blok jest uchwytem, nie buforem

```cpp
struct Block { u8* data; u32 capacity, length; u64 pts; PoolId pool; u16 slot; u8 flags; };
```

Klatka 1080p to 4 MB. Element, który ją kopiuje, robi to jako najdroższą
rzecz w całym potoku. Blok wędruje więc jak pałeczka w sztafecie: **jeden
właściciel**, kopia uchwytu nie zwiększa licznika. Jedynym wyjątkiem jest
rozgałęzienie (`Tee`), które woła `retain()` jawnie — i dlatego gałąź nie ma
prawa pisać po bloku.

### Domeny zamiast tasku na element

```
domena „capture"  (High,   2 ms)  I2sSource → Gain
domena „slow"     (Low,  100 ms)  MeterSink
```

ESP-ADF daje każdemu elementowi własny task: pięć elementów to pięć stosów po
4 kB, czyli 20 kB na samo czekanie. Tutaj tyle samo elementów mieści się
w dwóch domenach i dwóch stosach, a granice są tam, gdzie naprawdę zmienia się
priorytet. Wewnątrz domeny przekazanie bloku to wywołanie funkcji; między
domenami — kolejka SPSC bez zamka.

### Cisza jest błędem

Zgubiony blok, pusta pula i przepełniona kolejka trafiają na `EventBus` jako
`MediaFaultRaised`. Przerwa w dźwięku trwa cztery milisekundy i w logu ginie;
licznik na wykresie pokazuje, że urządzenie nie wyrabia. Ten sam wybór, co
przy spóźnieniach tasków.

## Format jest strukturą, nie napisem

GStreamer negocjuje po tekstowych „capsach". Tutaj `MediaFormat` to POD
porównywany polami: parsowanie `audio/x-raw,rate=44100` na układzie bez sterty
nie miałoby jak działać, a `switch` po wyliczeniu wyłapuje przypadek,
o którym ktoś zapomniał.

`prepare()` przechodzi elementy w kolejności rejestracji i pyta każdy, co
wyjdzie, skoro wchodzi to a to. **Kolejność rejestracji jest kierunkiem
przepływu** — połączenie „pod prąd" jest odrzucane przy budowie, bo dawałoby
okres opóźnienia na każdym obiegu zamiast błędu.

## Polityka przepełnienia należy do miejsca w grafie

| Polityka | Kiedy | Dlaczego |
|---|---|---|
| `DropOldest` | podgląd z kamery | świeża klatka warta więcej niż ta, której nikt nie zobaczy |
| `DropNewest` | zapis do pliku | kolejność ma znaczenie; dziura na końcu lepsza niż w środku |
| `Reject` | źródło ma wstrzymać produkcję | nic nie ginie, ale ktoś musi czekać |

`DropOldest` wyjmuje z ogona po stronie producenta, więc przez granicę domen
jest wyścigiem — `Pipeline::link()` zamienia ją tam na `DropNewest` i mówi
o tym w logu.

## Etap 2: peryferia audio

Peryferia dzielą się na dwie rodziny i różnią się wszystkim.

### Blokowe — I2S

Kontroler bierze bufor i oddaje go po przerwaniu. To jest ten przypadek, dla
którego etap 1 dostał asynchroniczne przekazywanie własności; gdyby `Element`
musiał oddać wynik w tym samym wywołaniu, I2S dałoby się wpiąć wyłącznie przez
blokadę na semaforze — czyli task audio, który czeka zamiast liczyć.

Nowy peryferiał w HAL-u, `hal::II2s`, jest jedynym bez `read()`/`write()`:

```
submit(buffer)     → bufor należy do sterownika
…DMA…
reclaim(buffer, n) → wraca do wołającego
```

**Backend kopiujący jest dopuszczalny.** ESP-IDF nie oddaje własności buforów
DMA — daje `i2s_write()`, które przepisuje do własnego pierścienia. Taki
backend przyjmuje bufor w `submit()` i oddaje go, gdy przepisanie się skończy;
warstwa wyżej nigdy nie zakłada, *kiedy* bufor wróci, tylko że wróci.
Przepisanie bywa częściowe, więc wpis pamięta postęp i dosyła resztę.

### Próbkowe — PWM, DAC, ADC

Nie ma DMA i nie ma buforów: jest jeden rejestr, do którego trzeba trafiać
w rytm próbkowania. Element wystawia więc tyle próbek, ile **minęło czasu** —
budżet liczy z `nowUs`, nie z liczby wywołań:

```cpp
budget  = (nowUs - lastUs + carry) * sampleRate / 1e6;
carry   = reszta poniżej jednej próbki;   // bez tego wysokość dźwięku spada
```

Drganie okresu taska rozjeżdża wtedy chwilowe tempo, a nie długoterminową
wysokość dźwięku. Brak danych to `Underrun` i **przepadnięcie** reszty budżetu
— nadrabianie zaległości oznaczałoby dźwięk odtwarzany szybciej po każdej
przerwie, czyli objaw gorszy od samej przerwy.

Górna granica wynika z okresu domeny. Powyżej kilku kiloherców trzeba timera
sprzętowego, a nie krótszego okresu — i to jest ograniczenie sprzętu, nie
tego kodu. PWM jest więc rozwiązaniem dla komunikatów głosowych, nie dla muzyki:
nośna musi leżeć co najmniej dziesięciokrotnie wyżej niż próbkowanie, co przy
8 kHz daje 80 kHz i rozdzielczość spadającą do dziewięciu bitów.

### Dwie rzeczy, które wyszły dopiero w testach

`PwmAudioSink::onStop()` ustawia wypełnienie na połowę i **nie zwalnia
kanału**: zwolnienie zatrzymuje nośną, pin idzie do zera i robi dokładnie ten
trzask, którego unikaliśmy. Pin oddaje aplikacja, gdy naprawdę go potrzebuje.

Wartownik „czas nieustawiony" nie może być zerem. Potok wystartowany w chwili
zerowej zegara — a tak wygląda każdy test i każdy start po resecie — nigdy nie
wychodził z fazy pierwszego wywołania i nie wystawiał ani jednej próbki.

## Etap 3: pliki i sieć

### Plik jest wolny i nieprzewidywalny

Karta SD potrafi zamilknąć na sto milisekund, gdy kontroler kasuje sektor.
Element czytający w pętli aż do końca pliku zabiera wtedy cały czas domeny
i wywraca strumień audio, który płynie obok. Oba elementy plikowe robią więc
w jednym `process()` **najwyżej `blocksPerStep` bloków** i naturalnie należą do
domeny o niskim priorytecie, oddzielonej kolejką od tej z przetwornikiem.

### Nagłówek WAV jest łatany na bieżąco

Rozmiary w RIFF znane są dopiero po zamknięciu pliku. Nagranie przerwane
zanikiem zasilania miałoby tam zera — czyli plik nie do odtworzenia mimo
poprawnych danych. `FileSink` uzupełnia je co `patchEvery` bloków, więc awaria
kosztuje ogon, a nie całość.

Czytnik przechodzi **listę fragmentów**, zamiast zakładać, że `fmt ` i `data`
leżą po sobie. Nie leżą: edytory wstawiają między nie `LIST` z nazwą programu,
a rekordery `fact`. Kod ze stałym przesunięciem 44 bajtów działa z plikami,
które sam wyprodukował, i z niczym więcej.

WAV skompresowany (ADPCM, μ-law) ma ten sam nagłówek i inny ładunek —
odrzucamy go, bo odtworzony jako PCM daje szum o pełnej głośności, czyli
najgorszy możliwy sposób poinformowania użytkownika, że format jest
nieobsługiwany.

Format odtwarzania pochodzi **z pliku**, nie z konfiguracji. Plik 44,1 kHz
odtworzony jako 16 kHz brzmi jak nagranie zwolnione i wygląda na usterkę
sprzętu.

### Sieć: TCP i szesnaście bajtów nagłówka

```
0   'H' 'M'     magia
2   wersja | flagi
4   długość ładunku (u32 LE)
8   znacznik czasu w µs (u64 LE)
```

Bez magii odbiornik wpięty w połowie bloku czytałby długość ze środka próbek
i czekał na dwa gigabajty. Z magią wraca do siebie po jednym bloku.

**Dlaczego TCP, a nie UDP.** Dla dźwięku na żywo UDP byłby lepszy — strata
pakietu kosztuje jedno kliknięcie, retransmisja kaskadę opóźnień. Ale warstwa
transportowa Hydry ma dziś tylko `net::IClient`, czyli strumień TCP; dołożenie
UDP to nowy interfejs HAL i backendy na trzech platformach, czyli osobna
decyzja, a nie efekt uboczny tego etapu. Dopóki jej nie ma, `NetSink` nadaje
się do zapisu zdalnego, podglądu i przesyłania nagrań — nie do rozmowy.

Zapchane gniazdo powoduje porzucenie **całego** bloku. Wysłanie połowy
rozsypałoby odbiornikowi ramkowanie na resztę połączenia; brak całego to jedna
przerwa. Brak połączenia nie zatrzymuje potoku — dźwięk ma dalej grać lokalnie.

## Etap 4: karta dźwiękowa i okno hosta

### Kolejka SDL, nie funkcja zwrotna

SDL daje oba warianty. Funkcja zwrotna jest wołana z **wątku audio SDL-a** —
wątku, którego moduł `media` nie tworzy i nie kontroluje. Trzeba by dołożyć
bezzamkowy pierścień między nią a potokiem i moduł, który dotąd nie miał ani
jednego wątku, miałby w środku wątek obcy. Kolejka pasuje do modelu push bez
żadnej dodatkowej maszynerii: element dokłada, SDL wybiera.

### Opóźnienie trzeba ograniczyć jawnie

Kolejka SDL nie ma górnego rozmiaru. Potok czytający plik szybciej, niż karta
go odtwarza — a tak jest zawsze — wepchnąłby w nią całe nagranie w ułamku
sekundy i pauza reagowałaby po kilkunastu sekundach.

```cpp
u8 queueBudget(u32 queued, u32 highBytes, u32 blockBytes);
```

Cała arytmetyka opóźnienia jest w jednej funkcji, **niezależnej od SDL** —
i dlatego jedyna rzecz w tym etapie, którą da się sprawdzić bez karty
dźwiękowej, jest zarazem tą, która decyduje o odczuciu użytkownika.

Odtwarzanie rusza dopiero po uzbieraniu połowy zapasu. Start od pierwszego
bloku daje przerwę w drugiej sekundzie, bo karta wybiera szybciej, niż potok
zdąży się rozkręcić.

### Podgląd kopiuje klatkę

`gfx::SdlDisplay` rysuje z bufora, który dostał przy `begin()`, a klatka
przychodzi w bloku z puli. Zero-copy wymagałoby przepinania bufora powierzchni
pod każdą klatkę, czyli API w `gfx`, którego nikt poza tym miejscem by nie
użył. Kopia 320×240 na PC jest darmowa; przy 1080p na sprzęcie docelowym ta
decyzja wymaga rewizji.

`SdlVideoSink` rysuje **tylko ostatnią** klatkę z kolejki — monitor odświeża
się 60 razy na sekundę i zaległych i tak nikt nie zobaczy.

JPEG i YUV są odrzucane przy `prepare()` z komunikatem. Okno z szumem wygląda
na usterkę kamery i kosztuje godzinę szukania w niewłaściwym miejscu; dekoder
to etap 5.

### Co wyszło z testów

Sprawdzenie formatu próbki siedziało tylko w gałęzi z SDL, a wariant bez niego
przyjmował wszystko — czyli `prepare()` dawał inny wynik w zależności od tego,
czy maszyna ma `libsdl2-dev`. Objawem na maszynie z SDL byłby potok, który
nagle odmawia startu **po** instalacji biblioteki. Negocjacja i przygotowanie
są teraz wspólne dla obu wariantów; różnią się wyłącznie `onStart`, `onStop`
i `process`.

## Etap 5: obraz

### Kamera jest właścicielem buforów — odwrotnie niż I2S

```
I2S:     my dajemy bufor   → sterownik go wypełnia → oddaje nam
kamera:  sterownik ma bufory → pożycza nam klatkę  → my ją oddajemy
```

Tak działa `esp_camera_fb_get()` i tak samo v4l2 w trybie MMAP: sterownik
alokuje pamięć DMA przy starcie, bo tylko on wie, gdzie musi leżeć. Interfejs,
w którym to my podajemy bufor, wymuszałby na backendzie kopiowanie.

Konsekwencja: **klatkę trzeba oddać, i to szybko**. Kamera ma zwykle dwa
bufory; zatrzymanie jednego na czas przejścia przez potok zatrzymuje strumień.
`CameraSource` kopiuje więc treść do bloku puli i zwalnia klatkę natychmiast —
także wtedy, gdy pula jest pusta i blok się nie udał. Bez tego brak miejsca
w puli zatrzymywałby kamerę **na stałe**: sensor nie ma gdzie pisać, a pula
i tak się nie zwolni.

Kopia jest świadomym kosztem: QVGA w RGB565 to 150 kB na klatkę, przy 30 kl./s
4,5 MB/s. Dla JPEG-a to zwykle 15 kB i koszt znika. Przy 1080p trzeba by puli
z blokami w pamięci sterownika — a to zmiana w rdzeniu, nie w tym elemencie.

### JPEG jedzie bez rekompresji

Nie ma dekodera i nie będzie: dekompresja 640×480 na Cortex-M4 zajmuje kilkaset
milisekund, czyli tyle, co kilkanaście klatek. Moduł kamery kompresuje
sprzętowo, a potok przepuszcza bajty do pliku albo do sieci, nie zaglądając do
środka. Potrzebne są z tego tylko wymiary — czyta je `jpegInfo()` z nagłówka
SOF, pomijając `0xC4`, `0xC8` i `0xCC`, które wpadają w ten sam zakres
znaczników, a ramki nie opisują.

`Scaler` odmawia dla JPEG-a przy negocjacji: nie ma pikseli, do których dałoby
się sięgnąć.

### Skalowanie najbliższym sąsiadem, bez interpolacji

Dwuliniowa kosztuje cztery odczyty i trzy mnożenia na piksel — przy QVGA
i 30 kl./s to dwa i pół miliona mnożeń na sekundę na układzie bez FPU.
Najbliższy sąsiad to jeden odczyt i jedno przesunięcie, a przy zmniejszaniu do
podglądu różnicy nie widać. Współczynniki liczone raz, w Q16: dzielenie na
piksel byłoby najdroższą operacją w całym elemencie.

### Konwersja barw — trzy przejścia, wszystkie na całkowitych

| Z | Na | Po co |
|---|---|---|
| YUV422 | RGB565 | sensor daje YUYV, panel chce RGB565 |
| YUV422 | GRAY8 | **darmowe** — składowa Y *jest* jasnością |
| RGB565 | GRAY8 | wykrywanie ruchu i kody kreskowe nie potrzebują barw |

Drugi wiersz jest jedynym powodem, dla którego warto trzymać sensor w YUV, gdy
liczy się tylko luminancja. Współczynniki BT.601 przemnożone przez 256 —
wersja zmiennoprzecinkowa oznaczałaby emulację przy każdym z 76 800 pikseli.

## Przykład

[`examples/media-tone`](../examples/media-tone/) — generator tonu →
wzmocnienie → miernik, w dwóch domenach, bez sprzętu.

[`examples/media-i2s`](../examples/media-i2s/) — mikrofon → wzmocnienie →
głośnik po I2S. Wejście i wyjście chodzą w tym samym rytmie próbek, więc są
w jednej domenie: domeny rozdziela się tam, gdzie różni się priorytet, a nie
tam, gdzie różni się kierunek.

[`examples/media-record`](../examples/media-record/) — mikrofon → rozgałęzienie
→ {plik na karcie, strumień po sieci}. Trzy domeny: przetwornik, karta i sieć
chodzą we własnym rytmie.

[`examples/media-native`](../examples/media-native/) — generator → wzmocnienie
→ karta dźwiękowa hosta, budowany celem `native`. Bez SDL2 program się
uruchamia i mówi, czego brakuje, zamiast grać ciszą.

```cpp
gPipeline.addPool(ByteSpan{storage, sizeof(storage)}, 128, 6);
gPipeline.add(gTone,  kCapture);
gPipeline.add(gGain,  kCapture);
gPipeline.add(gMeter, kSlow);
gPipeline.link(gTone, gGain);
gPipeline.link(gGain, gMeter);
gPipeline.prepare();
```

Pule dostarcza aplikacja — framework nie ma własnej pamięci. Rozmiar bloku
przekłada się wprost na opóźnienie, liczba bloków na zapas przy obciążeniu;
to decyzje projektu urządzenia, nie biblioteki.

## Rozmiary statyczne

| Makro | Domyślnie | Co ogranicza |
|---|---|---|
| `HYDRA_MEDIA_MAX_ELEMENTS` | 12 | elementy w potoku |
| `HYDRA_MEDIA_MAX_PADS` | 4 | pady jednego elementu |
| `HYDRA_MEDIA_MAX_POOLS` | 4 | pule buforów |
| `HYDRA_MEDIA_MAX_DOMAINS` | 4 | domeny czasowe |
| `HYDRA_MEDIA_PAD_DEPTH` | 8 | głębokość kolejki padu |
| `HYDRA_MEDIA_I2S_INFLIGHT` | 4 | bufory oddane naraz sterownikowi I2S |

## Czego nie ma i nie będzie

Rejestru wtyczek i ładowania w runtime — graf jest zamrażany, bo po
`App::begin()` nic się nie alokuje. Przewijania i segmentów — na MCU źródłem
czasu jest I2S albo VSYNC kamery i koniec. Negocjacji capsów po stringach.
Dowolnej topologii — graf jest skierowany i acykliczny, a kolejność
rejestracji rozstrzyga.
