/** Hydra — implementacja fuzji IMU (rozdz. 8). */

#include "hydra/sense/ImuFusion.hpp"

#if HYDRA_ENABLE_SENSE

#include <math.h>

namespace hydra {
namespace sense {
namespace {

constexpr float kDegToRad = 0.01745329252f;
constexpr float kRadToDeg = 57.29577951f;

float absf(float v) { return v < 0.0f ? -v : v; }

}  // namespace

ImuFusion::ImuFusion(TopicId accelTopic, TopicId gyroTopic, const Setup& setup)
    : setup_(setup), accelTopic_(accelTopic), gyroTopic_(gyroTopic) {}

ImuFusion::ImuFusion(TopicId accelTopic, TopicId gyroTopic)
    : ImuFusion(accelTopic, gyroTopic, Setup{}) {}

ImuFusion::~ImuFusion() {
    if (sampleSub_ != kInvalidSub) EventBus::unsubscribe(sampleSub_);
}

Status ImuFusion::probe() {
    // Czujnik wirtualny jest zawsze obecny — nie ma czego wykrywać.
    return ok();
}

Status ImuFusion::configure(const SensorCfg&) {
    if (accelTopic_ == kInvalidTopic || gyroTopic_ == kInvalidTopic) {
        return fail(Err::BadArgument);
    }
    if (setup_.alpha <= 0.0f || setup_.alpha > 1.0f) return fail(Err::BadArgument);

    reset();

    // Tryb Direct: obie próbki przychodzą już z taska sense.poll, więc kolejka
    // dokładałaby tylko opóźnienie między pomiarem a jego uwzględnieniem.
    auto a = EventBus::subscribe<Sample>([this](const Sample& s) {
        if (s.topic == accelTopic_) onAccel(s);
        else if (s.topic == gyroTopic_) onGyro(s);
    });
    if (!a) return fail(a.error());
    sampleSub_ = *a;
    return ok();
}

void ImuFusion::reset() {
    roll_ = pitch_ = yaw_ = 0.0f;
    accel_[0] = accel_[1] = 0.0f;
    accel_[2] = 1.0f;
    haveAccel_  = false;
    lastGyroUs_ = 0;
    fresh_      = false;
    stampUs_    = 0;
}

void ImuFusion::onAccel(const Sample& s) {
    if (s.n < 3) return;
    accel_[0]  = s.value[0];
    accel_[1]  = s.value[1];
    accel_[2]  = s.value[2];
    haveAccel_ = true;
}

void ImuFusion::onGyro(const Sample& s) {
    if (s.n < 3) return;

    if (lastGyroUs_ == 0) {
        // Pierwsza próbka wyznacza tylko punkt odniesienia czasu — bez niej
        // krok całkowania byłby liczony względem zera i wyszedłby absurdalny.
        lastGyroUs_ = s.t_us;
        return;
    }

    float dt = static_cast<float>(s.t_us - lastGyroUs_) / 1000000.0f;
    lastGyroUs_ = s.t_us;
    if (dt <= 0.0f) return;
    if (dt > setup_.maxDtSec) dt = setup_.maxDtSec;

    // Całkowanie prędkości kątowych.
    roll_  += s.value[0] * kDegToRad * dt;
    pitch_ += s.value[1] * kDegToRad * dt;
    yaw_   += s.value[2] * kDegToRad * dt;

    // Korekta akcelerometrem — tylko gdy moduł wektora jest bliski 1 g.
    // W trakcie gwałtownego ruchu akcelerometr mierzy grawitację razem
    // z przyspieszeniem własnym i „poprawiłby" orientację w złą stronę.
    if (haveAccel_) {
        const float mag = sqrtf(accel_[0] * accel_[0] + accel_[1] * accel_[1] +
                                accel_[2] * accel_[2]);
        if (mag > 0.01f && absf(mag - 1.0f) <= setup_.accelTolerance) {
            const float accelRoll  = atan2f(accel_[1], accel_[2]);
            const float accelPitch = atan2f(-accel_[0],
                                            sqrtf(accel_[1] * accel_[1] +
                                                  accel_[2] * accel_[2]));
            const float beta = 1.0f - setup_.alpha;
            roll_  = setup_.alpha * roll_ + beta * accelRoll;
            pitch_ = setup_.alpha * pitch_ + beta * accelPitch;
            // Kursu akcelerometr nie widzi — bez magnetometru yaw dryfuje.
        }
    }

    stampUs_ = s.t_us;
    fresh_   = true;
}

Status ImuFusion::read(Sample& out) {
    // Tryb Free: brak nowych danych to nie awaria, tylko brak czego liczyć.
    if (!fresh_) return fail(Err::WouldBlock);
    fresh_ = false;

    const float cr = cosf(roll_ * 0.5f),  sr = sinf(roll_ * 0.5f);
    const float cp = cosf(pitch_ * 0.5f), sp = sinf(pitch_ * 0.5f);
    const float cy = cosf(yaw_ * 0.5f),   sy = sinf(yaw_ * 0.5f);

    out.value[0] = cr * cp * cy + sr * sp * sy;  // w
    out.value[1] = sr * cp * cy - cr * sp * sy;  // x
    out.value[2] = cr * sp * cy + sr * cp * sy;  // y
    out.value[3] = cr * cp * sy - sr * sp * cy;  // z
    out.n        = 4;
    // Znacznik z próbki żyroskopu, a nie z chwili odczytu — orientacja opisuje
    // moment ostatniego pomiaru, nie moment jego przetworzenia.
    out.t_us     = stampUs_;
    out.q        = haveAccel_ ? Quality::Good : Quality::Suspect;
    return ok();
}

void ImuFusion::euler(float& rollDeg, float& pitchDeg, float& yawDeg) const {
    rollDeg  = roll_ * kRadToDeg;
    pitchDeg = pitch_ * kRadToDeg;
    yawDeg   = yaw_ * kRadToDeg;
}

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
