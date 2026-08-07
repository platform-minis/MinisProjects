/**
 * rover — napęd różnicowy z pętlą czasu rzeczywistego.
 *
 * Najważniejsza rzecz w tym pliku nie jest widoczna na pierwszy rzut oka:
 * ani jednej liczby opisującej wyprowadzenie. `Pin::MotorLeftA1` pochodzi
 * z `boards/rover_s3.hpp`, który powstaje ze schematu — przepięcie sygnału
 * na inną nóżkę nie wymaga tknięcia tego kodu.
 *
 * Druga rzecz to pętla napędu: 2 ms, własny rdzeń, jawny termin. Gdy zaczyna
 * się spóźniać, framework to zgłasza i po dziesiątym spóźnieniu przechodzi
 * w tryb ograniczony — bo łazik jadący na ślepo jest gorszy niż wolniejszy.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST
#  include <Arduino.h>
#endif

#include <Hydra.h>

#include "hydra/core/LogSinks.hpp"
#include "hydra/hal/Board.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("rover")

using namespace hydra;

namespace {

UartLogSink gConsole;

/** Ile spóźnień z rzędu zanim uznamy, że pętla nie nadąża — jak w pliku projektu. */
constexpr u32 kMissLimit = 10;

/**
 * Moduł napędu. W pełnym projekcie siedzi tu `MotionModule` z regulatorami
 * i odometrią; tutaj zostaje sama pętla, żeby widać było jej kształt:
 * stały okres, pomiar terminu i reakcja na jego przekroczenie.
 */
class DriveModule : public ModuleBase {
public:
    DriveModule() : ModuleBase("drive") {}

protected:
    Status onInit() override {
        auto& gpio = hal::Hal::gpio();
        HYDRA_CHECK(gpio.configure(Pin::MotorLeftA1, hal::PinMode::Output));
        HYDRA_CHECK(gpio.configure(Pin::MotorLeftA2, hal::PinMode::Output));
        HYDRA_CHECK(gpio.configure(Pin::MotorRightB1, hal::PinMode::Output));
        HYDRA_CHECK(gpio.configure(Pin::MotorRightB2, hal::PinMode::Output));

        // Zatrzymanie awaryjne z podciągnięciem: przerwany przewód daje stan
        // aktywny, czyli stop. Odwrotne podłączenie oznaczałoby, że urwany
        // przewód wyłącza zabezpieczenie.
        return gpio.configure(Pin::EStop, hal::PinMode::InputPullUp);
    }

    Status onStart() override {
        Task::Cfg cfg;
        cfg.name = "motion.control";
        cfg.prio = Prio::Realtime;
        cfg.core = Core::Core1;          // własny rdzeń — sieć jej nie opóźni
        cfg.missThreshold = kMissLimit;
        return task_.startPeriodic(cfg, 2, [this] { step(); });
    }

    void onStop() override {
        task_.stopAndWait();
        stopMotors();
    }

private:
    void step() {
        if (!hal::Hal::gpio().read(Pin::EStop)) {   // stan niski = wciśnięty
            stopMotors();
            return;
        }
        // Tu w prawdziwym urządzeniu liczy się regulacja prędkości kół.
        // Kształt pętli jest ważniejszy od jej zawartości: stały okres,
        // brak alokacji, brak operacji, które mogą się zablokować.
    }

    void stopMotors() {
        auto& gpio = hal::Hal::gpio();
        for (const auto pin : { Pin::MotorLeftA1, Pin::MotorLeftA2,
                                Pin::MotorRightB1, Pin::MotorRightB2 }) {
            gpio.write(pin, false);
        }
    }

    Task task_;
};

DriveModule gDrive;

}  // namespace

void setup() {
    App::config()
        .name("rover-01")
        .logLevel(LogLevel::Info)
        .logSink(gConsole)
        .add(gDrive);

    if (auto r = App::begin(); !r) {
        HYDRA_LOGE("start nieudany: %s", toString(r.error()));
        return;
    }

    // Przekroczony termin pętli nie jest ciszą: framework mówi o tym zdarzeniem,
    // a aplikacja decyduje, co z tym zrobić.
    EventBus::subscribe<TaskDeadlineMissed>([](const TaskDeadlineMissed& e) {
        HYDRA_LOGW("pętla spóźniona o %ums (razem %lu)",
                   e.overrunMs, static_cast<unsigned long>(e.totalMisses));
    });

    HYDRA_LOGI("łazik na płytce %s, dioda %d, e-stop %d",
               hal::board::name, static_cast<int>(Pin::StatusLed), static_cast<int>(Pin::EStop));
}

void loop() {}
