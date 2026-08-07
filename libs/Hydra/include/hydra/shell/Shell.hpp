#pragma once
/**
 * Hydra — shell diagnostyczny (rozdz. 13).
 *
 * Mini-interpreter komend dostępny przez UART, USB albo — docelowo —
 * WebSocket. Jego wartość nie polega na wygodzie, tylko na tym, że daje
 * wgląd w stan działającego urządzenia bez debuggera i bez przerywania
 * pracy: `ps` pokazuje taski i zapas ich stosów, `i2c scan` weryfikuje
 * okablowanie, `cfg` zmienia konfigurację bez przekompilowania.
 *
 * To także jedyny interfejs, przez który testy sprzętowe w CI mogą sterować
 * urządzeniem i odczytywać wynik — stąd wymóg, by wyjście dało się
 * przetworzyć maszynowo, a nie tylko przeczytać.
 *
 * Shell nie alokuje i nie zna żadnego modułu. Komendy rejestrują się same,
 * więc build bez modułu sieciowego nie ma komend sieciowych — i nie ma po nich
 * ani bajta (rozdz. 1).
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"

namespace hydra {
namespace shell {

/** Maksymalna liczba zarejestrowanych komend. */
#ifndef HYDRA_SHELL_MAX_COMMANDS
#  define HYDRA_SHELL_MAX_COMMANDS 24
#endif
/** Długość bufora edytowanego wiersza. */
#ifndef HYDRA_SHELL_LINE_MAX
#  define HYDRA_SHELL_LINE_MAX 96
#endif
/** Maksymalna liczba argumentów komendy wraz z jej nazwą. */
#ifndef HYDRA_SHELL_MAX_ARGS
#  define HYDRA_SHELL_MAX_ARGS 8
#endif

/** Strumień wyjściowy komendy. */
class Output {
public:
    using Sink = Delegate<void(const char*, size_t)>;

    explicit Output(Sink sink) : sink_(sink) {}

    void write(const char* text);
    void writeLine(const char* text = "");
    /** Formatowanie z ograniczeniem długości wiersza. */
    void printf(const char* format, ...);

    /**
     * Wiersz w postaci klucz=wartość. Wyjście shella czytają też testy
     * sprzętowe w CI, więc obok formy czytelnej dla człowieka potrzebna jest
     * taka, którą da się rozebrać jednym wyrażeniem.
     */
    void field(const char* key, const char* value);
    void field(const char* key, i32 value);
    void field(const char* key, u32 value);

    size_t bytesWritten() const { return written_; }

private:
    Sink   sink_;
    size_t written_ = 0;
};

class Shell {
public:
    /** Handler komendy. argv[0] to nazwa komendy. */
    using Handler = Delegate<Status(int, char**, Output&)>;

    struct Command {
        const char* name = nullptr;
        const char* help = nullptr;
        Handler     handler{};
    };

    /** Ustawia odbiornik wyjścia — zwykle port szeregowy. */
    void setOutput(Output::Sink sink) { sink_ = sink; }

    /** Rejestruje komendę. Nazwa i opis muszą przeżyć shell. */
    Status add(const char* name, const char* help, Handler handler);

    u8             count() const { return count_; }
    const Command* command(u8 index) const;
    const Command* find(const char* name) const;

    /**
     * Rozbiera wiersz i wykonuje komendę.
     * Err::NotFound oznacza nieznaną komendę, a nie błąd wykonania.
     */
    Status execute(const char* line);

    /**
     * Podaje znak z portu. Po znaku końca wiersza wykonuje komendę.
     * Obsługuje cofanie i odrzuca znaki sterujące.
     */
    void feed(char c);
    /** Podaje ciąg znaków. */
    void feed(const char* text);

    /** Wypisuje zachętę do wprowadzenia komendy. */
    void prompt();

    /** Czy w buforze jest niezakończony wiersz. */
    bool editing() const { return length_ > 0; }
    void clearLine();

    struct Stats {
        u32 executed = 0;
        u32 unknown  = 0;
        u32 failed   = 0;
        u32 overflow = 0;  ///< wiersze dłuższe niż bufor
    };
    Stats stats() const { return stats_; }

private:
    Output makeOutput() { return Output(sink_); }

    Output::Sink sink_{};
    Command      commands_[HYDRA_SHELL_MAX_COMMANDS];
    u8           count_ = 0;

    char   line_[HYDRA_SHELL_LINE_MAX] = {};
    size_t length_    = 0;
    bool   truncated_ = false;
    Stats  stats_{};
};

/** Rejestruje komendy rdzenia: help, ps, top, log, reboot, uptime. */
Status registerCoreCommands(Shell& shell);

/** Rejestruje komendy warstwy sprzętowej: i2c, gpio, adc, cfg. */
Status registerHalCommands(Shell& shell);

}  // namespace shell
}  // namespace hydra
