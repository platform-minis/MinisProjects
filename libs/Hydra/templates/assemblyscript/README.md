# Moduł Hydry w AssemblyScript

Szablon modułu WebAssembly dla `script::WasmEngine`. Ten sam kontrakt, co przy
skrypcie Lua — opcjonalne `setup()` i opcjonalne `loop()` — tylko że typy są
sprawdzane przed wgraniem, a moduł jest piaskownicą: widzi wyłącznie to, co
zadeklarował w importach.

## Dlaczego akurat AssemblyScript

Moduł da się napisać w Rust, C++ albo TinyGo i wszystkie trzy zadziałają.
AssemblyScript jest tu wyróżniony z jednego powodu: **jego kompilator jest
w JavaScripcie**, więc chodzi w przeglądarce. Hydra Studio może zbudować
i wgrać logikę bez żadnego toolchaina po stronie użytkownika — podczas gdy wsad
na płytkę wymaga kontenera z PlatformIO ważącego 13,8 GB.

## Zawartość

    assembly/hydra.ts    deklaracje importów — GENEROWANE, nie edytować
    assembly/index.ts    twój moduł
    asconfig.json        konfiguracja kompilatora

`assembly/hydra.ts` powstaje z `tools/wasm_bindings.def` przez
`tools/gen_bindings.py`. Z tego samego pliku generują się tablice rejestracyjne
w C++, więc deklaracje po stronie modułu i implementacje po stronie urządzenia
nie mają jak się rozjechać. Pełna lista: [`docs/wasm-imports.md`](../../docs/wasm-imports.md).

## Budowa lokalna

    npm install --no-save assemblyscript
    npx asc assembly/index.ts --config asconfig.json --target release

Wynik: `build/module.wasm`. Tą samą drogą idzie Studio, tylko woła `asc`
w Web Workerze zamiast w powłoce.

## Wgranie na urządzenie

Moduł wchodzi kanałem z `ScriptDelivery` — rozszerzenie `ext/script` na tym
samym łączu, którym idzie telemetria:

    {"op":"begin",  "params":{"size":1234,"sha256":"…","variant":"wasm"}}
    {"op":"chunk",  "params":{"seq":0,"data":"<base64>"}}
    {"op":"commit", "params":{}}

`variant` musi brzmieć `"wasm"` — urządzenie z silnikiem wasm3 odrzuci
`"aot:*"` przed transferem, bo kodu skompilowanego z wyprzedzeniem nie wykona.
Sprawdź, co przyjmuje, komendą `status`; odpowiedź niesie pole `engine`.

Po `commit` moduł wchodzi na **okres próbny**. Jeśli w tym czasie `loop()`
zostanie wyłączona po serii błędów, urządzenie wraca do poprzedniej wersji samo.

## Trzy rzeczy, które trzeba wiedzieć, zanim się zacznie

**1. `loop()` ma się kończyć sama.** Dostaje budżet liczony w krawędziach
wstecznych pętli i wywołaniach funkcji. Po jego wyczerpaniu wykonanie zostaje
przerwane i **zaczyna się od nowa** w kolejnym przebiegu — inaczej niż w Lua,
gdzie jest wznawiane w miejscu. Stan trzymaj w zmiennych modułu, nie w pętli
czekającej na zdarzenie.

**2. Napisy to para (offset, długość).** Pamięć modułu jest jego własną
przestrzenią adresową, więc wskaźnik hosta nic by w niej nie znaczył. Wzorzec
jest w `index.ts` — `String.UTF8.encode` i `changetype<i32>`.

**3. `runtime: "stub"` w `asconfig.json` jest celowe.** Pełny odśmiecacz
AssemblyScriptu dokłada kilka kilobajtów do modułu i alokuje w trakcie pracy,
a moduł ma się mieścić w puli o rozmiarze ustalonym przy linkowaniu. Wariant
`stub` alokuje przyrostowo i nie zwalnia — co dla logiki sterującej, która nie
tworzy obiektów w pętli, jest dokładnie tym, czego trzeba. Kto potrzebuje
pełnego odśmiecacza, zmienia to pole i podnosi pulę.

## Czego moduł nie widzi

`i2c` i `event` nie są wystawione. Pierwsze oddaje dane o kształcie tabeli,
których WebAssembly nie zna; drugie wymaga wywołania zwrotnego do modułu.
Powody i stan: [`docs/wasm-imports.md`](../../docs/wasm-imports.md).

Grupa niewłączona po stronie urządzenia w `BindingSet` **nie zostaje
zlinkowana**, więc moduł jej żądający się nie wczyta — z jasnym błędem, zamiast
działać połowicznie.
