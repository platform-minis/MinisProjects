/**
 * Testy modułu ruchu (etap M5).
 *
 * Cała ścieżka sterowania — regulator, kinematyka, odometria, łańcuch
 * bezpieczeństwa — jest tu sprawdzalna liczbowo, bez napędu i bez czekania
 * na realny czas. Regulator testowany jest w obu arytmetykach naraz, bo
 * twierdzenie „ten sam kod działa na float i na Q16.16" jest warte dokładnie
 * tyle, ile dowód.
 */

#include "hydra_test.hpp"

#include "hydra/core/App.hpp"
#include "hydra/core/RealMath.hpp"
#include "hydra/hal/Mock.hpp"
#include "hydra/motion/DifferentialDrive.hpp"
#include "hydra/motion/Mock.hpp"
#include "hydra/motion/MotionModule.hpp"

using namespace hydra;
using namespace hydra::motion;

namespace {

void resetMotion() {
    App::reset();
    hal::Hal::reset();
    hal::mock::backend().clear();
    hal::mock::install();
    EventBus::reset();
    EventBus::init();
    Log::init(LogLevel::Off, Log::Mode::Sync);
}

bool near(float value, float expected, float tolerance) {
    const float diff = value - expected;
    return (diff < 0 ? -diff : diff) <= tolerance;
}

DifferentialDrive::Config driveConfig() {
    DifferentialDrive::Config cfg;
    cfg.wheelBase  = real(0.2f);
    cfg.maxLinear  = real(1.0f);
    cfg.maxAngular = real(4.0f);
    return cfg;
}

}  // namespace

// ---------------------------------------------------------------------------
// Trygonometria stałoprzecinkowa
// ---------------------------------------------------------------------------

TEST("RealMath: sinus stałoprzecinkowy zgadza się ze zmiennoprzecinkowym") {
    // Odometria opiera się na tych funkcjach, a RP2040 nie ma ani FPU,
    // ani tablic funkcji przestępnych.
    const float angles[] = {0.0f, 0.5f, 1.0f, 1.5708f, 2.5f, 3.0f, -0.5f, -2.0f, -3.0f};

    for (float a : angles) {
        const float exact       = sinReal(a);
        const float approximate = toFloat(sinReal(Fixed(a)));
        CHECK(near(approximate, exact, 0.002f));
    }

    CHECK(near(toFloat(cosReal(Fixed(0.0f))), 1.0f, 0.002f));
    CHECK(near(toFloat(cosReal(Fixed(3.14159f))), -1.0f, 0.002f));
}

TEST("RealMath: sprowadzanie kąta do zakresu i pierwiastek") {
    CHECK(near(wrapAngle(7.0f), 7.0f - 2 * kPi, 0.001f));
    CHECK(near(wrapAngle(-7.0f), -7.0f + 2 * kPi, 0.001f));
    CHECK(near(toFloat(wrapAngle(Fixed(7.0f))), 7.0f - 2 * kPi, 0.01f));

    CHECK(near(toFloat(sqrtReal(Fixed(9.0f))), 3.0f, 0.01f));
    CHECK(near(toFloat(sqrtReal(Fixed(2.0f))), 1.41421f, 0.01f));
    CHECK(toFloat(sqrtReal(Fixed(-1.0f))) == 0.0f);
}

// ---------------------------------------------------------------------------
// Regulator PID
// ---------------------------------------------------------------------------

TEST("PID: człon proporcjonalny odpowiada na uchyb") {
    Pid<float> pid;
    Pid<float>::Gains gains;
    gains.kp = 2.0f;
    pid.setGains(gains);

    // Uchyb 1,0 przy wzmocnieniu 2,0 daje wyjście 2,0.
    CHECK(near(pid.update(1.0f, 0.0f, 0.01f), 2.0f, 0.001f));
    // Brak uchybu to brak wyjścia.
    CHECK(near(pid.update(1.0f, 1.0f, 0.01f), 0.0f, 0.001f));
    // Uchyb ujemny daje wyjście ujemne.
    CHECK(near(pid.update(0.0f, 1.0f, 0.01f), -2.0f, 0.001f));
}

TEST("PID: człon całkujący likwiduje uchyb ustalony") {
    Pid<float> pid;
    Pid<float>::Gains gains;
    gains.ki = 10.0f;
    pid.setGains(gains);

    // Stały uchyb 1,0 przez 100 kroków po 10 ms to całka równa 1 s × 10 = 10.
    float out = 0.0f;
    for (int i = 0; i < 100; ++i) out = pid.update(1.0f, 0.0f, 0.01f);
    CHECK(near(out, 10.0f, 0.1f));
}

TEST("PID: ograniczenie całkowania nie dopuszcza do zapasu z przeszłości") {
    Pid<float> pid;
    Pid<float>::Gains gains;
    gains.ki = 100.0f;
    Pid<float>::Limits limits;
    limits.outMin = -10.0f;
    limits.outMax = 10.0f;

    pid.setGains(gains);
    pid.setLimits(limits);

    // Długi uchyb przy nasyconym wyjściu — klasyczny przypadek robota
    // dociśniętego do przeszkody.
    for (int i = 0; i < 200; ++i) pid.update(1.0f, 0.0f, 0.01f);
    CHECK(pid.diagnostics().holdCount > 0);
    // Całka nie urosła ponad to, co da się wykorzystać.
    CHECK(pid.diagnostics().integral <= 11.0f);

    // Po ustąpieniu przyczyny regulator reaguje od razu, a nie po odpracowaniu
    // nagromadzonego zapasu.
    const float out = pid.update(0.0f, 1.0f, 0.01f);
    CHECK(out < 10.0f);
}

TEST("PID: wyjście trzyma się w zadanych granicach") {
    Pid<float> pid;
    Pid<float>::Gains gains;
    gains.kp = 1000.0f;
    Pid<float>::Limits limits;
    limits.outMin = -100.0f;
    limits.outMax = 100.0f;

    pid.setGains(gains);
    pid.setLimits(limits);

    CHECK(near(pid.update(10.0f, 0.0f, 0.01f), 100.0f, 0.001f));
    CHECK(near(pid.update(-10.0f, 0.0f, 0.01f), -100.0f, 0.001f));
    CHECK(pid.diagnostics().saturatedCount >= 2);
}

TEST("PID: pochodna liczona z pomiaru, nie z uchybu") {
    Pid<float> pid;
    Pid<float>::Gains gains;
    gains.kd = 1.0f;
    pid.setGains(gains);
    pid.setDerivativeFilter(1.0f);  // bez filtrowania, żeby widzieć czysty efekt

    pid.update(0.0f, 0.0f, 0.01f);  // pierwszy krok tylko zapamiętuje pomiar

    // Skokowa zmiana zadania nie może dać impulsu na wyjściu — inaczej napęd
    // szarpałby przy każdej nowej komendzie.
    const float onSetpointJump = pid.update(100.0f, 0.0f, 0.01f);
    CHECK(near(onSetpointJump, 0.0f, 0.001f));

    // Zmiana pomiaru już tak — i z przeciwnym znakiem, bo to człon tłumiący.
    const float onMeasurementChange = pid.update(100.0f, 1.0f, 0.01f);
    CHECK(onMeasurementChange < 0.0f);
}

TEST("PID: filtr pochodnej tłumi szum pomiaru") {
    Pid<float>::Gains gains;
    gains.kd = 1.0f;

    Pid<float> unfiltered, filtered;
    unfiltered.setGains(gains);
    filtered.setGains(gains);
    unfiltered.setDerivativeFilter(1.0f);
    filtered.setDerivativeFilter(0.1f);

    float maxUnfiltered = 0.0f;
    float maxFiltered   = 0.0f;

    // Pomiar drgający wokół zera — tak wygląda enkoder przy okresie 1 ms.
    for (int i = 0; i < 40; ++i) {
        const float noisy = (i % 2 == 0) ? 0.01f : -0.01f;
        const float a = unfiltered.update(0.0f, noisy, 0.001f);
        const float b = filtered.update(0.0f, noisy, 0.001f);
        if (i > 5) {
            if (a > maxUnfiltered) maxUnfiltered = a;
            if (b > maxFiltered) maxFiltered = b;
        }
    }
    CHECK(maxFiltered < maxUnfiltered / 2.0f);
}

TEST("PID: ten sam kod na float i na Q16.16") {
    // Twierdzenie z rozdz. 9 wymaga dowodu, a nie deklaracji.
    Pid<float> f;
    Pid<Fixed> q;

    Pid<float>::Gains gf;
    gf.kp = 2.0f;
    gf.ki = 0.5f;
    gf.kd = 0.1f;
    Pid<Fixed>::Gains gq;
    gq.kp = Fixed(2.0f);
    gq.ki = Fixed(0.5f);
    gq.kd = Fixed(0.1f);

    f.setGains(gf);
    q.setGains(gq);
    f.setDerivativeFilter(0.5f);
    q.setDerivativeFilter(Fixed(0.5f));

    float measured = 0.0f;
    for (int i = 0; i < 50; ++i) {
        const float outF = f.update(1.0f, measured, 0.01f);
        const float outQ = toFloat(q.update(Fixed(1.0f), Fixed(measured), Fixed(0.01f)));

        // Arytmetyka stałoprzecinkowa ma skończoną rozdzielczość, ale przebieg
        // musi być ten sam co do ułamka procenta.
        CHECK(near(outQ, outF, 0.02f));
        measured += outF * 0.01f;  // prosty model obiektu całkującego
    }
}

TEST("PID: zerowy krok czasu jest odrzucany") {
    Pid<float> pid;
    Pid<float>::Gains gains;
    gains.kp = 1.0f;
    pid.setGains(gains);

    const float first = pid.update(1.0f, 0.0f, 0.01f);
    // Zerowy albo ujemny krok czasu dałby dzielenie przez zero w pochodnej.
    CHECK(near(pid.update(5.0f, 0.0f, 0.0f), first, 0.001f));
    CHECK(near(pid.update(5.0f, 0.0f, -0.01f), first, 0.001f));
}

TEST("PID: reset czyści stan wewnętrzny") {
    Pid<float> pid;
    Pid<float>::Gains gains;
    gains.ki = 10.0f;
    pid.setGains(gains);

    for (int i = 0; i < 50; ++i) pid.update(1.0f, 0.0f, 0.01f);
    CHECK(pid.diagnostics().integral > 1.0f);

    pid.reset();
    CHECK(pid.diagnostics().integral == 0.0f);
    CHECK(near(pid.update(1.0f, 0.0f, 0.01f), 0.1f, 0.01f));
}

// ---------------------------------------------------------------------------
// Kinematyka
// ---------------------------------------------------------------------------

TEST("Kinematyka: jazda prosto to równe prędkości kół") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    Twist twist;
    twist.linear = real(0.5f);
    const WheelSpeeds wheels = drive.toWheelSpeeds(twist);

    CHECK(near(toFloat(wheels.left), 0.5f, 0.001f));
    CHECK(near(toFloat(wheels.right), 0.5f, 0.001f));
}

TEST("Kinematyka: obrót w miejscu to prędkości przeciwne") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    Twist twist;
    twist.angular = real(1.0f);  // rad/s w lewo
    const WheelSpeeds wheels = drive.toWheelSpeeds(twist);

    // Przy rozstawie 0,2 m koło zewnętrzne jedzie 0,1 m/s, wewnętrzne wstecz.
    CHECK(near(toFloat(wheels.left), -0.1f, 0.001f));
    CHECK(near(toFloat(wheels.right), 0.1f, 0.001f));
}

TEST("Kinematyka: przeliczenie w obie strony jest odwracalne") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    Twist original;
    original.linear  = real(0.35f);
    original.angular = real(1.2f);

    const Twist back = drive.toTwist(drive.toWheelSpeeds(original));
    CHECK(near(toFloat(back.linear), 0.35f, 0.005f));
    CHECK(near(toFloat(back.angular), 1.2f, 0.02f));
}

TEST("Kinematyka: nadmierne zadanie skaluje się, nie przycina") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    Twist tooFast;
    tooFast.linear  = real(1.0f);  // już na granicy
    tooFast.angular = real(3.0f);  // koło zewnętrzne wyszłoby na 1,3 m/s

    const Twist limited = drive.limit(tooFast);
    const WheelSpeeds wheels = drive.toWheelSpeeds(limited);

    // Żadne koło nie przekracza możliwości napędu.
    CHECK(toFloat(wheels.left) <= 1.001f);
    CHECK(toFloat(wheels.right) <= 1.001f);

    // Kluczowe: stosunek prędkości, czyli promień skrętu, został zachowany.
    // Przycięcie każdej składowej osobno posłałoby robota w inną stronę.
    const float ratioBefore = 3.0f / 1.0f;
    const float ratioAfter  = toFloat(limited.angular) / toFloat(limited.linear);
    CHECK(near(ratioAfter, ratioBefore, 0.05f));
}

TEST("Kinematyka: błędna konfiguracja jest odrzucana") {
    DifferentialDrive drive;
    DifferentialDrive::Config bad = driveConfig();

    bad.wheelBase = real(0.0f);
    CHECK(drive.configure(bad).error() == Err::BadArgument);

    bad = driveConfig();
    bad.maxLinear = real(0.0f);
    CHECK(drive.configure(bad).error() == Err::BadArgument);
}

// ---------------------------------------------------------------------------
// Odometria
// ---------------------------------------------------------------------------

TEST("Odometria: jazda prosto przesuwa wzdłuż osi") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    // Metr do przodu w stu krokach.
    for (int i = 0; i < 100; ++i) drive.integrate(real(0.01f), real(0.01f));

    const Pose pose = drive.pose();
    CHECK(near(toFloat(pose.x), 1.0f, 0.01f));
    CHECK(near(toFloat(pose.y), 0.0f, 0.01f));
    CHECK(near(toFloat(pose.theta), 0.0f, 0.01f));
}

TEST("Odometria: obrót w miejscu zmienia sam kąt") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    // Obrót o π: koła w przeciwne strony, po 0,1·π metra każde.
    const float perWheel = kPi * 0.1f;
    for (int i = 0; i < 100; ++i) {
        drive.integrate(real(-perWheel / 100.0f), real(perWheel / 100.0f));
    }

    const Pose pose = drive.pose();
    CHECK(near(toFloat(pose.x), 0.0f, 0.01f));
    CHECK(near(toFloat(pose.y), 0.0f, 0.01f));
    // Kąt sprowadzony do zakresu, więc π i -π są równoważne.
    const float theta = toFloat(pose.theta);
    CHECK(near(theta > 0 ? theta : -theta, kPi, 0.05f));
}

TEST("Odometria: całkowanie po łuku, nie po prostej") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    // Ćwiartka okręgu o promieniu 1 m: koła jadą z różnicą wynikającą
    // z rozstawu, przez ćwierć obwodu.
    const float radius = 1.0f;
    const float base   = 0.2f;
    const float arc    = kHalfPi * radius;
    const int   steps  = 500;

    const float inner = (radius - base / 2) * kHalfPi / steps;
    const float outer = (radius + base / 2) * kHalfPi / steps;
    for (int i = 0; i < steps; ++i) drive.integrate(real(inner), real(outer));

    const Pose pose = drive.pose();
    // Po ćwiartce okręgu pojazd stoi w (r, r) obrócony o π/2. Przybliżenie
    // prostoliniowe dałoby tu błąd rzędu kilku procent promienia.
    CHECK(near(toFloat(pose.x), radius, 0.02f));
    CHECK(near(toFloat(pose.y), radius, 0.02f));
    CHECK(near(toFloat(pose.theta), kHalfPi, 0.02f));
    HYDRA_UNUSED(arc);
}

TEST("Odometria: kąt zostaje w zakresie [-pi, pi]") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    // Trzy pełne obroty.
    for (int i = 0; i < 3000; ++i) {
        drive.integrate(real(-0.000628f), real(0.000628f));
    }
    const float theta = toFloat(drive.pose().theta);
    CHECK(theta >= -kPi - 0.01f);
    CHECK(theta <= kPi + 0.01f);
}

TEST("Odometria: ustawienie i wyzerowanie położenia") {
    DifferentialDrive drive;
    REQUIRE(drive.configure(driveConfig()).has_value());

    Pose start;
    start.x     = real(2.0f);
    start.theta = real(7.0f);  // poza zakresem
    drive.setPose(start);

    CHECK(near(toFloat(drive.pose().x), 2.0f, 0.001f));
    // Kąt jest sprowadzany do zakresu także przy ustawianiu wprost.
    CHECK(toFloat(drive.pose().theta) < kPi);

    drive.resetPose();
    CHECK(near(toFloat(drive.pose().x), 0.0f, 0.001f));
}

// ---------------------------------------------------------------------------
// Przelicznik enkodera
// ---------------------------------------------------------------------------

TEST("Enkoder: zliczenia przeliczane na drogę") {
    WheelOdometer odometer;
    WheelOdometer::Config cfg;
    cfg.countsPerRevolution = 1000;
    cfg.wheelRadius         = real(0.05f);  // obwód ok. 0,314 m
    REQUIRE(odometer.configure(cfg).has_value());

    // Pierwszy odczyt tylko ustala punkt odniesienia.
    CHECK(toFloat(odometer.advance(500)) == 0.0f);

    // Pełny obrót to obwód koła.
    const float distance = toFloat(odometer.advance(1500));
    CHECK(near(distance, 0.3142f, 0.001f));
    CHECK(near(toFloat(odometer.distanceTotal()), 0.3142f, 0.001f));
}

TEST("Enkoder: przepełnienie licznika nie psuje różnicy") {
    WheelOdometer odometer;
    WheelOdometer::Config cfg;
    cfg.countsPerRevolution = 1000;
    cfg.wheelRadius         = real(0.05f);
    REQUIRE(odometer.configure(cfg).has_value());

    odometer.advance(2147483600);  // tuż pod granicą zakresu
    // Licznik przekręca się na wartości ujemne — odejmowanie w arytmetyce
    // uzupełnieniowej i tak daje poprawną różnicę stu zliczeń.
    const float distance = toFloat(odometer.advance(-2147483596));
    CHECK(near(distance, 100.0f / 1000.0f * 0.3142f, 0.001f));
}

TEST("Enkoder: dekoder kwadraturowy zlicza w obu kierunkach") {
    QuadratureEncoder encoder;
    QuadratureEncoder::Config cfg;
    cfg.a = 4;
    cfg.b = 5;
    REQUIRE(encoder.configure(cfg).has_value());

    // Pełny cykl w przód: 00 → 01 → 11 → 10 → 00, cztery zliczenia.
    encoder.update(false, false);  // stan początkowy
    encoder.update(false, true);
    encoder.update(true, true);
    encoder.update(true, false);
    encoder.update(false, false);

    auto counts = encoder.counts();
    // Bez begin() licznik jest niedostępny — sprawdzamy sam dekoder.
    CHECK(!counts.has_value());

    QuadratureEncoder forward;
    REQUIRE(forward.configure(cfg).has_value());
    forward.update(false, false);
    for (int i = 0; i < 4; ++i) {
        forward.update(false, true);
        forward.update(true, true);
        forward.update(true, false);
        forward.update(false, false);
    }
    CHECK_EQ(static_cast<int>(forward.glitches()), 0);
}

TEST("Enkoder: nieprawidłowe przejścia są liczone osobno") {
    QuadratureEncoder encoder;
    QuadratureEncoder::Config cfg;
    cfg.a = 4;
    cfg.b = 5;
    REQUIRE(encoder.configure(cfg).has_value());

    encoder.update(false, false);
    // Oba sygnały zmieniają się jednocześnie — przy poprawnym sygnale
    // kwadraturowym niemożliwe. Rosnąca liczba takich przejść to pierwszy
    // sygnał, że trzeba przejść na dekodowanie sprzętowe.
    encoder.update(true, true);
    CHECK_EQ(static_cast<int>(encoder.glitches()), 1);
}

// ---------------------------------------------------------------------------
// Silniki
// ---------------------------------------------------------------------------

TEST("Silnik: mostek w układzie dwóch wejść PWM") {
    resetMotion();
    HBridgeMotor motor;
    HBridgeMotor::Config cfg;
    cfg.wiring = HBridgeMotor::Wiring::DualPwm;
    cfg.in1    = 10;
    cfg.in2    = 11;
    REQUIRE(motor.configure(cfg).has_value());
    REQUIRE(motor.begin().has_value());

    auto& pwm = hal::mock::backend().pwm;

    REQUIRE(motor.setPower(600).has_value());
    CHECK_EQ(static_cast<int>(pwm.channel(10).permille), 600);
    // Drugie wejście musi być wyzerowane — inaczej mostek hamuje zamiast jechać.
    CHECK_EQ(static_cast<int>(pwm.channel(11).permille), 0);

    REQUIRE(motor.setPower(-600).has_value());
    CHECK_EQ(static_cast<int>(pwm.channel(10).permille), 0);
    CHECK_EQ(static_cast<int>(pwm.channel(11).permille), 600);
}

TEST("Silnik: hamowanie zwiera uzwojenie, wybieg je rozwiera") {
    resetMotion();
    HBridgeMotor motor;
    HBridgeMotor::Config cfg;
    cfg.in1 = 10;
    cfg.in2 = 11;
    REQUIRE(motor.configure(cfg).has_value());
    REQUIRE(motor.begin().has_value());

    auto& pwm = hal::mock::backend().pwm;

    REQUIRE(motor.brake().has_value());
    CHECK_EQ(static_cast<int>(pwm.channel(10).permille), kMaxPower);
    CHECK_EQ(static_cast<int>(pwm.channel(11).permille), kMaxPower);

    REQUIRE(motor.coast().has_value());
    CHECK_EQ(static_cast<int>(pwm.channel(10).permille), 0);
    CHECK_EQ(static_cast<int>(pwm.channel(11).permille), 0);
}

TEST("Silnik: strefa martwa odwzorowuje zakres na użyteczny") {
    resetMotion();
    HBridgeMotor motor;
    HBridgeMotor::Config cfg;
    cfg.in1      = 10;
    cfg.in2      = 11;
    cfg.deadband = 200;  // poniżej 20% wał nie rusza
    REQUIRE(motor.configure(cfg).has_value());
    REQUIRE(motor.begin().has_value());

    // Najmniejsze niezerowe zadanie od razu przekracza próg ruszenia.
    REQUIRE(motor.setPower(1).has_value());
    CHECK(motor.appliedPermille() >= 200);

    // Pełne zadanie zostaje pełne.
    REQUIRE(motor.setPower(1000).has_value());
    CHECK_EQ(static_cast<int>(motor.appliedPermille()), 1000);

    // Zero pozostaje zerem — strefa martwa nie może wprawiać w ruch stojącego.
    REQUIRE(motor.setPower(0).has_value());
    CHECK_EQ(static_cast<int>(motor.appliedPermille()), 0);
}

TEST("Silnik: kierunek da się odwrócić bez przekładania przewodów") {
    resetMotion();
    HBridgeMotor motor;
    HBridgeMotor::Config cfg;
    cfg.in1    = 10;
    cfg.in2    = 11;
    cfg.invert = true;
    REQUIRE(motor.configure(cfg).has_value());
    REQUIRE(motor.begin().has_value());

    auto& pwm = hal::mock::backend().pwm;
    REQUIRE(motor.setPower(500).has_value());
    // Przy odwróceniu dodatnia moc wychodzi na drugim wejściu.
    CHECK_EQ(static_cast<int>(pwm.channel(11).permille), 500);
}

TEST("Silnik: błędna konfiguracja jest odrzucana") {
    HBridgeMotor motor;
    HBridgeMotor::Config cfg;
    CHECK(motor.configure(cfg).error() == Err::BadArgument);  // brak pinów

    cfg.in1    = 10;
    cfg.in2    = 11;
    cfg.freqHz = 0;
    CHECK(motor.configure(cfg).error() == Err::BadArgument);

    cfg.freqHz   = 20000;
    cfg.deadband = 1000;
    CHECK(motor.configure(cfg).error() == Err::BadArgument);
}

TEST("Serwo: kąt przeliczany na szerokość impulsu") {
    resetMotion();
    PwmServo servo;
    PwmServo::Config cfg;
    cfg.pin = 7;
    REQUIRE(servo.configure(cfg).has_value());
    REQUIRE(servo.begin().has_value());

    REQUIRE(servo.setAngle(0).has_value());
    CHECK_EQ(static_cast<int>(servo.pulseUs()), 1000);

    REQUIRE(servo.setAngle(90).has_value());
    CHECK_EQ(static_cast<int>(servo.pulseUs()), 1500);

    REQUIRE(servo.setAngle(180).has_value());
    CHECK_EQ(static_cast<int>(servo.pulseUs()), 2000);

    // Kąt poza zakresem jest przycinany, a nie odrzucany.
    REQUIRE(servo.setAngle(400).has_value());
    CHECK_EQ(static_cast<int>(servo.angle()), 180);
}

// ---------------------------------------------------------------------------
// Bezpieczeństwo
// ---------------------------------------------------------------------------

TEST("Bezpieczeństwo: bez komend napęd nie rusza") {
    resetMotion();
    SafetyChain safety;
    SafetyChain::Config cfg;
    cfg.commandTimeoutMs = 500;
    REQUIRE(safety.configure(cfg).has_value());

    // Brak jakiejkolwiek komendy to stan wyjściowy — napęd stoi.
    CHECK(safety.evaluate(0) == SafetyState::CommandTimeout);
    CHECK(!safety.canDrive());

    safety.feedCommand(100);
    CHECK(safety.evaluate(200) == SafetyState::Ready);
    CHECK(safety.canDrive());
}

TEST("Bezpieczeństwo: milczenie nadawcy zatrzymuje napęd") {
    resetMotion();
    SafetyChain safety;
    SafetyChain::Config cfg;
    cfg.commandTimeoutMs = 500;
    REQUIRE(safety.configure(cfg).has_value());

    safety.feedCommand(0);
    CHECK(safety.evaluate(400) == SafetyState::Ready);

    // Zerwane łącze: bez tego zabezpieczenia robot jedzie dalej z ostatnią
    // zadaną prędkością, aż w coś uderzy.
    CHECK(safety.evaluate(500) == SafetyState::CommandTimeout);

    // Watchdog komend kasuje się sam, gdy komendy wrócą.
    safety.feedCommand(600);
    CHECK(safety.evaluate(700) == SafetyState::Ready);
}

TEST("Bezpieczeństwo: zatrzymanie awaryjne wymaga jawnego skasowania") {
    resetMotion();
    SafetyChain safety;
    REQUIRE(safety.configure(SafetyChain::Config{}).has_value());
    safety.feedCommand(0);
    CHECK(safety.evaluate(10) == SafetyState::Ready);

    safety.emergencyStop(StopReason::Operator);
    CHECK(safety.evaluate(20) == SafetyState::EmergencyStop);

    // Same świeże komendy nie wznawiają jazdy — to byłoby najgorsze możliwe
    // zachowanie po zatrzymaniu awaryjnym.
    safety.feedCommand(30);
    CHECK(safety.evaluate(40) == SafetyState::EmergencyStop);

    REQUIRE(safety.clearEmergencyStop().has_value());
    CHECK(safety.evaluate(50) == SafetyState::Ready);
}

TEST("Bezpieczeństwo: zmiany stanu trafiają na magistralę") {
    resetMotion();
    SafetyChain safety;
    REQUIRE(safety.configure(SafetyChain::Config{}).has_value());

    int         changes = 0;
    SafetyState last    = SafetyState::Ready;
    auto sub = EventBus::subscribe<SafetyChanged>([&](const SafetyChanged& e) {
        ++changes;
        last = e.to;
    });
    REQUIRE(sub.has_value());

    safety.feedCommand(0);
    safety.evaluate(10);
    CHECK(last == SafetyState::Ready);

    safety.emergencyStop(StopReason::Operator);
    safety.evaluate(20);
    CHECK(last == SafetyState::EmergencyStop);
    CHECK(changes >= 2);
}

TEST("Bezpieczeństwo: chwilowy prąd rozruchowy nie wyzwala zabezpieczenia") {
    resetMotion();
    SafetyChain safety;
    SafetyChain::Config cfg;
    cfg.currentLimitMa     = 2000;
    cfg.overCurrentGraceMs = 100;
    REQUIRE(safety.configure(cfg).has_value());

    safety.feedCommand(0);
    // Prąd rozruchowy silnika bywa wielokrotnie większy od roboczego.
    safety.reportCurrent(5000, 0);
    CHECK(safety.evaluate(50) == SafetyState::Ready);

    // Ustąpił przed upływem czasu zwłoki.
    safety.reportCurrent(800, 60);
    CHECK(safety.evaluate(200) == SafetyState::Ready);
}

TEST("Bezpieczeństwo: trwałe przekroczenie prądu zatrzymuje awaryjnie") {
    resetMotion();
    SafetyChain safety;
    SafetyChain::Config cfg;
    cfg.currentLimitMa     = 2000;
    cfg.overCurrentGraceMs = 100;
    REQUIRE(safety.configure(cfg).has_value());

    int  trips = 0;
    auto sub   = EventBus::subscribe<CurrentLimitTripped>(
        [&](const CurrentLimitTripped&) { ++trips; });
    REQUIRE(sub.has_value());

    safety.feedCommand(0);
    safety.reportCurrent(5000, 0);
    safety.reportCurrent(5000, 50);
    CHECK(safety.evaluate(50) == SafetyState::Ready);

    // Po upływie zwłoki: zablokowany wał albo zwarcie — wymaga obejrzenia
    // maszyny, więc przechodzi w zatrzymanie awaryjne, nie w stan przejściowy.
    safety.reportCurrent(5000, 150);
    CHECK(safety.evaluate(150) == SafetyState::EmergencyStop);
    CHECK_EQ(trips, 1);

    safety.feedCommand(200);
    CHECK(safety.evaluate(200) == SafetyState::EmergencyStop);
}

TEST("Bezpieczeństwo: wyłączenie programowe") {
    resetMotion();
    SafetyChain safety;
    REQUIRE(safety.configure(SafetyChain::Config{}).has_value());

    safety.feedCommand(0);
    CHECK(safety.evaluate(10) == SafetyState::Ready);

    safety.enable(false);
    CHECK(safety.evaluate(20) == SafetyState::NotEnabled);

    safety.enable(true);
    safety.feedCommand(30);
    CHECK(safety.evaluate(40) == SafetyState::Ready);
}

// ---------------------------------------------------------------------------
// Moduł
// ---------------------------------------------------------------------------

namespace {

/** Zestaw modułu z atrapami napędu, gotowy do sterowania czasem z testu. */
struct Rig {
    mock::MockMotor   left, right;
    mock::MockEncoder encLeft, encRight;
    MotionModule      motion;

    Status setUp(bool withEncoders = true) {
        MotionModule::Config cfg;
        cfg.periodMs = 5;
        cfg.drive    = driveConfig();
        cfg.odometer.countsPerRevolution = 1000;
        cfg.odometer.wheelRadius         = real(0.05f);
        cfg.safety.commandTimeoutMs      = 500;
        cfg.gains.kp        = real(2000.0f);
        cfg.pidLimits.outMin = real(-1000.0f);
        cfg.pidLimits.outMax = real(1000.0f);
        cfg.statePeriodCycles = 2;

        motion.attachMotors(left, right);
        if (withEncoders) motion.attachEncoders(encLeft, encRight);

        HYDRA_CHECK(motion.configure(cfg));
        return motion.init();
    }
};

}  // namespace

TEST("Moduł ruchu: bez komend silniki stoją zahamowane") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp().has_value());

    rig.motion.step(10);
    CHECK(rig.motion.safety().state() == SafetyState::CommandTimeout);
    // Hamowanie, nie wybieg: pojazd bez sterowania ma stanąć, a nie toczyć się.
    CHECK(rig.left.mode == mock::MockMotor::Mode::Brake);
    CHECK(rig.right.mode == mock::MockMotor::Mode::Brake);
}

TEST("Moduł ruchu: zadanie prędkości uruchamia regulację") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp().has_value());

    Twist target;
    target.linear = real(0.3f);
    rig.motion.setTarget(target);

    rig.motion.step(10);
    // Koła stoją, zadanie jest dodatnie — regulator musi dać moc do przodu.
    CHECK(rig.left.power() > 0);
    CHECK(rig.right.power() > 0);
    CHECK(rig.left.mode == mock::MockMotor::Mode::Drive);
}

TEST("Moduł ruchu: obrót daje przeciwne moce na kołach") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp().has_value());

    Twist target;
    target.angular = real(2.0f);
    rig.motion.setTarget(target);
    rig.motion.step(10);

    CHECK(rig.left.power() < 0);
    CHECK(rig.right.power() > 0);
}

TEST("Moduł ruchu: odometria działa także przy zatrzymanym napędzie") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp().has_value());

    // Brak komend, więc napęd stoi — ale ktoś pcha robota ręcznie.
    rig.motion.step(10);
    rig.encLeft.advance(1000);
    rig.encRight.advance(1000);
    rig.motion.step(15);

    // Obwód koła to około 0,314 m — odometria musi to zauważyć, inaczej robot
    // po popchnięciu nie wie, gdzie jest.
    CHECK(near(toFloat(rig.motion.pose().x), 0.3142f, 0.01f));
}

TEST("Moduł ruchu: utrata enkodera przy jeździe zatrzymuje awaryjnie") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp().has_value());

    Twist target;
    target.linear = real(0.3f);
    rig.motion.setTarget(target);
    rig.motion.step(10);
    CHECK(rig.motion.safety().state() == SafetyState::Ready);

    // Sterowanie w ciemno jest gorsze niż zatrzymanie.
    rig.encLeft.faulty = true;
    rig.motion.step(15);
    CHECK(rig.motion.safety().state() == SafetyState::EmergencyStop);
    CHECK(rig.left.mode == mock::MockMotor::Mode::Brake);
}

TEST("Moduł ruchu: bez enkoderów jedzie w otwartej pętli") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp(false).has_value());

    Twist target;
    target.linear = real(0.5f);  // połowa maksymalnej prędkości
    rig.motion.setTarget(target);
    rig.motion.step(10);

    // Zadanie przeliczone wprost na moc — bez pomiaru nie ma czego regulować.
    CHECK(near(static_cast<float>(rig.left.power()), 500.0f, 20.0f));
    CHECK(near(static_cast<float>(rig.right.power()), 500.0f, 20.0f));
}

TEST("Moduł ruchu: migawka stanu trafia na magistralę") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp().has_value());

    int         states = 0;
    MotionState last{};
    auto sub = EventBus::subscribe<MotionState>([&](const MotionState& e) {
        ++states;
        last = e;
    });
    REQUIRE(sub.has_value());

    Twist target;
    target.linear = real(0.2f);
    rig.motion.setTarget(target);
    for (Millis t = 5; t <= 30; t += 5) rig.motion.step(t);

    CHECK(states >= 2);
    CHECK_EQ(static_cast<int>(last.safety), static_cast<int>(SafetyState::Ready));
}

TEST("Moduł ruchu: zadanie ponad możliwości jest ograniczane przy przyjęciu") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp().has_value());

    Twist tooFast;
    tooFast.linear = real(10.0f);  // dziesięciokrotnie ponad maksimum
    rig.motion.setTarget(tooFast);

    CHECK(toFloat(rig.motion.target().linear) <= 1.001f);
}

TEST("Moduł ruchu: błędna konfiguracja jest odrzucana") {
    resetMotion();
    MotionModule motion;

    MotionModule::Config cfg;
    cfg.periodMs = 0;
    CHECK(motion.configure(cfg).error() == Err::BadArgument);

    cfg.periodMs = 100;  // poza zakresem 1–5 ms z rozdz. 9
    CHECK(motion.configure(cfg).error() == Err::BadArgument);

    // Bez silników moduł nie ma czym sterować.
    cfg.periodMs = 5;
    cfg.drive    = driveConfig();
    REQUIRE(motion.configure(cfg).has_value());
    CHECK(motion.init().error() == Err::NotInitialized);
}

TEST("Moduł ruchu: task sterujący trzyma okres") {
    resetMotion();
    Rig rig;
    REQUIRE(rig.setUp().has_value());

    Twist target;
    target.linear = real(0.2f);
    rig.motion.setTarget(target);

    REQUIRE(rig.motion.start().has_value());
    rtos::delayMs(120);
    rig.motion.stop();

    // 120 ms przy okresie 5 ms; luz na szum schedulera hosta.
    CHECK(rig.motion.stats().cycles >= 12);
    CHECK(rig.left.writes > 0);
}
