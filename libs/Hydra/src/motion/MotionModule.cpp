/** Hydra — implementacja modułu ruchu (rozdz. 9). */

#include "hydra/motion/MotionModule.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("motion")

namespace hydra {
namespace motion {

Status MotionModule::configure(const Config& cfg) {
    if (cfg.periodMs == 0 || cfg.periodMs > 20) return fail(Err::BadArgument);
    if (cfg.statePeriodCycles == 0) return fail(Err::BadArgument);

    cfg_ = cfg;
    return ok();
}

void MotionModule::attachMotors(IMotor& left, IMotor& right) {
    motorLeft_  = &left;
    motorRight_ = &right;
}

void MotionModule::attachEncoders(IEncoder& left, IEncoder& right) {
    encoderLeft_  = &left;
    encoderRight_ = &right;
}

void MotionModule::setTarget(Twist twist) {
    const Twist limited = drive_.limit(twist);
    {
        // Sekcja krytyczna zamiast kolejki: pętla czasu rzeczywistego nie może
        // czekać na przetworzenie polecenia, a zapis dwóch liczb trwa tyle,
        // co kilka instrukcji.
        //
        // Watchdoga komend karmi dopiero pętla, w swoim własnym czasie.
        // Sięgnięcie tutaj po zegar dałoby modułowi dwa niezależne źródła
        // czasu — a wtedy zachowanie zależałoby od tego, które z nich akurat
        // wyprzedza drugie.
        rtos::CriticalSection cs;
        target_       = limited;
        commandFresh_ = true;
    }
    ++stats_.commands;
}

Twist MotionModule::target() const {
    rtos::CriticalSection cs;
    return target_;
}

Status MotionModule::onInit() {
    if (!motorLeft_ || !motorRight_) return fail(Err::NotInitialized);

    HYDRA_CHECK(drive_.configure(cfg_.drive));
    HYDRA_CHECK(odoLeft_.configure(cfg_.odometer));
    HYDRA_CHECK(odoRight_.configure(cfg_.odometer));
    HYDRA_CHECK(safety_.configure(cfg_.safety));

    pidLeft_.setGains(cfg_.gains);
    pidRight_.setGains(cfg_.gains);
    pidLeft_.setLimits(cfg_.pidLimits);
    pidRight_.setLimits(cfg_.pidLimits);
    pidLeft_.setDerivativeFilter(cfg_.derivativeFilter);
    pidRight_.setDerivativeFilter(cfg_.derivativeFilter);

    HYDRA_CHECK(motorLeft_->begin());
    HYDRA_CHECK(motorRight_->begin());

    if (encoderLeft_) HYDRA_CHECK(encoderLeft_->begin());
    if (encoderRight_) HYDRA_CHECK(encoderRight_->begin());

    HYDRA_LOGI("napęd różnicowy: rozstaw %d mm, okres pętli %lu ms",
               static_cast<int>(toFloat(cfg_.drive.wheelBase) * 1000.0f),
               static_cast<unsigned long>(cfg_.periodMs));
    return ok();
}

Status MotionModule::onStart() {
    Task::Cfg cfg;
    cfg.name          = "motion.control";
    cfg.prio          = cfg_.priority;
    cfg.core          = cfg_.core;
    cfg.stackWords    = cfg_.stackWords;
    // Pętla czasu rzeczywistego zgłasza naruszenie po pierwszym spóźnieniu:
    // przy okresie kilku milisekund każde jest istotne.
    cfg.missThreshold = 1;

    lastStep_ = 0;
    return task_.startPeriodic(cfg, cfg_.periodMs, [this] { step(rtos::nowMs()); });
}

void MotionModule::onStop() {
    task_.stopAndWait();
    stopMotors();
}

void MotionModule::stopMotors() {
    // Także ze stanu nieznanego: po starcie wyjścia mostka nie są w żadnym
    // określonym stanie, a pojazd na pochyłości stoczyłby się na wybiegu.
    if (output_ == Output::Stopped) return;
    output_ = Output::Stopped;

    // Hamowanie, nie wybieg: pojazd, któremu odcięto sterowanie, ma stanąć,
    // a nie toczyć się dalej.
    if (motorLeft_) motorLeft_->brake();
    if (motorRight_) motorRight_->brake();

    pidLeft_.reset();
    pidRight_.reset();
}

void MotionModule::publishState() {
    const Pose pose = drive_.pose();
    EventBus::publish(MotionState{toFloat(pose.x), toFloat(pose.y), toFloat(pose.theta),
                                  toFloat(measured_.linear), toFloat(measured_.angular),
                                  static_cast<u8>(safety_.state())});
}

void MotionModule::step(Millis now) {
    ++stats_.cycles;

    // Krok całkowania z rzeczywistego odstępu, nie z okresu nominalnego:
    // pojedyncze spóźnienie pętli inaczej zafałszowałoby prędkość i odometrię.
    real_t dt = real(static_cast<float>(cfg_.periodMs) / 1000.0f);
    if (lastStep_ != 0 && now > lastStep_) {
        dt = real(static_cast<float>(now - lastStep_) / 1000.0f);
    }
    lastStep_ = now;

    // 1. Bezpieczeństwo — zanim cokolwiek trafi na silniki. Świeże zadanie
    //    karmi watchdoga w czasie pętli, nie w czasie nadawcy.
    {
        rtos::CriticalSection cs;
        if (commandFresh_) {
            commandFresh_ = false;
            safety_.feedCommand(now);
        }
    }
    SafetyState safety = safety_.evaluate(now);

    // 2. Pomiar. Wykonywany także przy zatrzymanym napędzie: pojazd może być
    //    pchany ręcznie i odometria musi to zauważyć.
    WheelSpeeds measuredWheels{};
    if (encoderLeft_ && encoderRight_) {
        auto left  = encoderLeft_->counts();
        auto right = encoderRight_->counts();

        if (left && right) {
            const real_t dl = odoLeft_.advance(*left);
            const real_t dr = odoRight_.advance(*right);
            drive_.integrate(dl, dr);

            if (dt > real(0.0f)) {
                measuredWheels.left  = dl / dt;
                measuredWheels.right = dr / dt;
            }
            measured_ = drive_.toTwist(measuredWheels);
        } else {
            ++stats_.encoderFaults;
            // Utrata odczytu enkodera przy jeździe oznacza sterowanie
            // w ciemno — to powód do zatrzymania, nie do kontynuowania.
            if (output_ == Output::Driving) safety_.emergencyStop(StopReason::EncoderFault);
        }
    }

    // Ocena powtórzona po pomiarze: awaria wykryta przed chwilą — utrata
    // enkodera, przekroczony prąd — musi zadziałać jeszcze w tym cyklu.
    // Odłożenie jej do następnego oznaczałoby jeden okres jazdy na ślepo.
    if (safety == SafetyState::Ready) safety = safety_.evaluate(now);

    if (safety != SafetyState::Ready) {
        stopMotors();
        if (++stateCounter_ >= cfg_.statePeriodCycles) {
            stateCounter_ = 0;
            publishState();
        }
        return;
    }

    // 3. Regulacja. Osobny regulator na koło — różnice w tarciu i obciążeniu
    //    między stronami są na tyle duże, że wspólny nie utrzymałby toru.
    Twist goal;
    {
        rtos::CriticalSection cs;
        goal = target_;
    }
    const WheelSpeeds wanted = drive_.toWheelSpeeds(goal);

    i16 leftPower  = 0;
    i16 rightPower = 0;

    if (encoderLeft_ && encoderRight_) {
        const real_t l = pidLeft_.update(wanted.left, measuredWheels.left, dt);
        const real_t r = pidRight_.update(wanted.right, measuredWheels.right, dt);
        leftPower  = static_cast<i16>(toFloat(l));
        rightPower = static_cast<i16>(toFloat(r));
    } else {
        // Bez enkoderów regulacja nie ma czego mierzyć — jedziemy w otwartej
        // pętli, przeliczając zadanie wprost na moc.
        const real_t scale = real(static_cast<float>(kMaxPower)) / cfg_.drive.maxLinear;
        leftPower  = static_cast<i16>(toFloat(wanted.left * scale));
        rightPower = static_cast<i16>(toFloat(wanted.right * scale));
    }

    // 4. Wyjście.
    output_ = Output::Driving;
    if (motorLeft_) motorLeft_->setPower(leftPower);
    if (motorRight_) motorRight_->setPower(rightPower);

    if (++stateCounter_ >= cfg_.statePeriodCycles) {
        stateCounter_ = 0;
        publishState();
    }

    // Kontrola dotrzymywania terminów. Licznik naruszeń prowadzi Task;
    // moduł jedynie ocenia, czy ich udział mieści się w założeniu z rozdz. 14.
    const auto taskStats = task_.stats();
    stats_.deadlineMisses = taskStats.deadlineMisses;
    if (cfg_.deadlineLimitPermille > 0 && taskStats.iterations > 100) {
        const u32 permille = taskStats.deadlineMisses * 1000u / taskStats.iterations;
        if (permille > cfg_.deadlineLimitPermille) {
            HYDRA_LOGE("pętla nie dotrzymuje terminów: %lu‰ naruszeń",
                       static_cast<unsigned long>(permille));
            safety_.emergencyStop(StopReason::DeadlineMissed);
        }
    }
}

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
