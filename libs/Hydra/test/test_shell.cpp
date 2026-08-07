/**
 * Testy shella diagnostycznego i zapisu awarii (etap M6a).
 *
 * Shell jest jedynym interfejsem, przez który testy sprzętowe w CI będą
 * sterować urządzeniem i odczytywać wynik. Stąd nacisk na to, żeby wyjście
 * dało się przetworzyć maszynowo, a rozbiór wiersza znosił to, co naprawdę
 * przychodzi z terminala: znaki cofania, puste wiersze, wklejone bloki.
 */

#include "hydra_test.hpp"

#include <stdio.h>
#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/diag/CrashRecorder.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/shell/Shell.hpp"
#include "hydra/shell/ShellModule.hpp"

using namespace hydra;
using namespace hydra::shell;

namespace {

void resetShell() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

/** Zbiera wyjście shella, żeby dało się je sprawdzić. */
struct Capture {
    static constexpr size_t kMax = 4096;
    char   text[kMax] = {};
    size_t length     = 0;

    Output::Sink sink() {
        return [this](const char* data, size_t n) {
            for (size_t i = 0; i < n && length < kMax - 1; ++i) text[length++] = data[i];
            text[length] = '\0';
        };
    }

    bool contains(const char* needle) const { return strstr(text, needle) != nullptr; }
    void clear() {
        length  = 0;
        text[0] = '\0';
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Rozbiór wiersza
// ---------------------------------------------------------------------------

TEST("Shell: komenda dostaje nazwę i argumenty") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    int   seenArgc = 0;
    char  first[16] = {};
    char  second[16] = {};

    REQUIRE(shell.add("test", "opis", [&](int argc, char** argv, Output&) {
                          seenArgc = argc;
                          if (argc > 1) strncpy(first, argv[1], sizeof(first) - 1);
                          if (argc > 2) strncpy(second, argv[2], sizeof(second) - 1);
                          return ok();
                      }).has_value());

    REQUIRE(shell.execute("test alfa beta").has_value());
    CHECK_EQ(seenArgc, 3);
    CHECK_STR(first, "alfa");
    CHECK_STR(second, "beta");
}

TEST("Shell: nadmiarowe odstępy nie tworzą pustych argumentów") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    int seenArgc = 0;
    REQUIRE(shell.add("t", "", [&](int argc, char**, Output&) {
                          seenArgc = argc;
                          return ok();
                      }).has_value());

    // Tak wygląda wiersz przepisany ręcznie albo wklejony z dokumentacji.
    REQUIRE(shell.execute("   t    a     b   ").has_value());
    CHECK_EQ(seenArgc, 3);
}

TEST("Shell: pusty wiersz nie jest błędem") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    // Wciśnięcie enter na pustym wierszu to najczęstsza rzecz, jaką robi
    // człowiek przy terminalu.
    CHECK(shell.execute("").has_value());
    CHECK(shell.execute("    ").has_value());
    CHECK_EQ(static_cast<int>(shell.stats().executed), 0);
}

TEST("Shell: nieznana komenda podpowiada, gdzie szukać") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    CHECK(shell.execute("nieistnieje").error() == Err::NotFound);
    CHECK(out.contains("nieznana komenda"));
    CHECK(out.contains("help"));
    CHECK_EQ(static_cast<int>(shell.stats().unknown), 1);
}

TEST("Shell: błąd komendy jest widoczny w wyjściu") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    REQUIRE(shell.add("zly", "", [](int, char**, Output&) {
                          return fail(Err::Timeout);
                      }).has_value());

    CHECK(shell.execute("zly").error() == Err::Timeout);
    CHECK(out.contains("timeout"));
    CHECK_EQ(static_cast<int>(shell.stats().failed), 1);
}

TEST("Shell: rejestracja odrzuca duplikaty i przepełnienie") {
    Shell shell;
    auto  noop = [](int, char**, Output&) { return ok(); };

    REQUIRE(shell.add("a", "", noop).has_value());
    CHECK(shell.add("a", "", noop).error() == Err::AlreadyExists);
    CHECK(shell.add("", "", noop).error() == Err::BadArgument);
    CHECK(shell.add("b", "", Shell::Handler{}).error() == Err::BadArgument);

    for (u8 i = 1; i < HYDRA_SHELL_MAX_COMMANDS; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "c%u", static_cast<unsigned>(i));
        // Nazwy muszą przeżyć shell, więc w teście trzymamy je statycznie.
        static char names[HYDRA_SHELL_MAX_COMMANDS][8];
        strncpy(names[i], name, sizeof(names[i]) - 1);
        REQUIRE(shell.add(names[i], "", noop).has_value());
    }
    CHECK(shell.add("nadmiar", "", noop).error() == Err::OutOfMemory);
}

// ---------------------------------------------------------------------------
// Edycja wiersza
// ---------------------------------------------------------------------------

TEST("Shell: znak końca wiersza wykonuje komendę") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    int calls = 0;
    REQUIRE(shell.add("go", "", [&](int, char**, Output&) {
                          ++calls;
                          return ok();
                      }).has_value());

    shell.feed("go");
    CHECK_EQ(calls, 0);  // wiersz jeszcze niezakończony
    CHECK(shell.editing());

    shell.feed('\r');
    CHECK_EQ(calls, 1);
    CHECK(!shell.editing());
    // Po wykonaniu pojawia się zachęta do kolejnej komendy.
    CHECK(out.contains("hydra>"));
}

TEST("Shell: cofanie kasuje ostatni znak") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    int calls = 0;
    REQUIRE(shell.add("ps", "", [&](int, char**, Output&) {
                          ++calls;
                          return ok();
                      }).has_value());

    // Literówka poprawiona backspace'em — tak wygląda praca przy terminalu.
    shell.feed("px");
    shell.feed('\x08');
    shell.feed("s\n");
    CHECK_EQ(calls, 1);
}

TEST("Shell: znaki sterujące nie trafiają do wiersza") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    char seen[16] = {};
    REQUIRE(shell.add("t", "", [&](int argc, char** argv, Output&) {
                          if (argc > 1) strncpy(seen, argv[1], sizeof(seen) - 1);
                          return ok();
                      }).has_value());

    // Sekwencje sterujące terminala nie mogą wylądować w argumencie.
    shell.feed("t a");
    shell.feed('\x1B');
    shell.feed('\x01');
    shell.feed("b\n");
    CHECK_STR(seen, "ab");
}

TEST("Shell: wiersz dłuższy niż bufor jest odrzucany, nie obcinany") {
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());

    int calls = 0;
    REQUIRE(shell.add("t", "", [&](int, char**, Output&) {
                          ++calls;
                          return ok();
                      }).has_value());

    shell.feed("t ");
    for (size_t i = 0; i < HYDRA_SHELL_LINE_MAX + 10; ++i) shell.feed('x');
    shell.feed('\n');

    // Wykonanie obciętego wiersza mogłoby znaczyć coś zupełnie innego niż
    // to, co wpisano — bezpieczniej odmówić.
    CHECK_EQ(calls, 0);
    CHECK(out.contains("za długi"));
    CHECK_EQ(static_cast<int>(shell.stats().overflow), 1);
}

// ---------------------------------------------------------------------------
// Wyjście
// ---------------------------------------------------------------------------

TEST("Shell: wyjście w postaci klucz=wartość") {
    Capture out;
    Output  output(out.sink());

    output.field("uptime_s", static_cast<u32>(42));
    output.field("name", "rover-01");
    output.field("offset", static_cast<i32>(-7));

    // Testy sprzętowe w CI rozbierają to wyrażeniem, więc format musi być
    // stały, a nie tylko czytelny.
    CHECK(out.contains("uptime_s=42\r\n"));
    CHECK(out.contains("name=rover-01\r\n"));
    CHECK(out.contains("offset=-7\r\n"));
}

// ---------------------------------------------------------------------------
// Komendy wbudowane
// ---------------------------------------------------------------------------

TEST("Komendy: help wymienia zarejestrowane komendy") {
    resetShell();
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerCoreCommands(shell).has_value());

    REQUIRE(shell.execute("help").has_value());
    CHECK(out.contains("ps"));
    CHECK(out.contains("top"));
    CHECK(out.contains("reboot"));
}

TEST("Komendy: ps pokazuje żywe taski") {
    resetShell();
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerCoreCommands(shell).has_value());

    Task      task;
    Task::Cfg cfg;
    cfg.name = "test.worker";
    REQUIRE(task.startPeriodic(cfg, 20, [] {}).has_value());
    rtos::delayMs(50);

    out.clear();
    REQUIRE(shell.execute("ps").has_value());
    CHECK(out.contains("test.worker"));
    CHECK(out.contains("tasks="));

    task.stopAndWait();
}

TEST("Komendy: top podaje wskaźniki w postaci do przetworzenia") {
    resetShell();
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerCoreCommands(shell).has_value());

    REQUIRE(shell.execute("top").has_value());
    CHECK(out.contains("uptime_s="));
    CHECK(out.contains("tasks="));
    CHECK(out.contains("stack_min="));
    CHECK(out.contains("events="));
}

TEST("Komendy: log zrzuca bufor i zmienia poziom") {
    resetShell();
    Log::init(LogLevel::Info, Log::Mode::Sync);

    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerCoreCommands(shell).has_value());

    Log::write(LogLevel::Warn, "test", "zdarzenie do odnalezienia");

    REQUIRE(shell.execute("log").has_value());
    CHECK(out.contains("zdarzenie do odnalezienia"));

    out.clear();
    REQUIRE(shell.execute("log level debug").has_value());
    CHECK(out.contains("level=debug"));
    CHECK(Log::level() == LogLevel::Debug);

    CHECK(shell.execute("log level bzdura").error() == Err::BadArgument);
}

TEST("Komendy: reboot zgłasza żądanie, a nie restartuje sam") {
    resetShell();
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerCoreCommands(shell).has_value());

    u8   delay   = 0;
    int  requests = 0;
    auto sub = EventBus::subscribe<RebootRequest>([&](const RebootRequest& e) {
        ++requests;
        delay = e.delaySec;
    });
    REQUIRE(sub.has_value());

    REQUIRE(shell.execute("reboot 5").has_value());
    // Decyzję o momencie restartu podejmuje warstwa, która wie, co jest
    // w toku — na przykład trwający zapis do pamięci trwałej.
    CHECK_EQ(requests, 1);
    CHECK_EQ(static_cast<int>(delay), 5);

    CHECK(shell.execute("reboot 999").error() == Err::OutOfRange);
}

TEST("Komendy: version wymienia moduły i ich stan") {
    resetShell();
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerCoreCommands(shell).has_value());

    REQUIRE(shell.execute("version").has_value());
    CHECK(out.contains("hydra="));
    CHECK(out.contains("platform=host"));
}

TEST("Komendy sprzętowe: skan magistrali znajduje układy") {
    resetShell();
    auto& mockHal = hal::mock::backend();
    REQUIRE(mockHal.i2c.addDevice(0x3C).has_value());
    REQUIRE(mockHal.i2c.addDevice(0x68).has_value());

    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerHalCommands(shell).has_value());

    REQUIRE(shell.execute("i2c scan").has_value());
    CHECK(out.contains("0x3C"));
    CHECK(out.contains("0x68"));
    CHECK(out.contains("found=2"));

    out.clear();
    CHECK(shell.execute("i2c").error() == Err::BadArgument);
    CHECK(out.contains("użycie"));
}

TEST("Komendy sprzętowe: odczyt i zapis pinu") {
    resetShell();
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerHalCommands(shell).has_value());

    REQUIRE(shell.execute("gpio write 5 1").has_value());
    CHECK(out.contains("level=1"));
    CHECK(hal::mock::backend().gpio.state(5).level);

    out.clear();
    REQUIRE(shell.execute("gpio read 5").has_value());
    CHECK(out.contains("pin=5"));
}

TEST("Komendy sprzętowe: odczyt napięcia po kalibracji") {
    resetShell();
    auto& mockHal = hal::mock::backend();

    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerHalCommands(shell).has_value());

    REQUIRE(hal::Hal::adc().configure(1, hal::AdcConfig{}).has_value());
    mockHal.adc.setPinMv(1, 1650);

    hal::AdcCalibration cal;
    cal.dividerNum = 2;  // pomiar przez dzielnik 1:2
    REQUIRE(hal::Hal::adc().setCalibration(1, cal).has_value());

    REQUIRE(shell.execute("adc 1").has_value());
    // Wynik ma być napięciem źródła, nie napięciem na pinie.
    CHECK(out.contains("mv=3300"));
}

TEST("Komendy sprzętowe: konfiguracja trwała przez cfg") {
    resetShell();
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerHalCommands(shell).has_value());

    REQUIRE(shell.execute("cfg set net ssid domowa").has_value());
    CHECK(out.contains("ssid=domowa"));

    out.clear();
    REQUIRE(shell.execute("cfg get net ssid").has_value());
    CHECK(out.contains("ssid=domowa"));

    out.clear();
    REQUIRE(shell.execute("cfg erase net ssid").has_value());
    CHECK(out.contains("erased=ssid"));
    CHECK(shell.execute("cfg get net ssid").error() == Err::NotFound);
}

TEST("Komendy sprzętowe: hal opisuje dostępne sterowniki") {
    resetShell();
    Shell   shell;
    Capture out;
    shell.setOutput(out.sink());
    REQUIRE(registerHalCommands(shell).has_value());

    REQUIRE(shell.execute("hal").has_value());
    CHECK(out.contains("backend=mock"));
    CHECK(out.contains("reset_reason=power-on"));
    CHECK(out.contains("i2c0=1"));
}

// ---------------------------------------------------------------------------
// Moduł shella
// ---------------------------------------------------------------------------

TEST("Moduł shella: czyta port i wykonuje komendy") {
    resetShell();
    auto& uart = hal::mock::backend().uart;
    REQUIRE(hal::Hal::uart(0).begin(hal::UartConfig{}).has_value());

    ShellModule module;
    ShellModule::Config cfg;
    cfg.banner = false;
    REQUIRE(module.configure(cfg).has_value());
    REQUIRE(module.init().has_value());

    // Komendy wbudowane rejestrują się przy inicjalizacji.
    CHECK(module.shell().find("ps") != nullptr);
    CHECK(module.shell().find("i2c") != nullptr);

    const char command[] = "version\r\n";
    uart.inject(CByteSpan{reinterpret_cast<const u8*>(command), sizeof(command) - 1});
    module.step();

    CHECK(module.bytesRead() >= 8);
    // Odpowiedź idzie na ten sam port, z którego przyszło pytanie.
    const CByteSpan sent = uart.sent();
    bool found = false;
    for (size_t i = 0; i + 6 < sent.size(); ++i) {
        if (memcmp(sent.data() + i, "hydra=", 6) == 0) found = true;
    }
    CHECK(found);
}

TEST("Moduł shella: błędna konfiguracja jest odrzucana") {
    resetShell();
    ShellModule module;
    ShellModule::Config cfg;
    cfg.pollMs = 0;
    CHECK(module.configure(cfg).error() == Err::BadArgument);
}

// ---------------------------------------------------------------------------
// Zapis awarii
// ---------------------------------------------------------------------------

TEST("Awaria: licznik rozruchów rośnie przy każdym starcie") {
    resetShell();

    diag::CrashRecorder first;
    REQUIRE(first.begin().has_value());
    const u32 firstBoot = first.bootCount();
    CHECK(firstBoot >= 1);

    diag::CrashRecorder second;
    REQUIRE(second.begin().has_value());
    // Pętla restartów wygląda niewinnie w pojedynczym logu; licznik pokazuje
    // ją od razu.
    CHECK_EQ(static_cast<int>(second.bootCount()), static_cast<int>(firstBoot + 1));
}

TEST("Awaria: zapisany kontekst przeżywa restart") {
    resetShell();

    {
        diag::CrashRecorder before;
        REQUIRE(before.begin().has_value());
        REQUIRE(before.record(ResetReason::Panic, nameId("motion"), 42,
                              "deadline pętli sterowania")
                    .has_value());
    }

    // Nowa instancja to odpowiednik ponownego uruchomienia.
    diag::CrashRecorder after;
    REQUIRE(after.begin().has_value());

    CHECK(after.hasPrevious());
    const diag::CrashRecord record = after.previous();
    CHECK(record.reason == ResetReason::Panic);
    CHECK_EQ(record.sourceId, nameId("motion"));
    CHECK_EQ(static_cast<int>(record.code), 42);
    CHECK_STR(record.detail, "deadline pętli sterowania");
}

TEST("Awaria: raport trafia na magistralę i kasuje zapis") {
    resetShell();

    {
        diag::CrashRecorder before;
        REQUIRE(before.begin().has_value());
        REQUIRE(before.record(ResetReason::Watchdog, 7, 1, "brak karmienia").has_value());
    }

    diag::CrashRecorder recorder;
    REQUIRE(recorder.begin().has_value());

    int              reports = 0;
    diag::CrashReported last{};
    auto sub = EventBus::subscribe<diag::CrashReported>(
        [&](const diag::CrashReported& e) {
            ++reports;
            last = e;
        });
    REQUIRE(sub.has_value());

    REQUIRE(recorder.publishAndClear().has_value());
    CHECK_EQ(reports, 1);
    CHECK(last.reason == ResetReason::Watchdog);

    // Po zgłoszeniu zapis znika — inaczej ten sam raport wracałby przy
    // każdym kolejnym rozruchu.
    diag::CrashRecorder next;
    REQUIRE(next.begin().has_value());
    CHECK(!next.hasPrevious());
}

TEST("Awaria: sprzętowa przyczyna resetu wystarcza za zapis") {
    resetShell();
    // Zadziałanie watchdoga albo zanik napięcia nie zostawia oprogramowaniu
    // szansy na zapisanie czegokolwiek — przyczyna pochodzi z rejestrów.
    hal::Hal::reset();
    REQUIRE(hal::mock::install(ResetReason::Watchdog).has_value());

    diag::CrashRecorder recorder;
    REQUIRE(recorder.begin().has_value());

    CHECK(recorder.hasPrevious());
    CHECK(recorder.previous().reason == ResetReason::Watchdog);
}

TEST("Awaria: zwykłe włączenie zasilania nie jest zgłaszane") {
    resetShell();

    diag::CrashRecorder recorder;
    REQUIRE(recorder.begin().has_value());
    // Atrapa zgłasza PowerOn — pierwszy start urządzenia to nie awaria.
    CHECK(!recorder.hasPrevious());
}

TEST("Awaria: ogon logu przenosi się przez restart") {
    resetShell();
    Log::init(LogLevel::Info, Log::Mode::Sync);

    Log::write(LogLevel::Error, "motion", "przekroczony limit pradu");

    {
        diag::CrashRecorder before;
        REQUIRE(before.begin().has_value());
        REQUIRE(before.saveLogTail().has_value());
    }

    diag::CrashRecorder after;
    REQUIRE(after.begin().has_value());

    char   tail[256] = {};
    auto   length    = after.loadLogTail(tail, sizeof(tail));
    REQUIRE(length.has_value());
    // Interesuje nas to, co działo się tuż przed awarią.
    CHECK(strstr(tail, "limit pradu") != nullptr);
}
