#pragma once
/**
 * Hydra — moduł ruchu (rozdz. 9, 4.2).
 *
 * Najbardziej wymagający czasowo element frameworka. Pętla sterowania działa
 * w tasku motion.control z okresem 1–5 ms, o priorytecie czasu rzeczywistego,
 * przypięta do rdzenia 1 — z dala od sieci i interfejsu, które siedzą na
 * rdzeniu 0 (rozdz. 10). Okres egzekwuje delayUntil, więc nie dryfuje wraz
 * z czasem wykonania pętli.
 *
 * Kolejność kroków w cyklu nie jest dowolna:
 *
 *   1. bezpieczeństwo — zanim cokolwiek trafi na silniki,
 *   2. pomiar — enkodery i odometria, także gdy napęd stoi (robot może być
 *      pchany ręcznie i odometria musi to zauważyć),
 *   3. regulacja — PID na prędkości każdego koła osobno,
 *   4. wyjście — moc na mostki.
 *
 * Zadanie prędkości wolno podać z dowolnego taska; przechodzi przez pole
 * chronione sekcją krytyczną, a nie przez kolejkę — pętla czasu rzeczywistego
 * nie może czekać na cokolwiek.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/IModule.hpp"
#include "hydra/core/Task.hpp"
#include "hydra/motion/DifferentialDrive.hpp"
#include "hydra/motion/IEncoder.hpp"
#include "hydra/motion/IMotor.hpp"
#include "hydra/motion/Pid.hpp"
#include "hydra/motion/Safety.hpp"

namespace hydra {
namespace motion {

class MotionModule : public ModuleBase {
public:
    struct Config {
        /** Okres pętli sterowania. Zakres 1–5 ms wg rozdz. 9. */
        u32  periodMs   = 5;
        Prio priority   = Prio::Realtime;
        Core core       = Core::Core1;
        u32  stackWords = HYDRA_DEFAULT_STACK;

        DifferentialDrive::Config drive{};
        WheelOdometer::Config     odometer{};
        SafetyChain::Config       safety{};
        PidR::Gains               gains{};
        PidR::Limits              pidLimits{};
        /** Współczynnik filtru pochodnej regulatora; 1 wyłącza filtrowanie. */
        real_t derivativeFilter = real(0.2f);

        /** Co ile cykli publikowana jest migawka stanu na magistralę. */
        u16 statePeriodCycles = 100;

        /**
         * Udział cykli z przekroczonym deadlinem, powyżej którego napęd
         * przechodzi w zatrzymanie awaryjne. Wyrażony w promilach; kryterium
         * z rozdz. 14 mówi o mniej niż jednym procencie, stąd domyślne 10.
         */
        u16 deadlineLimitPermille = 10;
    };

    struct Stats {
        u32 cycles          = 0;
        u32 deadlineMisses  = 0;
        u32 encoderFaults   = 0;
        u32 commands        = 0;
    };

    MotionModule() : ModuleBase("motion") {}

    Status configure(const Config& cfg);

    void attachMotors(IMotor& left, IMotor& right);
    void attachEncoders(IEncoder& left, IEncoder& right);

    /** Zadaje prędkość pojazdu. Wolno wołać z dowolnego taska. */
    void setTarget(Twist twist);
    Twist target() const;

    /** Ostatnia zmierzona prędkość — z enkoderów, nie z zadania. */
    Twist measured() const { return measured_; }
    Pose  pose() const { return drive_.pose(); }
    void  resetPose() { drive_.resetPose(); }

    SafetyChain&       safety() { return safety_; }
    DifferentialDrive& drive() { return drive_; }
    Stats              stats() const { return stats_; }

    /** Diagnostyka regulatorów — do strojenia i telemetrii. */
    PidR::Diagnostics leftPid() const { return pidLeft_.diagnostics(); }
    PidR::Diagnostics rightPid() const { return pidRight_.diagnostics(); }

    /** Jeden cykl pętli. Wystawiony publicznie na potrzeby testów. */
    void step(Millis now);

protected:
    Status onInit() override;
    Status onStart() override;
    void   onStop() override;

private:
    void stopMotors();
    void publishState();

    Config cfg_{};

    IMotor*   motorLeft_    = nullptr;
    IMotor*   motorRight_   = nullptr;
    IEncoder* encoderLeft_  = nullptr;
    IEncoder* encoderRight_ = nullptr;

    DifferentialDrive drive_;
    WheelOdometer     odoLeft_;
    WheelOdometer     odoRight_;
    SafetyChain       safety_;
    PidR              pidLeft_;
    PidR              pidRight_;
    Task              task_;

    /** Zadanie chronione sekcją krytyczną — pisane spoza pętli. */
    Twist  target_{};
    /** Ustawiane przez setTarget, kasowane w pętli po nakarmieniu watchdoga. */
    bool   commandFresh_ = false;
    /** Stan wyjść, żeby nie powtarzać zapisów i nie zostawić stanu nieznanego. */
    enum class Output : u8 { Unknown = 0, Driving, Stopped };
    Output output_ = Output::Unknown;
    Twist  measured_{};
    Millis lastStep_     = 0;
    u16    stateCounter_ = 0;
    Stats  stats_{};
};

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
