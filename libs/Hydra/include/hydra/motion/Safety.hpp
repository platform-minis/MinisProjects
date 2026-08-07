#pragma once
/**
 * Hydra — łańcuch bezpieczeństwa napędu (rozdz. 9).
 *
 * Trzy niezależne mechanizmy, z których każdy sam potrafi zatrzymać napęd:
 *
 * **Zatrzymanie awaryjne.** Ustawiane flagą, nie przez magistralę zdarzeń.
 * Kolejka mogłaby być pełna, subskrypcja odroczona, a task rozgłaszający
 * wywłaszczony — przy zatrzymaniu awaryjnym żadne z tych opóźnień nie jest
 * dopuszczalne. Flaga jest widoczna w następnym cyklu pętli, czyli po
 * najwyżej kilku milisekundach, i wolno ją ustawić także z przerwania.
 * Skasowanie wymaga jawnej decyzji — samo ustąpienie przyczyny nie wznawia jazdy.
 *
 * **Watchdog komend.** Brak nowego zadania prędkości przez zadany czas
 * zatrzymuje napęd. To zabezpieczenie przed zerwaniem łącza: bez niego robot
 * jedzie dalej z ostatnią zadaną prędkością, dopóki nie uderzy w przeszkodę.
 * W odróżnieniu od zatrzymania awaryjnego kasuje się samo, gdy komendy wrócą.
 *
 * **Limit prądu.** Przekroczenie musi utrzymywać się przez zadany czas, żeby
 * zadziałało — prąd rozruchowy silnika bywa wielokrotnie większy od roboczego
 * i wyzwalałby zabezpieczenie przy każdym ruszeniu.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MOTION

#include <atomic>

#include "hydra/core/Expected.hpp"
#include "hydra/motion/MotionTypes.hpp"

namespace hydra {
namespace motion {

class SafetyChain {
public:
    struct Config {
        /** Po tylu milisekundach bez nowego zadania napęd staje. Zero wyłącza. */
        u32 commandTimeoutMs = 500;
        /** Limit prądu w miliamperach. Zero wyłącza kontrolę. */
        u32 currentLimitMa = 0;
        /** Jak długo prąd musi przekraczać limit, zanim zadziała zabezpieczenie. */
        u32 overCurrentGraceMs = 100;
    };

    Status configure(const Config& cfg);

    /** Programowe włączenie i wyłączenie napędu. */
    void enable(bool enabled);
    bool enabled() const { return enabled_; }

    /** Zgłoszenie świeżego zadania prędkości — karmi watchdoga komend. */
    void feedCommand(Millis now);

    /** Zgłoszenie pomiaru prądu z danego kanału. */
    void reportCurrent(u32 milliamps, Millis now, u8 channel = 0);

    /**
     * Zatrzymanie awaryjne. Bezpieczne do wywołania z przerwania: ustawia
     * wyłącznie flagę, a pętla sterowania zauważa ją w następnym cyklu.
     */
    void emergencyStop(StopReason reason);

    /** Kasuje zatrzymanie awaryjne. Wymaga jawnej decyzji operatora. */
    Status clearEmergencyStop();

    /**
     * Ocenia warunki i zwraca bieżący stan. Publikuje zmiany na magistrali.
     * Wołane raz na cykl pętli sterowania.
     */
    SafetyState evaluate(Millis now);

    SafetyState state() const { return state_; }
    bool        canDrive() const { return state_ == SafetyState::Ready; }
    StopReason  lastReason() const { return reason_; }
    u32         lastCurrentMa() const { return lastCurrent_; }

private:
    void transition(SafetyState next, StopReason reason);

    Config cfg_{};

    std::atomic<bool> estop_{false};
    std::atomic<u8>   estopReason_{static_cast<u8>(StopReason::Unknown)};

    bool        enabled_     = true;
    SafetyState state_       = SafetyState::NotEnabled;
    StopReason  reason_      = StopReason::Unknown;
    Millis      lastCommand_ = 0;
    bool        commandSeen_ = false;

    u32    lastCurrent_    = 0;
    Millis overCurrentSince_ = 0;
    bool   overCurrent_    = false;
    u8     overChannel_    = 0;
};

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
