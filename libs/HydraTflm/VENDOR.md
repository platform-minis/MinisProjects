# HydraTflm — osadzony TensorFlow Lite Micro

Drzewo wygenerowane z [tflite-micro](https://github.com/tensorflow/tflite-micro)
przez `libs/Hydra/tools/vendor_tflm.sh`. **Nie edytuj ręcznie** — zmiany
przepadną przy następnym odtworzeniu.

    tools/vendor_tflm.sh              # odtworzenie z gałęzi main
    tools/vendor_tflm.sh --ref v1.3   # z konkretnego znacznika

## Dlaczego osobna biblioteka, a nie `libs/Hydra/src/`

Ta sama ściana, o którą rozbił się WAMR: **PlatformIO kompiluje całe `src/`**.
202 pliki TFLM trafiałyby do budowy każdego projektu, także tego bez modelu,
a usuwałby je dopiero konsolidator. Lua (~30 k wierszy) i wasm3 (~14,5 k) już
tam są i to jest granica rozsądku.

Konsekwencja: projekt, który chce inferencji, dokłada `HydraTflm` do
`lib_deps` i definiuje `HYDRA_ENABLE_INFER_TFLM=1`. Projekt bez modelu nie
płaci nic.

## Co jest w drzewie

| Katalog | Zawartość |
|---|---|
| `tensorflow/lite/micro/` | interpreter, alokator arenowy, planiści pamięci |
| `tensorflow/lite/micro/kernels/` | jądra referencyjne wszystkich operatorów |
| `tensorflow/lite/kernels/internal/` | wspólna arytmetyka jąder |
| `third_party/flatbuffers/` | odczyt formatu `.tflite` (tylko nagłówki) |
| `third_party/gemmlowp/`, `ruy/` | arytmetyka stałoprzecinkowa |
| `third_party/kissfft/`, `signal/` | FFT i filtry — pod frontend MFCC |

Nie ma tu CMSIS-NN ani innych jąder optymalizowanych pod konkretny rdzeń.
To świadome: wchodzą jako osobny wariant wtedy, gdy pomiar pokaże, że są
potrzebne — a nie zanim pierwszy model ruszy.

## Pułapki, które wyszły przy osadzaniu

**`TF_LITE_STATIC_MEMORY` zmienia układ pól `TfLiteTensor`, a nie tylko
zachowanie.** Bez tej flagi struktura ma dodatkowe pola i `type`/`dims` leżą
pod innymi offsetami. Jednostka Hydry włączająca nagłówki TFLM musi więc
widzieć dokładnie tę samą definicję, co biblioteka. Objaw niezgodności jest
mylący: model wczytuje się bez błędu, `inputs_size()` zwraca poprawną liczbę,
ale każdy tensor wygląda na pusty (`type = 0`, `dims = nullptr`) — co prowadzi
prosto do szukania błędu w modelu albo w konwerterze.

**Pliki o tych samych nazwach w różnych katalogach.** `common.cpp`,
`window.cpp`, `kernel_util.cpp`, `energy.cpp` występują po dwa i trzy razy.
Reguła Makefile’a używająca `notdir` sklei je w jeden obiekt, a build wygląda
na udany aż do konsolidacji. Obiekty muszą zachowywać strukturę katalogów.

**Lista operatorów należy do aplikacji.** `MicroMutableOpResolver<N>` wymaga
podania ich w czasie kompilacji i to on decyduje o rozmiarze wsadu. Model
sinusa potrzebuje jednego (`FULLY_CONNECTED`), rozpoznawanie słowa kluczowego
czterech, `AllOpsResolver` wciąga wszystkie sto i sam waży więcej niż typowy
model. Dlatego `TflmEngine::setOpResolver()` przyjmuje go z zewnątrz.

## Generator, nie ręczna lista

`create_tflm_tree.py` jest utrzymywany przez zespół TFLM właśnie po to, żeby
integracje nie musiały śledzić listy plików. Ręcznie spisana lista rozjechałaby
się przy pierwszej zmianie układu katalogów — a przy 200 plikach nikt tego nie
zauważy, dopóki nie zabraknie symbolu.

Generator wymaga GNU Make ≥ 3.82 (macOS ma 3.81 i tego nie podniesie),
`numpy`, `pillow`, `curl`, `patch` i `unzip`. Skrypt uruchamia go dlatego
w kontenerze `mycastle-hydra-wasm:local`, który jest już w projekcie.
