/**
 * Hydra — przykład: lua-script.
 *
 * Logika urządzenia napisana w Lua zamiast w C++, z zachowaniem wszystkich
 * gwarancji frameworka. Ten sam plik kompiluje się bez zmian na ESP32-S3,
 * ESP32-C3, RP2040, RP2350 i STM32 — łącznie ze skryptem, bo skrypt sięga po
 * sprzęt przez HAL, a nie przez API konkretnej platformy.
 *
 * Pokazuje cztery rzeczy, dla których osadza się interpreter:
 *
 *   1. **Logika bez przekompilowania.** Progi, histereza i tempo migania
 *      siedzą w tekście skryptu. Zmiana zachowania to zmiana tego tekstu,
 *      a nie cykl kompilacja–flashowanie.
 *   2. **Własne funkcje z C++.** `board.temperature()` jest funkcją natywną —
 *      skrypt woła ją tak samo jak każdą inną, nie wiedząc, że pod spodem
 *      jest kod C++.
 *   3. **Dwustronna magistrala zdarzeń.** Skrypt zgłasza `alarm` przez
 *      `hydra.event.emit`, a moduł C++ ten sygnał odbiera. W drugą stronę
 *      działa tak samo.
 *   4. **Skrypt nie zawiesza urządzenia.** Funkcja `loop()` dostaje budżet
 *      instrukcji. Nawet nieskończona pętla w skrypcie nie zatrzyma ani
 *      pętli sterowania, ani sieci — zostanie wywłaszczona.
 *
 * Wymaga `-D HYDRA_ENABLE_SCRIPT=1` w build_flags.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
// Deklaracje setup() i loop(). Potrzebne, bo STM32duino umieszcza je w bloku
// extern "C" — bez tej deklaracji definicje poniżej dostają wiązanie C++
// i konsolidator ich nie znajduje.
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/script/ScriptModule.hpp"

HYDRA_LOG_MODULE("demo");

using namespace hydra;

namespace {

// ---------------------------------------------------------------------------
// Skrypt urządzenia
// ---------------------------------------------------------------------------

/**
 * Źródło leży w pamięci programu, więc nie zajmuje RAM-u poza tym, co
 * interpreter zużyje na skompilowany kod bajtowy. W prawdziwym urządzeniu
 * przyszłoby z karty SD, z pamięci trwałej albo przez OTA — `ScriptModule`
 * przyjmuje dowolny wskaźnik na tekst, który przeżyje moduł.
 */
const char kScript[] = R"LUA(
-- Sterowanie diodą z histerezą, w całości po stronie skryptu.

local PROG_ZAL   = 30.0   -- °C — powyżej tego włącz alarm
local PROG_WYL   = 25.0   -- °C — poniżej tego wyłącz (histereza)
local OKRES_MIG  = 4      -- co ile przebiegów zmienić stan diody

local alarm   = false
local licznik = 0
local stanLed = false

function setup()
  hydra.log.info("skrypt wystartowal na", hydra.platform, "hydra", hydra.version)
  if board.ledPin() >= 0 then
    hydra.gpio.mode(board.ledPin(), "out")
  end
  -- Skrypt nasłuchuje też tego, co dzieje się po stronie C++.
  hydra.event.on("kalibracja", function(wartosc)
    PROG_ZAL = wartosc
    PROG_WYL = wartosc - 5.0
    hydra.log.info(string.format("nowe progi: %.1f / %.1f", PROG_ZAL, PROG_WYL))
  end)
end

function loop()
  local t = board.temperature()

  -- Histereza: dwa różne progi, żeby przy wartości drgającej wokół jednego
  -- alarm nie przełączał się co przebieg.
  if not alarm and t > PROG_ZAL then
    alarm = true
    hydra.log.warn(string.format("przekroczony prog: %.1f C", t))
    hydra.event.emit("alarm", t, 1)
  elseif alarm and t < PROG_WYL then
    alarm = false
    hydra.log.info(string.format("powrot do normy: %.1f C", t))
    hydra.event.emit("alarm", t, 0)
  end

  -- Dioda miga tylko w stanie alarmu.
  licznik = licznik + 1
  if alarm and licznik % OKRES_MIG == 0 then
    stanLed = not stanLed
    if board.ledPin() >= 0 then
      hydra.gpio.write(board.ledPin(), stanLed)
    end
  end
end
)LUA";

// ---------------------------------------------------------------------------
// Funkcje natywne udostępnione skryptowi
// ---------------------------------------------------------------------------

/**
 * Odczyt temperatury. Tutaj symulowany, bo przykład ma się zbudować na każdej
 * płytce — w prawdziwym urządzeniu byłby to odczyt z `sense::SensorHub`.
 */
int boardTemperature(script::Ctx& c) {
    const u32   ms   = rtos::nowMs();
    const float fala = 27.0f + 5.0f * static_cast<float>((ms / 1000) % 8) / 4.0f;
    c.pushNumber(fala);
    return 1;
}

/** Numer pinu diody z pliku płytki. Skrypt nie zna pinoutu — zna nazwę. */
int boardLedPin(script::Ctx& c) {
    c.pushInt(hal::board::led);
    return 1;
}

const script::Reg kBoardApi[] = {
    {"temperature", boardTemperature},
    {"ledPin",      boardLedPin},
    {nullptr,       nullptr},
};

// ---------------------------------------------------------------------------
// Moduł aplikacji
// ---------------------------------------------------------------------------

/**
 * Moduł skryptowy z dołożoną własną biblioteką `board`.
 *
 * Dziedziczenie po `ScriptModule` jest tu po to, żeby wpiąć się między
 * otwarcie interpretera a wczytanie skryptu — funkcje natywne muszą istnieć,
 * zanim skrypt je zawoła.
 */
class DemoScript : public script::ScriptModule {
public:
    DemoScript() {
        Config cfg;
        cfg.source   = kScript;
        cfg.periodMs = 250;
        // Budżet instrukcji na przebieg. Ten skrypt zużywa ich kilkaset,
        // więc zapas jest kilkudziesięciokrotny — a mimo to pętla bez wyjścia
        // zostałaby wywłaszczona.
        cfg.budget        = 20000;
        cfg.bindings.gpio = true;
        configure(cfg);
    }

protected:
    Status onInit() override {
        HYDRA_CHECK(ScriptModule::onInit());
        // Uwaga na kolejność: `ScriptModule::onInit()` wczytuje skrypt i woła
        // `setup()`, więc biblioteka `board` musi trafić do interpretera
        // wcześniej. Rejestrujemy ją przed wczytaniem, przeładowując skrypt.
        HYDRA_CHECK(interp().registerLib("board", kBoardApi));
        return reload();
    }
};

DemoScript  gScript;
UartLogSink gConsole;

}  // namespace

void setup() {
    App::config()
        .name("lua-demo")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .housekeepingMs(5000)
        .add(gScript);

    // Sygnał ze skryptu odebrany po stronie C++ — dokładnie tak samo, jak
    // odbiera się zdarzenia dowolnego innego modułu.
    EventBus::subscribe<script::ScriptSignal>([](const script::ScriptSignal& s) {
        if (s.nameId != nameId("alarm")) return;
        HYDRA_LOGI("skrypt zglosil alarm=%d przy %d.%d C", static_cast<int>(s.data),
                   static_cast<int>(s.value),
                   static_cast<int>(s.value * 10) % 10);
    });

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }

    // Progi da się zmienić z C++ bez dotykania skryptu — skrypt nasłuchuje
    // sygnału "kalibracja" i sam przelicza histerezę.
    EventBus::publish(script::ScriptSignal{nameId("kalibracja"), 31.0f, 0});
}

void loop() {
    // Nieużywane — cała praca dzieje się w taskach.
}
