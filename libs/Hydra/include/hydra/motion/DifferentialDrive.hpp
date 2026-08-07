#pragma once
/**
 * Hydra — kinematyka napędu różnicowego i odometria (rozdz. 9).
 *
 * Napęd różnicowy to dwa niezależnie napędzane koła na wspólnej osi. Zamiana
 * prędkości pojazdu na prędkości kół i z powrotem sprowadza się do dwóch
 * równań, ale to odometria decyduje o tym, czy robot wie, gdzie jest.
 *
 * Całkowanie po łuku, nie po prostej: w jednym kroku pętli pojazd zakreśla
 * łuk, a nie odcinek. Przy 1 ms różnica jest znikoma, ale kumuluje się —
 * i to właśnie ona odpowiada za systematyczne odchylenie toru przy jeździe
 * po okręgu, które przy prostoliniowym przybliżeniu potrafi sięgnąć
 * kilkunastu procent obwodu.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/Expected.hpp"
#include "hydra/motion/MotionTypes.hpp"

namespace hydra {
namespace motion {

class DifferentialDrive {
public:
    struct Config {
        /** Rozstaw kół w metrach — odległość między punktami styku. */
        real_t wheelBase = real(0.15f);
        /** Największa prędkość liniowa, jaką napęd jest w stanie osiągnąć. */
        real_t maxLinear = real(1.0f);
        /** Największa prędkość kątowa. */
        real_t maxAngular = real(3.0f);
    };

    Status configure(const Config& cfg);
    const Config& config() const { return cfg_; }

    /** Prędkość pojazdu na prędkości kół. */
    WheelSpeeds toWheelSpeeds(Twist twist) const;

    /** Prędkości kół na prędkość pojazdu — używane do pomiaru. */
    Twist toTwist(WheelSpeeds wheels) const;

    /**
     * Ogranicza zadanie do możliwości napędu.
     *
     * Przy nadmiernym zadaniu obie prędkości kół są skalowane wspólnym
     * współczynnikiem, a nie przycinane osobno. Przycięcie każdej z osobna
     * zmieniłoby stosunek między nimi, czyli promień skrętu — robot pojechałby
     * w inną stronę, niż mu kazano, zamiast po prostu wolniej.
     */
    Twist limit(Twist twist) const;

    /** Całkuje odometrię z dróg przebytych przez oba koła w jednym kroku. */
    void integrate(real_t leftDistance, real_t rightDistance);

    Pose pose() const { return pose_; }
    void setPose(Pose pose);
    void resetPose() { setPose(Pose{}); }

private:
    Config cfg_{};
    Pose   pose_{};
    bool   configured_ = false;
};

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
