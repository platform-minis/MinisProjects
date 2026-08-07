#pragma once
/**
 * Hydra — aktuatory: silniki i serwa (rozdz. 9).
 *
 * Moc silnika wyrażona jest w promilach ze znakiem, od -1000 do 1000.
 * Ta sama jednostka obowiązuje w HAL (wypełnienie PWM) i w widżecie joysticka,
 * więc droga od dotknięcia ekranu do napięcia na mostku nie zawiera ani
 * jednego przeliczenia jednostek — a każde takie przeliczenie to okazja
 * do pomyłki o rząd wielkości albo o znak.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/Expected.hpp"
#include "hydra/hal/IPwm.hpp"
#include "hydra/hal/Pin.hpp"
#include "hydra/motion/MotionTypes.hpp"

namespace hydra {
namespace motion {

class IMotor {
public:
    virtual ~IMotor() = default;

    virtual Status begin() = 0;

    /** Moc w promilach; znak wyznacza kierunek obrotów. */
    virtual Status setPower(i16 permille) = 0;

    /** Zwarcie uzwojeń — hamowanie dynamiczne, szybkie zatrzymanie. */
    virtual Status brake() = 0;

    /** Rozwarcie uzwojeń — wybieg, wał kręci się swobodnie. */
    virtual Status coast() = 0;

    virtual i16 power() const = 0;
};

/**
 * Silnik na mostku H sterowanym z HAL.
 *
 * Obsługuje oba spotykane sposoby okablowania. DRV8833 i pokrewne przyjmują
 * dwa sygnały PWM (kierunek wynika z tego, który jest aktywny), TB6612 i wiele
 * gotowych modułów — jeden PWM i osobne wejście kierunku.
 */
class HBridgeMotor : public IMotor {
public:
    enum class Wiring : u8 {
        DualPwm,  ///< dwa wejścia PWM, np. DRV8833
        PwmDir,   ///< PWM plus wejście kierunku, np. TB6612
    };

    struct Config {
        Wiring      wiring = Wiring::DualPwm;
        hal::PinNum in1    = hal::kNoPin;  ///< DualPwm: pierwsze wejście
        hal::PinNum in2    = hal::kNoPin;  ///< DualPwm: drugie wejście
        hal::PinNum pwm    = hal::kNoPin;  ///< PwmDir: wejście PWM
        hal::PinNum dir    = hal::kNoPin;  ///< PwmDir: wejście kierunku

        /**
         * Częstotliwość PWM. Domyślne 20 kHz leży powyżej progu słyszalności —
         * niższa daje wyraźny pisk silnika przy małych wypełnieniach.
         */
        u32  freqHz = 20000;
        bool invert = false;  ///< zamienia kierunek bez przekładania przewodów

        /**
         * Najmniejsza moc, przy której wał w ogóle rusza. Zakres [1, 1000]
         * jest odwzorowywany na [deadband, 1000], więc regulator nie traci
         * czasu na wartości, przy których silnik stoi.
         */
        i16 deadband = 0;
    };

    Status configure(const Config& cfg);

    Status begin() override;
    Status setPower(i16 permille) override;
    Status brake() override;
    Status coast() override;
    i16    power() const override { return power_; }

    /** Moc po uwzględnieniu strefy martwej — do diagnostyki. */
    i16 appliedPermille() const { return applied_; }

private:
    Status writeOutputs(i16 permille);
    i16    compensate(i16 permille) const;

    Config cfg_{};
    i16    power_   = 0;
    i16    applied_ = 0;
    bool   ready_   = false;
};

// ---------------------------------------------------------------------------

class IServo {
public:
    virtual ~IServo() = default;
    virtual Status begin() = 0;
    /** Kąt w stopniach w zakresie skonfigurowanym dla serwa. */
    virtual Status setAngle(i16 degrees) = 0;
    virtual i16    angle() const = 0;
};

/** Serwo modelarskie sterowane szerokością impulsu przez HAL. */
class PwmServo : public IServo {
public:
    struct Config {
        hal::PinNum pin      = hal::kNoPin;
        u16         minUs    = hal::kServoMinUs;
        u16         maxUs    = hal::kServoMaxUs;
        i16         minAngle = 0;
        i16         maxAngle = 180;
        u32         freqHz   = hal::kServoFreqHz;
    };

    Status configure(const Config& cfg);
    Status begin() override;
    Status setAngle(i16 degrees) override;
    i16    angle() const override { return angle_; }

    /** Szerokość impulsu odpowiadająca ostatnio zadanemu kątowi. */
    u16 pulseUs() const { return pulseUs_; }

private:
    Config cfg_{};
    i16    angle_   = 0;
    u16    pulseUs_ = 0;
    bool   ready_   = false;
};

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
