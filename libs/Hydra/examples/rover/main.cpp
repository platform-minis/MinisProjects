/**
 * Hydra — przykład: rover.
 *
 * Kryterium ukończenia etapu M5 (rozdz. 14): rover jedzie z zadaną prędkością,
 * a pętla sterowania dotrzymuje terminów w ponad 99% cykli.
 *
 * Pokazuje pełny łańcuch: zadanie prędkości → kinematyka napędu różnicowego →
 * regulator na każde koło → mostki H, z odometrią z enkoderów w drugą stronę
 * i łańcuchem bezpieczeństwa nad całością.
 *
 * Piny pochodzą z pliku płytki rover_s3.hpp — aplikacja mówi Pin::MotorLeftPwm,
 * nigdy „GPIO 17".
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
// Deklaracje setup() i loop(). Potrzebne, bo STM32duino umieszcza je w bloku
// extern "C" — bez tej deklaracji definicje poniżej dostają wiązanie C++
// i konsolidator ich nie znajduje. Na ESP32 i RP2040 deklaracje są zwykłe,
// więc włączenie niczego nie zmienia.
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include <stdio.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"
#include "hydra/motion/MotionModule.hpp"

HYDRA_LOG_MODULE("rover")

using namespace hydra;
using namespace hydra::motion;

namespace {

// --- sprzęt ----------------------------------------------------------------

HBridgeMotor      gLeftMotor;
HBridgeMotor      gRightMotor;
QuadratureEncoder gLeftEncoder;
QuadratureEncoder gRightEncoder;

MotionModule gMotion;
UartLogSink  gConsole;

/** Parametry mechaniczne pojazdu — jedyne miejsce, gdzie się pojawiają. */
constexpr float kWheelRadiusM     = 0.033f;  // koło 66 mm
constexpr float kWheelBaseM       = 0.145f;  // rozstaw kół
constexpr i32   kCountsPerRev     = 1440;    // enkoder 360 impulsów × 4 × przekładnia

Status setUpHardware() {
    HBridgeMotor::Config left;
    left.wiring = HBridgeMotor::Wiring::PwmDir;
    left.pwm    = Pin::MotorLeftPwm;
    left.dir    = Pin::MotorLeftDir;
    // Poniżej około 12% wypełnienia ten napęd nie rusza — regulator nie ma
    // powodu marnować na ten zakres części swojego wyjścia.
    left.deadband = 120;

    HBridgeMotor::Config right = left;
    right.pwm    = Pin::MotorRightPwm;
    right.dir    = Pin::MotorRightDir;
    right.invert = true;  // silnik po drugiej stronie kręci się przeciwnie

    HYDRA_CHECK(gLeftMotor.configure(left));
    HYDRA_CHECK(gRightMotor.configure(right));

    QuadratureEncoder::Config encLeft;
    encLeft.a = Pin::EncoderLeftA;
    encLeft.b = Pin::EncoderLeftB;

    QuadratureEncoder::Config encRight = encLeft;
    encRight.a      = Pin::EncoderRightA;
    encRight.b      = Pin::EncoderRightB;
    encRight.invert = true;

    HYDRA_CHECK(gLeftEncoder.configure(encLeft));
    return gRightEncoder.configure(encRight);
}

MotionModule::Config motionConfig() {
    MotionModule::Config cfg;
    // 5 ms to górna granica zakresu z rozdz. 9 — wystarczająco szybko dla
    // napędu kołowego, a zostawia zapas czasu procesora na resztę systemu.
    cfg.periodMs = 5;

    cfg.drive.wheelBase  = real(kWheelBaseM);
    cfg.drive.maxLinear  = real(0.6f);
    cfg.drive.maxAngular = real(4.0f);

    cfg.odometer.countsPerRevolution = kCountsPerRev;
    cfg.odometer.wheelRadius         = real(kWheelRadiusM);

    // Regulator prędkości koła: wyjście w promilach mocy, wejście w m/s.
    // Wzmocnienie proporcjonalne dobrane tak, by pełny uchyb prędkości
    // dawał pełną moc; całkujące likwiduje uchyb od tarcia i pochylenia.
    cfg.gains.kp = real(1600.0f);
    cfg.gains.ki = real(2400.0f);
    cfg.gains.kd = real(20.0f);
    cfg.pidLimits.outMin      = real(-1000.0f);
    cfg.pidLimits.outMax      = real(1000.0f);
    cfg.pidLimits.integralMax = real(800.0f);
    cfg.derivativeFilter      = real(0.15f);

    // Brak nowej komendy przez pół sekundy zatrzymuje pojazd. To zabezpieczenie
    // przed zerwaniem łącza, nie wygoda — bez niego robot jedzie dalej.
    cfg.safety.commandTimeoutMs = 500;
    // Limit prądu z pomiaru INA219; zwłoka przepuszcza prąd rozruchowy.
    cfg.safety.currentLimitMa     = 2500;
    cfg.safety.overCurrentGraceMs = 150;

    cfg.statePeriodCycles = 40;  // migawka stanu dwa razy na sekundę
    return cfg;
}

}  // namespace

void setup() {
    if (auto r = setUpHardware(); !r) {
        HYDRA_LOGE("konfiguracja sprzętu nieudana: %s", toString(r.error()));
        return;
    }

    gMotion.attachMotors(gLeftMotor, gRightMotor);
    gMotion.attachEncoders(gLeftEncoder, gRightEncoder);
    gMotion.configure(motionConfig());

    App::config()
        .name("rover-01")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .housekeepingMs(1000)
        .add(gMotion);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }

    // Enkodery zgłaszają się przerwaniem; dekodowanie sprzętowe (PCNT na ESP32,
    // PIO na RP2) podmienia się tu na inną implementację IEncoder bez zmian
    // w pozostałym kodzie.
    gLeftEncoder.attachInterrupts();
    gRightEncoder.attachInterrupts();

    // Podgląd stanu napędu. Kod ekranu ani telemetrii nie musi nic wiedzieć
    // o regulatorach — dostaje gotową migawkę.
    EventBus::subscribe<MotionState>([](const MotionState& s) {
        HYDRA_LOGI("poz. %.2f/%.2f m, kąt %.2f rad, v=%.2f m/s, stan %s",
                   static_cast<double>(s.x), static_cast<double>(s.y),
                   static_cast<double>(s.theta), static_cast<double>(s.linear),
                   toString(static_cast<SafetyState>(s.safety)));
    });

    EventBus::subscribe<SafetyChanged>([](const SafetyChanged& e) {
        HYDRA_LOGW("bezpieczeństwo: %s → %s", toString(e.from), toString(e.to));
    });

    // Zatrzymanie awaryjne z przycisku. Handler przerwania ustawia wyłącznie
    // flagę — pętla sterowania zauważy ją w następnym cyklu, czyli po
    // najwyżej pięciu milisekundach.
    hal::Hal::gpio().configure(Pin::EStopButton, hal::PinMode::InputPullUp);
    hal::Hal::gpio().attachInterrupt(
        Pin::EStopButton, hal::Edge::Falling,
        [](void* arg) {
            static_cast<MotionModule*>(arg)->safety().emergencyStop(StopReason::Operator);
        },
        &gMotion);

    // Zadanie prędkości. W docelowym urządzeniu przyszłoby z joysticka
    // ekranowego albo komendą MQTT — tu na sztywno, żeby pokazać ścieżkę.
    Twist target;
    target.linear = real(0.25f);
    gMotion.setTarget(target);

    HYDRA_LOGI("rover gotowy: rozstaw %d mm, koło %d mm",
               static_cast<int>(kWheelBaseM * 1000), static_cast<int>(kWheelRadiusM * 2000));
}

void loop() {
    // Zadanie trzeba odnawiać, inaczej watchdog komend zatrzyma pojazd.
    // W prawdziwym urządzeniu robi to źródło komend, nie pętla główna.
    Twist target;
    target.linear = real(0.25f);
    gMotion.setTarget(target);
    rtos::delayMs(100);
}
