/** Hydra — implementacja kinematyki napędu różnicowego (rozdz. 9). */

#include "hydra/motion/DifferentialDrive.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/RealMath.hpp"

namespace hydra {
namespace motion {

Status DifferentialDrive::configure(const Config& cfg) {
    if (!(cfg.wheelBase > real(0.0f))) return fail(Err::BadArgument);
    if (!(cfg.maxLinear > real(0.0f))) return fail(Err::BadArgument);
    if (!(cfg.maxAngular > real(0.0f))) return fail(Err::BadArgument);

    cfg_        = cfg;
    configured_ = true;
    return ok();
}

WheelSpeeds DifferentialDrive::toWheelSpeeds(Twist twist) const {
    // v_lewe  = v − ω·b/2
    // v_prawe = v + ω·b/2
    const real_t half = twist.angular * cfg_.wheelBase * real(0.5f);
    return WheelSpeeds{twist.linear - half, twist.linear + half};
}

Twist DifferentialDrive::toTwist(WheelSpeeds wheels) const {
    Twist twist;
    twist.linear  = (wheels.left + wheels.right) * real(0.5f);
    twist.angular = (wheels.right - wheels.left) / cfg_.wheelBase;
    return twist;
}

Twist DifferentialDrive::limit(Twist twist) const {
    Twist limited = twist;

    // Najpierw twarde granice pojedynczych składowych.
    limited.linear  = clamp(limited.linear, -cfg_.maxLinear, cfg_.maxLinear);
    limited.angular = clamp(limited.angular, -cfg_.maxAngular, cfg_.maxAngular);

    // Potem wspólne skalowanie, jeśli któreś koło i tak wyszłoby poza zakres.
    const WheelSpeeds wheels = toWheelSpeeds(limited);
    const real_t      fastest =
        max(abs(wheels.left), abs(wheels.right));

    if (fastest > cfg_.maxLinear) {
        const real_t scale = cfg_.maxLinear / fastest;
        limited.linear  = limited.linear * scale;
        limited.angular = limited.angular * scale;
    }
    return limited;
}

void DifferentialDrive::integrate(real_t leftDistance, real_t rightDistance) {
    const real_t ds     = (leftDistance + rightDistance) * real(0.5f);
    const real_t dtheta = (rightDistance - leftDistance) / cfg_.wheelBase;

    // Kierunek liczony w połowie łuku. Użycie kąta sprzed kroku albo po nim
    // dawałoby błąd rosnący z każdym zakrętem; środek łuku jest przybliżeniem
    // drugiego rzędu i przy krokach rzędu milisekundy praktycznie dokładnym.
    const real_t heading = pose_.theta + dtheta * real(0.5f);

    pose_.x     += ds * cosReal(heading);
    pose_.y     += ds * sinReal(heading);
    pose_.theta  = wrapAngle(pose_.theta + dtheta);
}

void DifferentialDrive::setPose(Pose pose) {
    pose_       = pose;
    pose_.theta = wrapAngle(pose_.theta);
}

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
