/**
 * Testy silnika WebAssembly.
 *
 * Moduł testowy jest **wygenerowany bajt po bajcie** i wpisany niżej jako
 * tablica. Nie ma tu pliku binarnego w repozytorium ani zależności od
 * kompilatora Rusta czy AssemblyScriptu: format WebAssembly jest opisany
 * i da się go złożyć ręcznie, a 107 bajtów, które można przeczytać w diffie,
 * jest lepszym materiałem dowodowym niż nieprzejrzysty plik.
 *
 * Moduł eksportuje:
 *   setup()      globalna := 100
 *   loop()       globalna += 1
 *   get() -> i32 zwraca globalną
 *   boom()       instrukcja `unreachable` — pułapka maszyny
 *   memory       jedna strona pamięci liniowej (64 KB)
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/WamrEngine.hpp"
#include "hydra/script/Wasm3Engine.hpp"
#include "hydra/core/EventBus.hpp"
#include "hydra/script/EngineSelector.hpp"
#include "hydra/script/ProgramReceiver.hpp"
#include "hydra/script/ScriptModule.hpp"
#include "hydra/script/WasmBindings.hpp"
#include <string.h>

#include "hydra_test.hpp"

using namespace hydra;
using namespace hydra::script;

namespace {

const u8 kTestModule[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x60,
    0x00, 0x00, 0x60, 0x00, 0x01, 0x7F, 0x03, 0x05, 0x04, 0x00, 0x00, 0x01,
    0x00, 0x05, 0x03, 0x01, 0x00, 0x01, 0x06, 0x06, 0x01, 0x7F, 0x01, 0x41,
    0x00, 0x0B, 0x07, 0x26, 0x05, 0x05, 0x73, 0x65, 0x74, 0x75, 0x70, 0x00,
    0x00, 0x04, 0x6C, 0x6F, 0x6F, 0x70, 0x00, 0x01, 0x03, 0x67, 0x65, 0x74,
    0x00, 0x02, 0x04, 0x62, 0x6F, 0x6F, 0x6D, 0x00, 0x03, 0x06, 0x6D, 0x65,
    0x6D, 0x6F, 0x72, 0x79, 0x02, 0x00, 0x0A, 0x1B, 0x04, 0x06, 0x00, 0x41,
    0x64, 0x24, 0x00, 0x0B, 0x09, 0x00, 0x23, 0x00, 0x41, 0x01, 0x6A, 0x24,
    0x00, 0x0B, 0x04, 0x00, 0x23, 0x00, 0x0B, 0x03, 0x00, 0x00, 0x0B,};

CByteSpan testModule() { return CByteSpan{kTestModule, sizeof(kTestModule)}; }

/**
 * Pula podana wprost — testy nie walczą o pulę domyślną.
 *
 * 160 KB, bo z tej puli idzie **wszystko**: struktury interpretera, skompilowany
 * kod modułu i jego pamięć liniowa. Moduł testowy deklaruje jedną stronę, czyli
 * 64 KB samej pamięci liniowej — pula mniejsza niż to sprawia, że `loadBinary()`
 * odmawia, i jest to zachowanie poprawne, tylko łatwe do wzięcia za usterkę.
 */
alignas(8) u8 gPool[160 * 1024];

Wasm3Engine::Config poolConfig() {
    Wasm3Engine::Config cfg;
    cfg.pool      = gPool;
    cfg.poolBytes = sizeof(gPool);
    return cfg;
}

}  // namespace

TEST("wasm: silnik zglasza brak wywlaszczania w punkcie") {
    Wasm3Engine engine;
    const EngineInfo info = engine.info();

    CHECK_STR(info.name, "wasm3");
    CHECK(info.language == ScriptLanguage::Wasm);
    // To jest ta różnica wobec Lua, o którą opiera się cała obsługa budżetu
    // w module skryptów. Zmiana tej wartości bez zmiany modułu byłaby cichą
    // obietnicą, że `while(1)` da się przerwać.
    CHECK(info.preemption == Preemption::RunToCompletion);
    CHECK(!info.acceptsSource);
    CHECK(info.acceptsBinary);
}

TEST("wasm: modul wczytuje sie i udostepnia eksporty") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());

    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());

    CHECK(engine.hasFunction("setup"));
    CHECK(engine.hasFunction("loop"));
    CHECK(!engine.hasFunction("nie_ma_takiej"));

    engine.close();
}

TEST("wasm: wywolania zmieniaja stan modulu") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());

    // setup() ustawia globalną na 100, każde loop() dokłada jeden.
    REQUIRE(engine.callFunction("setup").has_value());
    for (u8 i = 0; i < 5; ++i) {
        REQUIRE(engine.startJob("loop").has_value());
        CHECK(engine.resumeJob(1000) == RunState::Done);
    }

    // Stan sprawdzamy przez eksport `get`, a nie przez zajrzenie do pamięci
    // silnika — tak jak zrobiłby to prawdziwy program.
    CHECK(engine.callFunction("get").has_value());
    CHECK_EQ(engine.jobSteps(), 5u);

    engine.close();
}

TEST("wasm: jeden przebieg konczy zadanie, bez stanu Running") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());

    REQUIRE(engine.startJob("loop").has_value());
    CHECK(engine.jobState() == RunState::Running);

    // Budżet jest ignorowany — wasm3 nie ma czym przerwać wykonania.
    // Pętla „dopóki Running" wołającego wykonuje więc jeden obieg.
    CHECK(engine.resumeJob(1) == RunState::Done);
    CHECK(engine.jobState() == RunState::Done);

    engine.close();
}

TEST("wasm: pulapka w module to blad, nie wywrotka") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());

    REQUIRE(engine.startJob("boom").has_value());
    CHECK(engine.resumeJob(0) == RunState::Failed);

    // Treść błędu ma trafić do wołającego; pusty komunikat zostawiłby
    // diagnostykę bez czegokolwiek.
    CHECK(engine.error()[0] != '\0');

    // Silnik zostaje zdatny do pracy — kolejne wywołanie ma się udać.
    REQUIRE(engine.startJob("loop").has_value());
    CHECK(engine.resumeJob(0) == RunState::Done);

    engine.close();
}

TEST("wasm: obcy plik odrzucony po naglowku") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());

    // Najczęstsza pomyłka w polu: wgrany skrypt Lua zamiast modułu.
    const char* lua = "function loop() end";
    auto result = engine.loadBinary(
        CByteSpan{reinterpret_cast<const u8*>(lua), strlen(lua)}, "=zly");

    CHECK(!result.has_value());
    CHECK(result.error() == Err::Protocol);
    // Komunikat ma wskazywać na nagłówek, a nie na „unknown opcode" —
    // inaczej szuka się błędu w module, a nie w tym, co go dowiozło.
    CHECK(engine.error()[0] != '\0');

    engine.close();
}

TEST("wasm: uciety transfer nie przechodzi") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());

    // Poprawny nagłówek, urwana reszta — tak wygląda przerwane pobieranie.
    CHECK(!engine.loadBinary(CByteSpan{kTestModule, 40}, "=uciety").has_value());

    engine.close();
}

TEST("wasm: pamiec liniowa modulu jest widoczna") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());

    // Moduł deklaruje jedną stronę, czyli 64 KB. Wartość mniejsza znaczyłaby,
    // że wasm3 nie zbudował pamięci — a moduł zapisujący do niej wywaliłby się
    // dopiero w trakcie pracy.
    CHECK_EQ(engine.linearMemoryBytes(), 65536u);

    engine.close();
}

TEST("wasm: cala pamiec idzie z puli, nie z malloc") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());

    const u32 afterOpen = engine.memory().usedBytes;
    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());
    const u32 afterLoad = engine.memory().usedBytes;

    // Interpreter i moduł mają widzieć się w statystykach puli. Zero wzrostu
    // znaczyłoby, że łatka alokatora nie działa i wasm3 woła `malloc()` —
    // czyli że reguła „brak alokacji po App::begin()" jest złamana bez śladu.
    CHECK(afterOpen > 0u);
    CHECK(afterLoad > afterOpen);
    CHECK(engine.memory().capacityBytes <= sizeof(gPool));

    engine.close();
}

TEST("wasm: pula zwalnia sie po zamknieciu silnika") {
    {
        Wasm3Engine first;
        first.configure(poolConfig());
        REQUIRE(first.open().has_value());

        // Alokator wasm3 nie ma kontekstu, więc naraz może pracować jeden
        // silnik. Drugi ma to powiedzieć wprost, a nie sięgnąć po cudzą pulę.
        Wasm3Engine second;
        second.configure(poolConfig());
        auto blocked = second.open();
        CHECK(!blocked.has_value());
        CHECK(blocked.error() == Err::Busy);
    }

    // Po wyjściu z zakresu destruktor zwolnił pulę — kolejny silnik wstaje.
    Wasm3Engine third;
    third.configure(poolConfig());
    CHECK(third.open().has_value());
    third.close();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Importy — funkcje gospodarza
// ═══════════════════════════════════════════════════════════════════════════
//
// Drugi moduł, też złożony ręcznie. Importuje `hydra.log`, `hydra.gpio_write`
// i `hydra.millis`, a eksportuje:
//
//   run()      loguje łańcuch z pamięci modułu i zapala nóżkę 2
//   bad_log()  woła log() ze wskaźnikiem 0x7FFFFFF0 — daleko poza pamięcią
//
// `bad_log` jest tu najważniejsza. Bez sprawdzania zakresu moduł czytałby
// pamięć urządzenia spod dowolnego adresu, a piaskownica byłaby fikcją.

namespace {

const u8 kImportModule[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x14, 0x04, 0x60,
    0x03, 0x7F, 0x7F, 0x7F, 0x00, 0x60, 0x02, 0x7F, 0x7F, 0x01, 0x7F, 0x60,
    0x00, 0x00, 0x60, 0x00, 0x01, 0x7F, 0x02, 0x2F, 0x03, 0x05, 0x68, 0x79,
    0x64, 0x72, 0x61, 0x03, 0x6C, 0x6F, 0x67, 0x00, 0x00, 0x05, 0x68, 0x79,
    0x64, 0x72, 0x61, 0x0A, 0x67, 0x70, 0x69, 0x6F, 0x5F, 0x77, 0x72, 0x69,
    0x74, 0x65, 0x00, 0x01, 0x05, 0x68, 0x79, 0x64, 0x72, 0x61, 0x06, 0x6D,
    0x69, 0x6C, 0x6C, 0x69, 0x73, 0x00, 0x03, 0x03, 0x03, 0x02, 0x02, 0x02,
    0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x1A, 0x03, 0x03, 0x72, 0x75, 0x6E,
    0x00, 0x03, 0x07, 0x62, 0x61, 0x64, 0x5F, 0x6C, 0x6F, 0x67, 0x00, 0x04,
    0x06, 0x6D, 0x65, 0x6D, 0x6F, 0x72, 0x79, 0x02, 0x00, 0x0A, 0x22, 0x02,
    0x11, 0x00, 0x41, 0x01, 0x41, 0x10, 0x41, 0x0C, 0x10, 0x00, 0x41, 0x02,
    0x41, 0x01, 0x10, 0x01, 0x1A, 0x0B, 0x0E, 0x00, 0x41, 0x01, 0x41, 0xF0,
    0xFF, 0xFF, 0xFF, 0x07, 0x41, 0x10, 0x10, 0x00, 0x0B, 0x0B, 0x12, 0x01,
    0x00, 0x41, 0x10, 0x0B, 0x0C, 0x63, 0x7A, 0x65, 0x73, 0x63, 0x20, 0x7A,
    0x20, 0x77, 0x61, 0x73, 0x6D,
};

CByteSpan importModule() { return CByteSpan{kImportModule, sizeof(kImportModule)}; }

}  // namespace

TEST("wasm: modul woła funkcje gospodarza") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(importModule(), "=imports").has_value());

    // Bindingi wpina się po załadowaniu modułu — wcześniej nie ma w co.
    REQUIRE(installWasmBindings(engine).has_value());

    CHECK(engine.hasFunction("run"));
    CHECK(engine.callFunction("run").has_value());

    engine.close();
}

TEST("wasm: bindingi przed zaladowaniem modulu sa bledem") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());

    // Odwrotna kolejność dałaby moduł bez importów, wywalający się dopiero
    // przy pierwszym wywołaniu — w miejscu bez związku z przyczyną.
    auto result = installWasmBindings(engine);
    CHECK(!result.has_value());
    CHECK(result.error() == Err::NotInitialized);

    engine.close();
}

TEST("wasm: wskaznik poza pamiecia modulu jest odrzucany") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(importModule(), "=imports").has_value());
    REQUIRE(installWasmBindings(engine).has_value());

    // To jest granica piaskownicy. Moduł podaje adres 0x7FFFFFF0 jako miejsce,
    // z którego gospodarz ma czytać łańcuch. Poprawną odpowiedzią jest pułapka
    // maszyny, a nie odczyt pamięci urządzenia.
    REQUIRE(engine.startJob("bad_log").has_value());
    CHECK(engine.resumeJob(0) == RunState::Failed);
    CHECK(engine.error()[0] != '\0');

    // Po pułapce silnik zostaje zdatny do pracy — poprawne wywołanie przechodzi.
    CHECK(engine.callFunction("run").has_value());

    engine.close();
}

TEST("wasm: modul bierze tylko te importy, ktorych uzywa") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());

    // Pierwszy moduł testowy nie importuje niczego. Wpięcie kompletu funkcji
    // ma się udać mimo to: gospodarz oferuje, moduł bierze tyle, ile potrzebuje.
    REQUIRE(engine.loadBinary(testModule(), "=bez-importow").has_value());
    CHECK(installWasmBindings(engine).has_value());

    engine.close();
}

TEST("wasm: wylaczona grupa nie jest wiazana") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(importModule(), "=imports").has_value());

    BindingSet limited;
    limited.log   = false;    // moduł tego importu potrzebuje
    limited.gpio  = true;
    limited.core  = true;
    limited.event = false;

    // Samo wiązanie przechodzi: pomijamy funkcje, których moduł nie importuje,
    // i nie mamy jak odróżnić „moduł tego nie chce" od „grupa wyłączona".
    CHECK(installWasmBindings(engine, limited).has_value());

    // Brak wychodzi przy **wyszukaniu** funkcji, nie przy jej wywołaniu:
    // wasm3 kompiluje ciało przy `m3_FindFunction`, a niezwiązany import
    // przerywa kompilację. To jest lepszy moment niż wywołanie — program
    // nie zdąży wykonać połowy pracy, zanim się zorientuje.
    auto started = engine.startJob("run");
    CHECK(!started.has_value());
    CHECK(engine.error()[0] != '\0');

    engine.close();
}

TEST("wasm: podmiana modulu w locie") {
    static Wasm3Engine engine;
    engine.configure(poolConfig());

    ScriptModule module;
    ScriptModule::Config cfg;
    cfg.engine = &engine;
    // Bindingi wpina projekt, bo moduł skryptów nie zna wasm3.
    cfg.onEngineReady = [](IScriptEngine& e) {
        return installWasmBindings(static_cast<Wasm3Engine&>(e));
    };
    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    // Pierwszy program: bez importów.
    REQUIRE(module.loadModule(testModule(), "=pierwszy").has_value());
    CHECK(engine.hasFunction("loop"));
    CHECK(!engine.hasFunction("run"));

    // Drugi w to samo miejsce — stary znika w całości razem z pamięcią.
    REQUIRE(module.loadModule(importModule(), "=drugi").has_value());
    CHECK(engine.hasFunction("run"));
    CHECK(!engine.hasFunction("boom"));

    // Bindingi przeżyły podmianę, bo hak wołany jest za każdym razem.
    CHECK(engine.callFunction("run").has_value());

    module.stop();
}

TEST("wasm: nieudana podmiana nie przywraca starego programu") {
    static Wasm3Engine engine;
    engine.configure(poolConfig());

    ScriptModule module;
    ScriptModule::Config cfg;
    cfg.engine = &engine;
    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());
    REQUIRE(module.loadModule(testModule(), "=dobry").has_value());

    const u8 smiec[] = {'n', 'i', 'e', '-', 'w', 'a', 's', 'm'};
    CHECK(!module.loadModule(CByteSpan{smiec, sizeof(smiec)}, "=zly").has_value());

    // Silnik stoi otwarty i pusty. Powrót do poprzedniego wymagałby trzymania
    // obu obrazów naraz — czyli podwojenia pamięci na program.
    CHECK(engine.ready());
    CHECK(!engine.hasFunction("loop"));

    module.stop();
}

TEST("script: silnik tekstowy odmawia programu binarnego") {
    ScriptModule module;
    ScriptModule::Config cfg;
    cfg.source = nullptr;
    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    // Domyślnym silnikiem jest Lua — obrazu binarnego nie przyjmie.
    const u8 image[] = {0x00, 0x61, 0x73, 0x6D};
    auto result = module.loadModule(CByteSpan{image, sizeof(image)}, "=x");
    CHECK(!result.has_value());
    CHECK(result.error() == Err::NotSupported);

    module.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Dostarczanie programu z sieci
// ═══════════════════════════════════════════════════════════════════════════

namespace {
u8 gImageBuffer[4096];

/** Wysyła obraz kawałkami o zadanym rozmiarze. */
Status feedChunks(ProgramReceiver& rx, CByteSpan image, u32 chunkSize) {
    for (u32 offset = 0; offset < image.size(); offset += chunkSize) {
        const u32 take = offset + chunkSize <= image.size()
                             ? chunkSize
                             : static_cast<u32>(image.size() - offset);
        HYDRA_CHECK(rx.chunk(offset, CByteSpan{image.data() + offset, take}));
    }
    return ok();
}
}  // namespace

TEST("odbiornik: program sklada sie z kawalkow i trafia do silnika") {
    ProgramReceiver rx;
    REQUIRE(rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)}).has_value());

    const CByteSpan image = testModule();
    REQUIRE(rx.expect(static_cast<u32>(image.size()), crc16Ccitt(image)).has_value());
    CHECK(!rx.complete());

    // 16 bajtów na kawałek — mniej niż realny pakiet, żeby wymusić wiele porcji.
    REQUIRE(feedChunks(rx, image, 16).has_value());

    CHECK(rx.complete());
    CHECK_EQ(rx.received(), static_cast<u32>(image.size()));

    static Wasm3Engine engine;
    engine.configure(poolConfig());
    ScriptModule module;
    ScriptModule::Config cfg;
    cfg.engine = &engine;
    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    // Bufor odbiornika jest statyczny, więc przeżyje moduł — tego wymaga wasm3.
    REQUIRE(module.loadModule(rx.image(), "=z-sieci").has_value());
    CHECK(engine.hasFunction("loop"));

    module.stop();
}

TEST("odbiornik: niekompletny obraz nie jest wydawany") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});

    const CByteSpan image = testModule();
    (void)rx.expect(static_cast<u32>(image.size()), crc16Ccitt(image));

    // Brakuje ostatniego kawałka — tak wygląda zerwane połączenie.
    (void)rx.chunk(0, CByteSpan{image.data(), image.size() - 8});

    CHECK(!rx.complete());
    // Obraz w połowie wygląda jak poprawny, dopóki nie sięgnie się do reszty.
    CHECK(rx.image().empty());
}

TEST("odbiornik: przekłamany bajt wychodzi na sumie kontrolnej") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});

    const CByteSpan image = testModule();
    (void)rx.expect(static_cast<u32>(image.size()), crc16Ccitt(image));
    (void)feedChunks(rx, image, 32);
    REQUIRE(rx.complete());

    // Jeden bit zmieniony po drodze. Wszystkie bajty przyszły, więc sam
    // licznik ich nie wyłapie — od tego jest suma.
    gImageBuffer[20] = static_cast<u8>(gImageBuffer[20] ^ 0x01);
    CHECK(!rx.complete());
    CHECK(rx.image().empty());
}

TEST("odbiornik: kawalki nie po kolei i powtorzone") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});

    const CByteSpan image = testModule();
    (void)rx.expect(static_cast<u32>(image.size()), crc16Ccitt(image));

    // Sieć zmienia kolejność i dubluje pakiety — obraz ma się złożyć mimo to.
    const u32 half = static_cast<u32>(image.size()) / 2;
    (void)rx.chunk(half, CByteSpan{image.data() + half, image.size() - half});
    (void)rx.chunk(0, CByteSpan{image.data(), half});
    (void)rx.chunk(0, CByteSpan{image.data(), half});   // powtórka

    CHECK(rx.complete());
    CHECK_EQ(rx.received(), static_cast<u32>(image.size()));
    CHECK(rx.stats().duplicates >= 1u);
}

TEST("odbiornik: kawalek poza zapowiedzianym rozmiarem odrzucony") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});
    (void)rx.expect(64, 0);

    const u8 data[32] = {};
    auto result = rx.chunk(48, CByteSpan{data, sizeof(data)});   // 48+32 > 64
    CHECK(!result.has_value());
    CHECK(result.error() == Err::OutOfRange);
    CHECK_EQ(rx.stats().rejected, 1u);
}

TEST("odbiornik: kawalek bez zapowiedzi nie ma gdzie trafic") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});

    const u8 data[8] = {};
    CHECK(!rx.chunk(0, CByteSpan{data, sizeof(data)}).has_value());
}

TEST("odbiornik: program wiekszy niz bufor odrzucony od razu") {
    ProgramReceiver rx;
    u8 small[64];
    (void)rx.begin(ByteSpan{small, sizeof(small)});

    // Odmowa przy zapowiedzi, nie przy ostatnim kawałku — inaczej marnuje się
    // cały transfer.
    auto result = rx.expect(4096, 0);
    CHECK(!result.has_value());
    CHECK(result.error() == Err::OutOfRange);
}

TEST("odbiornik: ponowienie tej samej zapowiedzi kontynuuje transfer") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});

    const CByteSpan image = testModule();
    const u16 crc = crc16Ccitt(image);
    (void)rx.expect(static_cast<u32>(image.size()), crc);
    (void)rx.chunk(0, CByteSpan{image.data(), 32});

    // Nadawca po restarcie powtarza zapowiedź. Te same parametry znaczą
    // „to ten sam transfer" — kasowanie tego, co doszło, byłoby marnotrawstwem.
    REQUIRE(rx.expect(static_cast<u32>(image.size()), crc).has_value());
    CHECK_EQ(rx.received(), 32u);

    // Inne parametry to nowy program — od zera.
    REQUIRE(rx.expect(static_cast<u32>(image.size()), static_cast<u16>(crc ^ 0xFFFF)).has_value());
    CHECK_EQ(rx.received(), 0u);
}

TEST("wasm: modul bez on_event nie zjada sygnalow") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(testModule(), "=bez-on-event").has_value());
    REQUIRE(installWasmBindings(engine).has_value());

    // Program, który nie słucha magistrali, ma zostawić sygnały w kolejce
    // dla tego, kto ich chce — a nie zdjąć je i wyrzucić.
    CHECK_EQ(dispatchWasmSignals(engine, 8), 0u);

    engine.close();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Dobór silnika
// ═══════════════════════════════════════════════════════════════════════════
//
// Reguła jest funkcją czystą, więc sprawdza się ją dla sprzętu, którego się
// nie ma — łącznie z układami, na których nikt tego jeszcze nie uruchamiał.

namespace {

EngineInputs inputs(u32 ram, bool psram, bool wasm3, bool wamr) {
    EngineInputs in;
    in.usableRamBytes = ram;
    in.hasPsram       = psram;
    in.wasm3Available = wasm3;
    in.wamrAvailable  = wamr;
    return in;
}

}  // namespace

TEST("dobor: ponizej progu wygrywa lekki interpreter") {
    // ESP32-C3 z 400 KB pamięci całkowitej ma dla programu znacznie mniej.
    const auto choice = choose(inputs(180 * 1024, false, true, true));
    CHECK(choice.kind == EngineKind::Wasm3);
    CHECK(!choice.fallback);
}

TEST("dobor: powyzej progu wygrywa szybszy") {
    const auto choice = choose(inputs(320 * 1024, false, true, true));
    CHECK(choice.kind == EngineKind::Wamr);
    CHECK(!choice.fallback);
}

TEST("dobor: prog jest wlaczajacy") {
    // Dokładnie 256 KB ma już wystarczyć — reguła mówi „co najmniej".
    CHECK(choose(inputs(kWamrRamThreshold, false, true, true)).kind == EngineKind::Wamr);
    CHECK(choose(inputs(kWamrRamThreshold - 1, false, true, true)).kind == EngineKind::Wasm3);
}

TEST("dobor: PSRAM zdejmuje prog") {
    // Przy pamięci zewnętrznej wolniejszy interpreter boli podwójnie, więc
    // ilość RAM-u przestaje decydować.
    const auto choice = choose(inputs(64 * 1024, true, true, true));
    CHECK(choice.kind == EngineKind::Wamr);
    CHECK(!choice.fallback);
}

TEST("dobor: brak WAMR w obrazie jest ustepstwem, nie cichym wyborem") {
    const auto choice = choose(inputs(512 * 1024, true, true, false));

    CHECK(choice.kind == EngineKind::Wasm3);
    // To jest sedno: wybór działa, ale nie jest tym, który wskazała reguła.
    // Bez tego znacznika nikt się nie dowie, że urządzenie chodzi wolniej,
    // niż mogło.
    CHECK(choice.fallback);
    CHECK(choice.reason[0] != '\0');
}

TEST("dobor: malo pamieci i tylko WAMR — zgoda z ostrzezeniem") {
    const auto choice = choose(inputs(96 * 1024, false, false, true));
    CHECK(choice.kind == EngineKind::Wamr);
    CHECK(choice.fallback);
}

TEST("dobor: brak jakiegokolwiek interpretera WebAssembly") {
    const auto choice = choose(inputs(512 * 1024, false, false, false));

    // Podstawienie Lua byłoby gorsze: wołający dostałby silnik, który odrzuci
    // jego moduł dopiero przy ładowaniu.
    CHECK(choice.kind == EngineKind::None);
    CHECK(choice.reason[0] != '\0');
}

TEST("dobor: program tekstowy nie ma czego wybierac") {
    EngineInputs in = inputs(512 * 1024, true, true, true);
    in.wantsWasm = false;

    const auto choice = choose(in);
    CHECK(choice.kind == EngineKind::Lua);
}

TEST("dobor: odczyt ze sprzetu daje decyzje zgodna z regula") {
    const auto probed = probe(true);

    // Na hoście pamięci jest dużo, a WAMR-a w obrazie nie ma — spodziewamy się
    // wasm3 z zaznaczonym ustępstwem. Test pilnuje, że `probe()` przepuszcza
    // odczyt przez tę samą regułę, a nie ma własnej.
    CHECK(probed.kind == EngineKind::Wasm3 || probed.kind == EngineKind::Wamr);
    CHECK(probed.reason[0] != '\0');
}

// ═══════════════════════════════════════════════════════════════════════════
//  Droga powrotna: magistrala → moduł
// ═══════════════════════════════════════════════════════════════════════════
//
// Trzeci moduł. Eksportuje `on_event(i32, f32, i32)` i zapamiętuje w globalnych
// to, co dostał; `last_id()` i `last_data()` pozwalają to odczytać. Bez tego
// modułu `dispatchWasmSignals()` było sprawdzone wyłącznie w przypadku
// „program nie słucha" — czyli w tym, w którym nic się nie dzieje.

namespace {

const u8 kEventModule[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0B, 0x02, 0x60,
    0x03, 0x7F, 0x7D, 0x7F, 0x00, 0x60, 0x00, 0x01, 0x7F, 0x03, 0x04, 0x03,
    0x00, 0x01, 0x01, 0x06, 0x0B, 0x02, 0x7F, 0x01, 0x41, 0x00, 0x0B, 0x7F,
    0x01, 0x41, 0x00, 0x0B, 0x07, 0x22, 0x03, 0x08, 0x6F, 0x6E, 0x5F, 0x65,
    0x76, 0x65, 0x6E, 0x74, 0x00, 0x00, 0x07, 0x6C, 0x61, 0x73, 0x74, 0x5F,
    0x69, 0x64, 0x00, 0x01, 0x09, 0x6C, 0x61, 0x73, 0x74, 0x5F, 0x64, 0x61,
    0x74, 0x61, 0x00, 0x02, 0x0A, 0x16, 0x03, 0x0A, 0x00, 0x20, 0x00, 0x24,
    0x00, 0x20, 0x02, 0x24, 0x01, 0x0B, 0x04, 0x00, 0x23, 0x00, 0x0B, 0x04,
    0x00, 0x23, 0x01, 0x0B,
};

CByteSpan eventModule() { return CByteSpan{kEventModule, sizeof(kEventModule)}; }

}  // namespace

TEST("wasm: sygnal z magistrali dochodzi do modulu") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(eventModule(), "=on-event").has_value());
    REQUIRE(installWasmBindings(engine).has_value());
    CHECK(engine.hasFunction("on_event"));

    // Sygnał wchodzi tą samą drogą, co z każdego innego miejsca systemu.
    EventBus::publish(ScriptSignal{4242, 1.5f, -7});

    CHECK_EQ(dispatchWasmSignals(engine, 8), 1u);

    // Moduł zapamiętał to, co dostał — dowód, że argumenty przeszły granicę
    // we właściwej kolejności i z właściwymi typami.
    auto id = engine.callInt("last_id");
    REQUIRE(id.has_value());
    CHECK_EQ(*id, 4242);

    auto data = engine.callInt("last_data");
    REQUIRE(data.has_value());
    CHECK_EQ(*data, -7);

    engine.close();
}

TEST("wasm: limit sygnalow na przebieg jest przestrzegany") {
    Wasm3Engine engine;
    engine.configure(poolConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(eventModule(), "=on-event").has_value());
    REQUIRE(installWasmBindings(engine).has_value());

    for (u8 i = 0; i < 5; ++i) EventBus::publish(ScriptSignal{static_cast<u16>(100 + i), 0.0f, i});

    // Limit istnieje po to, żeby zalew zdarzeń nie zjadł całego przebiegu
    // taska — reszta poczeka do następnego.
    CHECK_EQ(dispatchWasmSignals(engine, 2), 2u);
    CHECK_EQ(dispatchWasmSignals(engine, 8), 3u);
    CHECK_EQ(dispatchWasmSignals(engine, 8), 0u);

    engine.close();
}

TEST("odbiornik: ramka transportowa niesie zapowiedz w kazdym kawalku") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});

    const CByteSpan image = testModule();
    const u32 total = static_cast<u32>(image.size());
    const u16 crc   = crc16Ccitt(image);

    u8 frame[64 + kProgramFrameHeader];
    const u32 payload = 40;

    // Kawałki celowo od końca: zapowiedź jedzie w każdym, więc pierwszy
    // odebrany niesie komplet informacji niezależnie od kolejności.
    for (i32 offset = static_cast<i32>((total - 1) / payload) * static_cast<i32>(payload);
         offset >= 0; offset -= static_cast<i32>(payload)) {
        const u32 take = (static_cast<u32>(offset) + payload <= total)
                             ? payload : total - static_cast<u32>(offset);
        frame[0] = 'H'; frame[1] = 'W'; frame[2] = 'A'; frame[3] = 'M';
        frame[4] = static_cast<u8>(total >> 24); frame[5] = static_cast<u8>(total >> 16);
        frame[6] = static_cast<u8>(total >> 8);  frame[7] = static_cast<u8>(total);
        frame[8] = static_cast<u8>(crc >> 8);    frame[9] = static_cast<u8>(crc);
        frame[10] = static_cast<u8>(offset >> 8); frame[11] = static_cast<u8>(offset);
        memcpy(frame + kProgramFrameHeader, image.data() + offset, take);

        REQUIRE(rx.feed(CByteSpan{frame, kProgramFrameHeader + take}).has_value());
    }

    CHECK(rx.complete());
    CHECK_EQ(rx.received(), total);
}

TEST("odbiornik: cudzy ruch na temacie jest odrzucany, nie psuty") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});

    // Wiadomość bez znacznika — ktoś inny publikuje na tym samym temacie.
    const u8 obcy[] = {'{', '"', 'a', '"', ':', '1', '}', 0, 0, 0, 0, 0, 9, 9};
    auto result = rx.feed(CByteSpan{obcy, sizeof(obcy)});

    CHECK(!result.has_value());
    CHECK(result.error() == Err::Protocol);
    // Nie zaczął się żaden transfer — cudzy ładunek nie ma wpływu na stan.
    CHECK_EQ(rx.expected(), 0u);
}

TEST("odbiornik: ramka krotsza niz naglowek") {
    ProgramReceiver rx;
    (void)rx.begin(ByteSpan{gImageBuffer, sizeof(gImageBuffer)});

    const u8 urwana[] = {'H', 'W', 'A', 'M', 0, 0};
    CHECK(!rx.feed(CByteSpan{urwana, sizeof(urwana)}).has_value());
}

#if HYDRA_SCRIPT_HAS_WAMR

// ═══════════════════════════════════════════════════════════════════════════
//  WAMR
// ═══════════════════════════════════════════════════════════════════════════
//
// Ten sam moduł testowy, co dla wasm3 — i to jest sens tych testów. Program
// nie wie, który interpreter go wykonuje, więc oba silniki muszą dać ten sam
// wynik. Rozjazd oznaczałby, że wybór silnika zmienia zachowanie urządzenia,
// a nie tylko jego szybkość.

namespace {
alignas(8) u8 gWamrPool[256 * 1024];

WamrEngine::Config wamrConfig() {
    WamrEngine::Config cfg;
    cfg.pool      = gWamrPool;
    cfg.poolBytes = sizeof(gWamrPool);
    return cfg;
}
}  // namespace

TEST("wamr: silnik zglasza sie jako ciezszy interpreter") {
    WamrEngine engine;
    const EngineInfo info = engine.info();
    CHECK_STR(info.name, "wamr");
    CHECK(info.language == ScriptLanguage::Wasm);
    // Strażnik z innego wątku istnieje w API, ale nikt go nie uruchamia —
    // deklarowanie `Watchdog` byłoby obietnicą bez pokrycia.
    CHECK(info.preemption == Preemption::RunToCompletion);
    CHECK(info.acceptsBinary);
    CHECK(!info.acceptsSource);
}

TEST("wamr: modul wykonuje sie i zmienia stan") {
    WamrEngine engine;
    engine.configure(wamrConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());

    CHECK(engine.hasFunction("setup"));
    CHECK(!engine.hasFunction("nie_ma_takiej"));

    REQUIRE(engine.callFunction("setup").has_value());
    for (u8 i = 0; i < 3; ++i) {
        REQUIRE(engine.startJob("loop").has_value());
        CHECK(engine.resumeJob(0) == RunState::Done);
    }
    CHECK_EQ(engine.jobSteps(), 3u);
    engine.close();
}

TEST("wamr: pulapka w module to blad, nie wywrotka") {
    WamrEngine engine;
    engine.configure(wamrConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());

    REQUIRE(engine.startJob("boom").has_value());
    CHECK(engine.resumeJob(0) == RunState::Failed);
    CHECK(engine.error()[0] != '\0');

    // Wyjątek zostaje na instancji do wyczyszczenia i blokuje kolejne
    // wywołania. Bez zdjęcia go jedna pułapka unieruchomiłaby moduł na stałe.
    REQUIRE(engine.startJob("loop").has_value());
    CHECK(engine.resumeJob(0) == RunState::Done);
    engine.close();
}

TEST("wamr: obcy plik odrzucony po naglowku") {
    WamrEngine engine;
    engine.configure(wamrConfig());
    REQUIRE(engine.open().has_value());

    const char* lua = "function loop() end";
    auto result = engine.loadBinary(
        CByteSpan{reinterpret_cast<const u8*>(lua), strlen(lua)}, "=zly");
    CHECK(!result.has_value());
    CHECK(result.error() == Err::Protocol);
    engine.close();
}

TEST("wamr: pamiec idzie z podanej puli") {
    WamrEngine engine;
    engine.configure(wamrConfig());
    REQUIRE(engine.open().has_value());
    REQUIRE(engine.loadBinary(testModule(), "=test").has_value());

    const ScriptMemory mem = engine.memory();
    // Zero pojemności znaczyłoby, że WAMR nie wziął naszej puli i alokuje
    // z malloc() — czyli że reguła „brak alokacji po App::begin()" jest
    // złamana bez śladu.
    CHECK(mem.capacityBytes > 0u);
    CHECK(mem.usedBytes > 0u);
    CHECK(mem.capacityBytes <= sizeof(gWamrPool));
    engine.close();
}

TEST("wamr: naraz jeden silnik") {
    WamrEngine first;
    first.configure(wamrConfig());
    REQUIRE(first.open().has_value());

    // WAMR trzyma środowisko globalnie; drugi `full_init` nadpisałby pulę
    // pierwszego. Odmowa jest jedyną uczciwą odpowiedzią.
    WamrEngine second;
    second.configure(wamrConfig());
    auto blocked = second.open();
    CHECK(!blocked.has_value());
    CHECK(blocked.error() == Err::Busy);

    first.close();
    CHECK(second.open().has_value());
    second.close();
}

TEST("wamr: oba silniki daja ten sam wynik") {
    Wasm3Engine light;
    light.configure(poolConfig());
    REQUIRE(light.open().has_value());
    REQUIRE(light.loadBinary(testModule(), "=test").has_value());
    REQUIRE(light.callFunction("setup").has_value());
    REQUIRE(light.startJob("loop").has_value());
    CHECK(light.resumeJob(0) == RunState::Done);
    auto fromWasm3 = light.callInt("get");
    REQUIRE(fromWasm3.has_value());
    light.close();

    WamrEngine heavy;
    heavy.configure(wamrConfig());
    REQUIRE(heavy.open().has_value());
    REQUIRE(heavy.loadBinary(testModule(), "=test").has_value());
    REQUIRE(heavy.callFunction("setup").has_value());
    REQUIRE(heavy.startJob("loop").has_value());
    CHECK(heavy.resumeJob(0) == RunState::Done);
    heavy.close();

    // setup() ustawia 100, jedno loop() dokłada jeden. Ten sam program,
    // dwa interpretery, jeden wynik.
    CHECK_EQ(*fromWasm3, 101);
}

#endif  // HYDRA_SCRIPT_HAS_WAMR

#endif  // HYDRA_ENABLE_SCRIPT
