#pragma once
/**
 * Hydra — fuzja IMU jako czujnik wirtualny (rozdz. 8).
 *
 * Nie dotyka magistrali: subskrybuje próbki akcelerometru i żyroskopu
 * z EventBusa, a publikuje kwaternion orientacji. Dzięki temu jest zwykłym
 * ISensor — hub obsługuje go tak samo jak układ fizyczny, z tą samą
 * kalibracją, filtracją i detekcją anomalii.
 *
 * Filtr komplementarny, nie Madgwick: przy jednym mnożeniu na oś daje na
 * tych platformach wynik nieodróżnialny w zastosowaniach robotycznych,
 * a mieści się w budżecie czasu pętli także na RP2040 bez FPU. Madgwick
 * pozostaje na etap M6, gdy pojawią się magnetometr i korekta kursu.
 *
 * Krok całkowania bierze się z różnicy znaczników czasu kolejnych próbek —
 * dlatego poprawne stemplowanie w SensorHub jest warunkiem sensownej fuzji,
 * a nie kosmetyką telemetrii.
 *
 * Jednostki wejściowe: akcelerometr w g, żyroskop w stopniach na sekundę.
 * Kanały wyjściowe: [0] w, [1] x, [2] y, [3] z kwaternionu.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/core/EventBus.hpp"
#include "hydra/sense/ISensor.hpp"

namespace hydra {
namespace sense {

class ImuFusion : public ISensor {
public:
    struct Setup {
        /**
         * Waga żyroskopu w filtrze komplementarnym. Bliżej 1 — mniej szumu
         * z akcelerometru, ale szybszy dryf; 0,98 to typowy punkt pracy.
         */
        float alpha = 0.98f;
        /** Maksymalny krok całkowania. Chroni przed skokiem po zawieszeniu taska. */
        float maxDtSec = 0.5f;
        /**
         * Dopuszczalne odchylenie modułu wektora przyspieszenia od 1 g, przy
         * którym akcelerometr jeszcze poprawia orientację. Podczas gwałtownego
         * ruchu wskazuje on sumę grawitacji i przyspieszenia własnego, więc
         * korekta z niego byłaby wtedy błędna.
         */
        float accelTolerance = 0.15f;
    };

    /**
     * accelTopic/gyroTopic to identyfikatory czujników źródłowych —
     * nameId("mpu6050"), SensorHub::topicOf(...) albo dowolna inna próbka
     * o właściwych jednostkach.
     */
    ImuFusion(TopicId accelTopic, TopicId gyroTopic, const Setup& setup);
    ImuFusion(TopicId accelTopic, TopicId gyroTopic);
    ~ImuFusion() override;

    const char* name() const override { return "imufuse"; }
    PollMode    pollMode() const override { return PollMode::Free; }
    u8          channels() const override { return 4; }
    const char* unit(u8) const override { return "quat"; }

    Status probe() override;
    Status configure(const SensorCfg& cfg) override;
    Status read(Sample& out) override;

    /** Kąty Eulera w stopniach — wygodniejsze do podglądu niż kwaternion. */
    void euler(float& rollDeg, float& pitchDeg, float& yawDeg) const;

    /** Zeruje orientację i historię. */
    void reset();

private:
    void onAccel(const Sample& s);
    void onGyro(const Sample& s);

    Setup   setup_;
    TopicId accelTopic_;
    TopicId gyroTopic_;
    /** Jedna subskrypcja obsługuje oba tematy — rozróżnia je Sample::topic. */
    SubId   sampleSub_ = kInvalidSub;

    float  accel_[3] = {0.0f, 0.0f, 1.0f};
    bool   haveAccel_ = false;

    float  roll_  = 0.0f;   // radiany
    float  pitch_ = 0.0f;
    float  yaw_   = 0.0f;

    Micros lastGyroUs_ = 0;
    bool   fresh_      = false;  ///< czy od ostatniego read() przyszła nowa próbka
    Micros stampUs_    = 0;
};

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
