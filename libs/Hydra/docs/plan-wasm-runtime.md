# Plan — WASM jako drugi backend skryptowy

Stan wyjściowy: Hydra ma jeden interpreter (Lua 5.4 vendorowana w `src/lua/`),
wpięty w `ScriptModule` przez konkretny typ. Celem jest drugi backend —
WebAssembly — działający w tych samych gniazdach: budżet instrukcji, statyczna
pula, `BindingSet`, podmiana w locie.

Docelowo aplikacje Hydry pisane w Rust, C++, TinyGo i AssemblyScript, ładowane
dynamicznie i dostarczane przez sieć. Domknięciem jest Hydra Studio kompilujące
AssemblyScript do WASM **w przeglądarce** — bez toolchaina po stronie
użytkownika.

---

## Co już jest, a czego nie ma

Zweryfikowane w kodzie, nie z pamięci:

| Element | Stan | Plik |
|---|---|---|
| Budżet instrukcji + wywłaszczanie | ✅ jest | `ScriptModule.hpp` (`budget = 20000`), `Job::resume()` |
| Statyczna pula, zero `malloc` | ✅ jest | `Heap.hpp`, `HYDRA_SCRIPT_HEAP_BYTES` |
| Wybieralna powierzchnia bindingów | ✅ jest | `BindingSet` w `Bindings.hpp` |
| API bez typów Lua | ✅ jest | `Script.hpp` — `Ctx`, `Result<T>`, `lua_State` ukryty |
| Podmiana skryptu w locie | ✅ jest | `ScriptModule::reload(source)` |
| Degradacja po błędach | ✅ jest | `maxConsecutiveErrors = 5` → SysDegraded |
| Profil pamięciowy per platforma | ✅ jest | `Profile.hpp` (`HYDRA_SCRIPT_LARGE_PROFILE`) |
| **Interfejs silnika** | ✅ **zrobione (faza 0)** | `IScriptEngine.hpp`, `LuaEngine`, `ScriptModule` trzyma `IScriptEngine*` |
| **Dostarczanie skryptu przez sieć** | ✅ **zrobione (faza 1)** | `ScriptDelivery` — rozszerzenie `ext/script`, `ImageStore`, okres próbny |
| **Cokolwiek WASM** | ✅ **zrobione (faza 2)** | `src/wasm3/` v0.5.0 + `WasmEngine`, budżet wykonania, 18 testów |

Dwie rzeczy warto powiedzieć wprost, bo zmieniają zakres pracy:

1. **Bytecode over MQTT w Hydrze nie istnieje.** OTA (`OtaUpdater`,
   `IFirmwareStore`) wozi wyłącznie obrazy firmware — partycje ESP32, dwubank
   STM32. Skrypt dziś wchodzi tylko przez `reload()` z C++ albo komendę shella
   `lua reload`. Kanał dostarczania trzeba **napisać**, nie podpiąć.
2. **Zasięg migracji `ScriptModule` jest mały** — `ScriptModule`/`interp()`
   występuje poza `src/` tylko w `examples/lua-script/main.cpp`
   i `test/test_script.cpp` (20 wystąpień łącznie). Faza 0 jest tania.

---

## Faza 0 — `IScriptEngine`: wycięcie szwu ✅ ZROBIONE

**Bez tego nic dalej nie ma sensu.** Dziś Lua jest wpisana w `ScriptModule`
strukturalnie: pole `Interp interp_`, pole `Job job_`, a `Config` niesie
`Interp::Libs libs`. Drugi backend bez interfejsu oznacza `#if` w środku
modułu i dwie kopie logiki cyklu życia.

### Zadania

1. **`include/hydra/script/IScriptEngine.hpp`** — nowy nagłówek:
   ```cpp
   class IScriptEngine {
   public:
       enum class JobState : u8 { Idle, Running, Done, Failed };

       virtual const char* name() const = 0;          // "lua", "wasm3", "wamr"
       virtual Status open(void* pool, size_t bytes) = 0;
       virtual void   close() = 0;
       virtual bool   ready() const = 0;

       /** Wczytuje jednostkę wykonywalną: źródło Lua albo moduł WASM. */
       virtual Status load(const u8* bytes, size_t len, const char* name) = 0;

       virtual bool     hasFunction(const char* name) const = 0;
       virtual Status   callBegin(const char* fn) = 0;
       virtual JobState step(u32 budget) = 0;
       virtual void     cancel() = 0;
       virtual u32      steps() const = 0;

       virtual Heap::Stats memory() const = 0;
       virtual u32         collect() = 0;
       virtual const char* error() const = 0;
   };
   ```
   `Job` znika z API modułu — jego rola przechodzi do pary
   `callBegin()`/`step()`, bo mechanika wznawiania jest różna (Lua: `lua_State`
   wątku + kotwica w rejestrze; WASM: własny stan interpretera).

2. **`LuaEngine : IScriptEngine`** — cienki opakowywacz na istniejące `Interp`
   + `Job`. Zero zmian w `Interp.cpp`. `Interp::Libs` przenosi się do
   `LuaEngine::Config`.

3. **`ScriptModule`** — `Interp interp_` → `IScriptEngine* engine_`. Wskaźnik,
   nie własność: silnik tworzy aplikacja i wstawia w `Config`, zgodnie z regułą
   „brak alokacji po starcie" (`architecture.md`). `interp()` zostaje jako
   `LuaEngine`-specyficzny akcesor tam, gdzie ma sens, albo znika — do decyzji
   przy migracji dwóch plików.

4. **Flagi**: `HYDRA_SCRIPT_ENGINE_LUA` (domyślnie 1),
   `HYDRA_SCRIPT_ENGINE_WASM` (domyślnie 0). Wzorzec z `Config.hpp` — moduły
   wycinane w preprocesorze.

5. **`ScriptCommands`** — komenda `lua` uogólniona do `script`, `lua` zostaje
   aliasem. Podpolecenia `mem`/`stat`/`gc`/`reload` są backend-agnostyczne;
   wykonanie fragmentu (`lua print(x)`) ma sens tylko dla Lua — dla WASM zwraca
   `Err::NotSupported`.

### Decyzja do podjęcia w tej fazie: co zrobić z `Ctx`

To jest **najważniejsza decyzja całego planu**, bo `Bindings.cpp` jest napisany
w całości przeciwko `Ctx` i przepisanie go dwa razy to prawdziwy koszt.

`Ctx` jest stosowy (`argInt(1)`, `pushStr`, `setField`) — model Lua. WASM MVP
zna tylko `i32/i64/f32/f64` i pamięć liniową; **tabel nie ma**. Uderza to
w konkretne funkcje: `hydra.i2c.scan()` zwraca tabelę adresów, `hydra.i2c.read()`
tabelę bajtów.

Rekomendacja: **wariant A — ograniczona powierzchnia dla v1.**
- Skalary i napisy jako `(ptr, len)` w pamięci liniowej modułu.
- Funkcje zwracające tabele dostają dla WASM wariant z buforem wyjściowym:
  `i2c_read(addr, reg, len, outPtr) -> i32` (liczba bajtów albo kod błędu).
- Powstaje jawna, udokumentowana lista „czego WASM nie ma" zamiast cichego
  rozjazdu semantyki.

Wariant B (serializacja tabel do pamięci liniowej) daje pełną zgodność, ale
wprowadza własny format i alokację po stronie modułu — koszt nieproporcjonalny
do tego, ile skryptów naprawdę czyta tabele.

### Kryterium ukończenia — spełnione

`test/test_script.cpp` przechodzi **bez zmian w logice testów**: doszedł
`LuaEngine engine;` z `cfg.engine = &engine`, a cztery wywołania
`module.interp()` zmieniły adresata na `engine.interp()`. Asercje, źródła
skryptów i liczby przebiegów bez zmian. Poza tym:

- 632 przypadki, 3557 asercji — **0 błędów pod ASAN i pod TSAN**,
- `tools/check_includes.sh` czysto, w tym reguły „Lua widoczne tylko
  w `LuaInternal.hpp`" i „`lua_State` nie wycieka poza `src/`",
- `make examples` — wszystkie przykłady i szablon, w tym `lua-script`,
- `make docs` — fragmenty w `api.md` zgodne z API,
- **`ScriptModule.cpp` i `ShellCommands.cpp` kompilują się przy
  `-D HYDRA_SCRIPT_ENGINE_LUA=0`** — czyli moduł naprawdę nie zależy od Lua,
  a nie tylko „nie wygląda, jakby zależał".

### Co ostatecznie powstało

| Plik | Rola |
|---|---|
| `include/hydra/script/IScriptEngine.hpp` | umowa: cykl życia, bindingi, obraz, wywłaszczanie, diagnostyka |
| `include/hydra/script/LuaEngine.hpp`, `src/script/LuaEngine.cpp` | przekład umowy na `Interp` + `Job`; zero zmian w `Interp.cpp` |
| `ScriptModule` | `IScriptEngine* engine_`; `Config::source` to `const void*` + `sourceBytes` |
| `Config.hpp` | `HYDRA_SCRIPT_ENGINE_LUA` (1), `HYDRA_SCRIPT_ENGINE_WASM` (0) |
| `ScriptCommands` | komenda `script`, `lua` jako alias; działa na `IScriptEngine` |

Dwie decyzje podjęte w trakcie, poza tym, co zapowiadał plan:

1. **`IScriptEngine::eval()` nie jest czysto wirtualna** — domyślnie oddaje
   `Err::NotSupported`. Bez tego shell musiałby rzutować w dół, żeby wykonać
   `script print(x)`. Silnik binarny odpowiada „nie ta liga", a `mem`, `stat`,
   `gc` i `reload` działają u każdego.
2. **`Config::source` zmieniło typ na `const void*`** i dostało `sourceBytes`.
   Moduł WebAssembly to bajty ze znaczącymi zerami; `const char*` z `strlen()`
   ucinałby go na pierwszym z nich. Zero oznacza tekst zakończony zerem, więc
   dla Lua nic się nie zmienia.

---

## Faza 1 — kanał dostarczania modułu ✅ ZROBIONE

Świadomie **przed** WASM-em: kanał jest potrzebny również Lua, da się go
przetestować na hoście natychmiast, a odkłada dowiezienie WASM-a tylko o tyle,
o ile trwa. Bez niego WASM na urządzeniu jest ciekawostką — wgrywaną razem
z firmware, czyli niczym nowym.

### Zadania

1. **Ponowne użycie tego, co jest**:
   - `util/Sha256.hpp` — już używany przez OTA, ta sama weryfikacja.
   - `hal::IFileSystem` + `HostFileSystem` (zrobione 7 sierpnia) — moduł ląduje
     w pliku, a testy hostowe dostają prawdziwy backend za darmo.
   - Kształt `IFirmwareStore`: zapis fragmentami, przełączenie jako **osobna,
     jawna decyzja**. Ta separacja jest tu równie potrzebna.

2. **`script::ModuleStore`** — zapis blobu fragmentami + weryfikacja skrótu
   przed podmianą. Nie dziedziczy po `IFirmwareStore` (inne pojęcia: brak
   partycji, brak restartu), ale powtarza jego dyscyplinę.

3. **`script::Delivery`** — odbiór ramek `{id, seq, total, sha256, payload}`.
   Fragmentowanie jest konieczne: `HYDRA_MINIS_TX_BUFFER` to 512 bajtów,
   a moduł WASM waży kilka–kilkadziesiąt kB.
   - Wejście przez `minis::MinisModule` jako rozszerzenie (temat
     `ext/script/req`) — spójne z platformą MyCastle,
   - albo wprost przez `net::MqttClient` na dedykowanym temacie.
   Decyzja: **rozszerzenie Minis**, bo MyCastle już umie rozszerzenia i po
   stronie serwera nie trzeba nic nowego.

4. **Tryb próbny — analog OTA dla skryptu.** Po podmianie moduł jest na okresie
   próbnym: jeśli `maxConsecutiveErrors` zadziała w ciągu N sekund od swapu,
   `ScriptModule` wraca do poprzedniej wersji. Bez tego pierwszy moduł, który
   wywraca się w `setup()`, kończy wizytą z programatorem — dokładnie ten sam
   problem, który OTA rozwiązuje trybem próbnym, i dokładnie ten sam moment,
   w którym zaczyna dotyczyć skryptów: gdy przychodzą z sieci.

5. **Test** `test/test_script_delivery.cpp` na mocku łącza (`net/mock`,
   `minis` ma już `test_minis.cpp` jako wzór).

### Kryterium ukończenia — spełnione

Wszystko z listy działa i jest pokryte testem na atrapowym łączu
(`test/test_script_delivery.cpp`, 17 przypadków): skrót weryfikowany przed
przełączeniem, podmiana bez restartu, wersja psująca się w `loop()` wycofana
w okresie próbnym, wersja z błędem składni wycofana natychmiast.

### Co ostatecznie powstało

| Plik | Rola |
|---|---|
| `include/hydra/util/Base64.hpp`, `src/util/Base64.cpp` | RFC 4648; obraz binarny nie przejdzie przez JSON inaczej |
| `include/hydra/script/ImageStore.hpp`, `src/script/ImageStore.cpp` | dwa sloty, skrót w drodze, wycofanie; nie zna ani sieci, ani interpretera |
| `include/hydra/script/ScriptDelivery.hpp`, `src/script/ScriptDelivery.cpp` | rozszerzenie `ext/script`: `begin`/`chunk`/`commit`/`abort`/`status` + okres próbny |
| `ScriptModule::image()` | obraz obecnie wczytany — magazyn musi znać ten sam wskaźnik, żeby wycofanie trafiło dokładnie w niego |

Trzy decyzje podjęte w trakcie:

1. **Dwa sloty wystarczą, bo w okresie próbnym transfer jest odmawiany.**
   Naiwnie potrzeba trzech buforów (aktywny + poprzedni + przyjmowany), ale
   przyjmowanie trzeciej wersji, gdy druga jeszcze nie dowiodła, że wstaje,
   i tak byłoby błędem. `Err::Busy` jest tu funkcją, nie ograniczeniem.
2. **`ImageStore` nie dotyka systemu plików.** Plan zakładał `IFileSystem`
   jako magazyn, ale `ScriptModule` trzyma na obraz **wskaźnik** przez cały
   czas pracy — obraz musi zostać w RAM-ie niezależnie od tego, czy leży też
   na dysku. Trwałość między restartami to osobna sprawa i osobna decyzja;
   dopisanie jej do `ImageStore` teraz zaciemniłoby jedną odpowiedzialność
   dwiema. **Zostaje do zrobienia.**
3. **Nazwa `ImageStore`, nie `ModuleStore`** jak w pierwotnym planie — „moduł"
   w Hydrze znaczy `IModule`, a to jest magazyn obrazów, nie modułów.

### Dopisane później

- **Trwałość między restartami** — `script::ImageFile` (osobna klasa, nie pole
  w `ImageStore`: to inny cykl życia i inna droga awarii). Zapis **po
  potwierdzeniu**, nie przy `commit` — inaczej restart w środku okresu próbnego
  przywracałby właśnie tę wersję, przed którą się bronimy. Odczyt idzie prosto
  do wolnego slotu magazynu, bez drugiego bufora wielkości obrazu.
- **Podpis HMAC** — `ScriptDelivery::Config::hmacKey`. Brak podpisu przy
  ustawionym kluczu kończy się odmową **przed transferem**; zły podpis —
  po weryfikacji skrótu, a przed przełączeniem. Liczony nad zawartością slotu,
  nie nad tym, co zapowiedział nadawca; inaczej potwierdzałby deklarację,
  a nie dane.

### Czego nadal świadomie nie ma

- **Wznowienia przerwanego transferu.** Zerwane połączenie oznacza transfer
  od zera. Przy kilkudziesięciu kilobajtach to akceptowalne; przy AOT z fazy 3
  trzeba będzie wrócić do tematu.

---

## Faza 2 — wasm3 jako pierwszy backend WASM ✅ ZROBIONE

wasm3 przed WAMR-em, bo jest mniejszy i prostszy do domknięcia — a domknięcie
pierwszego backendu jest tym, co weryfikuje `IScriptEngine`. Reguła kciuka
zostaje: poniżej 256 KB RAM wasm3 (~64 KB), powyżej WAMR.

### Zadania

1. **Vendorowanie** — `src/wasm3/` + `VENDOR.md`, dokładnie jak `src/lua/`.
   `hydra_wasm3_conf.h` na wzór `hydra_lua_conf.h`: nagłówek wyłącznie
   preprocesorowy, wciągany i przez C, i przez C++ (ten sam powód, dla którego
   `Profile.hpp` nie ma ani jednej konstrukcji C++).

2. **Alokacja.** wasm3 woła `malloc`. Reguła Hydry mówi: po `App::begin()` nie
   wolno. Łagodzące: alokacje wasm3 padają w fazie ładowania modułu, czyli
   w inicjalizacji. Mimo to przekierować `m3_Malloc`/`m3_Free` na `Heap` —
   `Heap` nie zna ani Lua, ani Hydry i jest testowany w izolacji, więc nadaje
   się bez zmian. **Zadanie badawcze**: sprawdzić, czy wasm3 nie alokuje
   w trakcie wykonania (`m3_CallV` na ścieżce wywołania).

3. **Budżet instrukcji — główne ryzyko fazy.** wasm3 nie ma licznika
   instrukcji, a `ScriptModule` obiecuje wywłaszczanie. Warianty:
   - (a) paliwo wstrzykiwane przez instrumentację modułu (kompilator emituje
     cykliczne wywołania hosta) — przenosi obowiązek na toolchain, w Rust/TinyGo
     nie do wyegzekwowania,
   - (b) **łatka w pętli dyspozycji wasm3** dekrementująca licznik na operację —
     kilka procent narzutu, ale kontrakt jest po stronie Hydry,
   - (c) tylko kooperacyjnie, z adnotacją w dokumentacji.

   Rekomendacja: **(b)**, opisana w `VENDOR.md` jako łatka Hydry. To jest
   odpowiednik pułapki instrukcji, którą `Job` wykorzystuje w Lua — ta sama
   obietnica, ta sama warstwa.

4. **Powierzchnia importów** — moduł `"hydra"` z funkcjami mapowanymi
   z `BindingSet`: `gpio_write(i32,i32)->i32`, `adc_mv(i32)->i32`,
   `log_info(ptr,len)`, `event_emit(ptr,len,f32)`. Grupy wyłączone w `BindingSet`
   po prostu nie są rejestrowane — modułu ich używającego nie da się
   zlinkować, i to jest właściwy moment na błąd.

5. **Test `test/test_script_wasm.cpp`** — moduł `.wasm` skompilowany
   z wyprzedzeniem i **wpisany do testu jako tablica bajtów**. Testy nie mogą
   wymagać toolchaina WASM, tak samo jak dziś nie wymagają Arduino
   (`test/arduino_stub/`). Do `test/Makefile` dochodzi `WASM3_SRC` i wpis
   w `VPATH` — plik jest zbudowany na `wildcard`, więc to dwie linijki.

### Kryterium ukończenia

Moduł WASM migający diodą przez `hydra.gpio`, wywłaszczany po wyczerpaniu
budżetu (`loopPreemptions` rośnie), z nieskończoną pętlą **nie zawieszający
urządzenia** — czyli ta sama gwarancja, którą dziś daje Lua.

---

## Faza 3 — WAMR + AOT dla platform z zapasem ✅ WAMR ZROBIONY (AOT nie)

### Zadania

1. **Wybór silnika przez profil** — rozszerzenie `Profile.hpp`:
   `HYDRA_SCRIPT_WASM_ENGINE` = wasm3 na profilu małym (RP2040/RP2350, STM32),
   WAMR na `HYDRA_PLAT_ESP32` i `HYDRA_PLAT_HOST`. Ten sam kształt decyzji, co
   `HYDRA_SCRIPT_LARGE_PROFILE`, i ten sam powód: skrypt nie ma prawa zabrać
   pamięci pętli sterowania.

2. **Budżet** — WAMR ma `WASM_ENABLE_INSTRUCTION_METERING`, więc odpada łatka
   z fazy 2. Jeden powód mniej, żeby zaczynać od WAMR-a: to właśnie wasm3
   sprawdza, czy `IScriptEngine` znosi backend bez wsparcia w runtime.

3. **PSRAM na ESP32-S3** — wykrycie i przydział większej puli. Runtime WAMR to
   wtedy 50–100 KB przy pełnym interpreterze.

4. **AOT** — `wamrc` jest narzędziem hosta i wynik jest zależny od celu, więc
   należy do kontenera budującego, nie do urządzenia. Precedens:
   `MyCastle/docker/hydra-native` jako warstwa pochodna nad
   `ghcr.io/platform-minis/hydra-build` (obraz bazowy waży 13.8 GB — przebudowa
   nie wchodzi w grę, warstwa pochodna kosztuje kilkadziesiąt MB).
   Konsekwencja dla fazy 1: kanał dostarczania musi nieść **wariant modułu**
   (interpretowany `.wasm` vs AOT dla konkretnego celu) — pole w metadanych
   ramki, dodane od razu, żeby nie łamać formatu później.

### Zrobione

| Rzecz | Gdzie |
|---|---|
| Wybór silnika przez profil | `Profile.hpp` — `HYDRA_SCRIPT_WASM_ENGINE`, wasm3/WAMR |
| Pula WASM w profilu, nie w pliku silnika | `Profile.hpp` — `HYDRA_WASM_HEAP_BYTES` |
| **Wariant obrazu w kanale dostarczania** | `IScriptEngine::acceptsVariant()`, `ScriptDelivery` |
| Silnik ogłaszany w `status` | serwer wie, czy przysłać źródło, bajtkod czy AOT |

Wariant sprawdzany jest **przed** transferem: serwer nie wie, co stoi po drugiej
stronie, a kod AOT zbudowany dla Xtensy nie jest na Cortex-M „gorszy" — jest
niewykonywalny. To była zapowiedziana konsekwencja fazy 3 dla formatu ramki
i lepiej ją mieć teraz niż łamać protokół później.

### Czego nie zrobiono i dlaczego — WAMR nie mieści się w tym modelu osadzania

Zmierzone, nie oszacowane:

| | wasm3 | WAMR (minimalny interpreter) |
|---|---|---|
| Wiersze `.c` | ~14 500 | ~80 000 (+15 000 na AOT) |
| Pliki | 32 | 391 w `core/` |
| Warstwa platformy | brak — czyste C99 | osobny katalog na system, wybierany **przez CMake** |
| Konfiguracja | `#ifndef` w jednym nagłówku | `wamr.cmake` + kilkadziesiąt opcji |

Trzy przeszkody, każda osobno wystarczająca:

1. **PlatformIO kompiluje całe `src/`.** 80–95 tysięcy wierszy WAMR trafiałoby
   do budowy każdego projektu, także tego bez skryptów, a usuwał je dopiero
   konsolidator. Lua (~30 k) i wasm3 (~14,5 k) już tam są i to jest granica
   rozsądku.
2. **Warstwę platformy WAMR wybiera CMake, nie preprocesor.** Hydra celuje
   w ESP32 (shim `esp-idf`), RP2040/RP2350 i STM32 (żaden z dostarczonych
   shimów nie pasuje do budowy pod frameworkiem Arduino) oraz host
   (`darwin`/`linux`). wasm3 nie potrzebował żadnego, bo jest przenośnym C99.
3. **`vendor_lua.sh` zakłada płaską listę plików.** Przy WAMR to nie jest
   „dłuższa lista", tylko inny sposób budowania.

Dobra wiadomość: `WASM_ENABLE_INSTRUCTION_METERING` w WAMR **istnieje**, więc
łatka licznika — najtrudniejsza część fazy 2 — przy WAMR w ogóle nie byłaby
potrzebna.

### Wybrana droga: WAMR jako osobna biblioteka ✅

`libs/HydraWamr` — osobny pakiet PlatformIO, `libs/Hydra/tools/vendor_wamr.sh`
odtwarza osadzone drzewo. `WasmEngineWamr` implementuje `IScriptEngine`;
`test_script_wamr.cpp` uruchamia **te same moduły `.wasm`**, co testy wasm3.

**697 przypadków, 3910 asercji, 0 błędów pod ASAN i TSAN.**

Sześć rzeczy, które wyszły dopiero przy budowie i uruchomieniu — żadnej nie
dało się przewidzieć z dokumentacji:

1. **`BH_MALLOC`/`BH_FREE` muszą wskazywać na alokator WAMR-a.** Runtime
   sprawdza to makrem i przerywa komunikatem, który nie mówi, czego brakuje.
2. **Nagłówki AOT i `compilation` są potrzebne mimo wyłączonego AOT** —
   `wasm_memory.c` włącza `aot_runtime.h` bezwarunkowo. Klon z pełnym
   repozytorium tego nie pokazuje; wyszło przy budowie z drzewa osadzonego.
3. **Tablica importów nie może być `const`** — WAMR sortuje ją w miejscu
   (`qsort` w `wasm_native.c`). Stałe dane to natychmiastowa wywrotka.
4. **Sprzętowa kontrola granic musi być wyłączona** (`WASM_DISABLE_HW_BOUND_CHECK`).
   Używa sygnałów i alternatywnego stosu; przy drugim otwarciu runtime'u kładła
   się na „Failed to init signal alternate stack". Na MCU i tak nie ma MMU.
5. **`invokeNative_general.c` nie przenosi poprawnie argumentów f32** —
   `event_emit` wywracał się na arm64. Trzeba wariantu w assemblerze, a na
   macOS wariantu Mach-O (`invokeNative_osx_universal.s`) z definicją
   `BH_PLATFORM_DARWIN`, bo pliki ELF-owe mają dyrektywy, których tamtejszy
   asembler nie zna.
6. **WAMR toleruje nierozwiązany import** — ostrzega przy tworzeniu instancji
   i wywraca się dopiero przy wywołaniu, podczas gdy wasm3 odmawia od razu.
   Różnicę domknęliśmy jawnie: `load()` przechodzi po `wasm_runtime_get_import_type`
   i odrzuca moduł z niepodpiętym importem. Zachowanie ma być takie samo na obu
   silnikach, a nie „prawie takie samo".

Przy okazji powstał `src/script/WasmApi.hpp`: treść importów wspólna dla obu
runtime'ów. wasm3 i WAMR podają argumenty zupełnie inaczej, więc bez tego
byłyby dwie implementacje `gpio_write` i dwie okazje do rozjazdu — a rozjazd
tutaj oznacza moduł, który przy tym samym bajtkodzie robi co innego na dwóch
płytkach.

### Czego nadal nie ma

- **AOT.** Wymaga `wamrc` w kontenerze budującym i `WASM_ENABLE_AOT=1`
  w bibliotece. Protokół jest przygotowany — `variant: "aot:<cel>"` przechodzi
  przez kanał dostarczania i jest odrzucany przez silniki, które go nie wykonają.
- **Warstwa ESP-IDF jest nieprzetestowana.** Pliki są osadzone, konfiguracja
  napisana, ale bez toolchaina Espressifa nie dało się jej sprawdzić. Pierwsze
  `pio run` dla ESP32 należy traktować jako część pracy.

### Trzy drogi rozważane wcześniej

1. **WAMR jako biblioteka PlatformIO obok Hydry**, nie w `src/`. Wtedy CMake
   WAMR-a robi swoje, Hydra tylko linkuje i dokłada `WasmEngineWamr`.
   Koszt: druga zależność w `library.json`, ale zero wpływu na projekty,
   które WAMR-a nie chcą.
2. **AOT bez WAMR-a na urządzeniu** — `wamrc` w kontenerze budującym produkuje
   obraz, ale wykonuje go... nic, bo wasm3 AOT nie umie. Ta droga wymaga (1).
3. **Zostać przy wasm3.** Reguła kciuka mówi „poniżej 256 kB wasm3", a ESP32-S3
   z PSRAM to jedyna płytka w `modules.json`, która przekracza próg. Pytanie
   brzmi, czy zysk na szybkości uzasadnia drugi runtime w drzewie.

Rekomendacja: **(1), ale dopiero po fazie 4**. AssemblyScript w Studio daje
realną zmianę doświadczenia użytkownika; WAMR daje szybsze wykonanie tego
samego, a wąskim gardłem dziś nie jest szybkość interpretera.

### Kryterium ukończenia — otwarte

Ten sam moduł `.wasm` uruchomiony na wasm3 (RP2350) i WAMR (ESP32-S3), bez
zmiany ani jednego bajtu modułu. Nieosiągalne, dopóki nie zapadnie decyzja
o sposobie osadzenia WAMR.

---

## Faza 4 — AssemblyScript w Hydra Studio ⚠️ CZĘŚCIOWO (strona Hydry gotowa)

Tu jest cała dźwignia: `asc` to czysty JS/WASM, więc kompiluje się
**w przeglądarce**. Użytkownik nie instaluje nic — a dziś budowanie idzie przez
`docker/hydra.sh` i 13.8 GB obrazu.

### Zadania

1. **`packages/hydra-studio`** (MyCastle) — `asc` w bundlu web, kompilacja
   w Web Workerze, wynik prosto do kanału z fazy 1.

2. **Deklaracje `.d.ts` generowane z tego samego źródła, co tablica bindingów
   w C++.** To jedyny element planu, który **na pewno zgnije**, jeśli zrobić go
   ręcznie: dwie listy funkcji w dwóch językach w dwóch repozytoriach.
   Rozwiązanie: jedna deklaratywna lista (np. `bindings.def`), z niej
   generowane i rejestracje C++, i `hydra.d.ts`, i dokumentacja.
   Zrobić to **przy fazie 2**, kiedy lista importów powstaje po raz pierwszy —
   nie później.

3. **Pętla**: edycja `.ts` w Studio → `asc` → `.wasm` → dostarczenie → podmiana
   na urządzeniu → log z powrotem do Studio. Bez toolchaina, bez restartu, bez
   kabla.

4. **`docs/studio.md`** — nowa sekcja obok „Budowa dla maszyny, na której stoi
   przeglądarka".

### Zrobione — strona Hydry

| Rzecz | Gdzie |
|---|---|
| **Jedno źródło prawdy dla powierzchni importów** | `tools/wasm_bindings.def` |
| Generator trzech artefaktów | `tools/gen_bindings.py` |
| Tablice rejestracyjne C++ | `src/script/wasm_imports.inc` (generowane) |
| Deklaracje AssemblyScript | `templates/assemblyscript/assembly/hydra.ts` (generowane) |
| Tabela do dokumentacji | `docs/wasm-imports.md` (generowane) |
| Szablon modułu + `asconfig.json` + README | `templates/assemblyscript/` |
| Strażnik rozjazdu w bramce `make docs` | `gen_bindings.py --check` |

To jest ten element, o którym plan mówił, że **na pewno zgnije**, jeśli zrobić
go ręcznie — i który miał powstać już w fazie 2. Powstał teraz. Typy
AssemblyScript są **wyprowadzane z sygnatury wasm3**, a nie zapisywane osobno,
więc nie mogą się rozjechać nawet w obrębie pliku definicji.

Strażnik jest sprawdzony w obie strony: zmiana `.def` bez regeneracji zatrzymuje
`make docs` z listą nieaktualnych plików i kodem wyjścia 1.

`WasmEngine::linkBindings()` chodzi teraz pętlą po wygenerowanej tablicy grup,
zamiast mieć wiersz na grupę — dołożenie grupy do `.def` nie wymaga pamiętania
o dopisaniu jej w silniku.

### Zostało — strona MyCastle

Kompilacja w przeglądarce i wpięcie w interfejs. Wymaga pracy w drugim
repozytorium: `asc` w bundlu `packages/hydra-studio`, kompilacja w Web Workerze,
wynik prosto do kanału `ext/script` z fazy 1, log z urządzenia z powrotem
do Studia. Fundament po stronie Hydry jest gotowy — deklaracje, szablon
i protokół są na miejscu i opisane w `templates/assemblyscript/README.md`.

---

## Ryzyka, w kolejności prawdopodobieństwa

| Ryzyko | Faza | Ograniczenie |
|---|---|---|
| `Ctx` nie przenosi się na WASM (brak tabel) | 0 | Ograniczona powierzchnia v1 + bufory wyjściowe; lista różnic w dokumentacji |
| Brak licznika instrukcji w wasm3 | 2 | Łatka w pętli dyspozycji, opisana w `VENDOR.md` |
| wasm3 alokuje w trakcie wykonania | 2 | Zadanie badawcze przed vendorowaniem; przekierowanie na `Heap` |
| Rozjazd `.d.ts` z tablicą bindingów C++ | 4 | Generowanie z jednego źródła, wprowadzone w fazie 2 |
| Rozrost formatu ramki po dodaniu AOT | 1/3 | Pole wariantu w metadanych od pierwszej wersji |
| WAMR podnosi minimum RAM ponad budżet płytki | 3 | Wybór silnika przez `Profile.hpp`, nie przez preferencję |

---

## Kolejność i jej uzasadnienie

```
Faza 0  IScriptEngine        ── tanio (2 pliki poza src), odblokowuje wszystko
   │
Faza 1  kanał dostarczania   ── testowalny na hoście od razu, potrzebny też Lua
   │
Faza 2  wasm3                ── najmniejszy backend; sprawdza, czy szew z fazy 0 trzyma
   │
Faza 3  WAMR + AOT           ── skala i wydajność, gdy kontrakt jest już znany
   │
Faza 4  AssemblyScript       ── dźwignia produktowa na gotowym fundamencie
```

Każda faza ma wartość osobno: 0 porządkuje moduł skryptowy, 1 daje zdalną
aktualizację skryptów Lua **zanim** pojawi się WASM, 2 daje pierwsze
sandboxowane moduły, 3 wydajność, 4 doświadczenie użytkownika.
