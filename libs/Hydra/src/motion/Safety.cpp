/** Hydra — implementacja łańcucha bezpieczeństwa (rozdz. 9). */

#include "hydra/motion/Safety.hpp"

#if HYDRA_ENABLE_MOTION

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("motion.safety")

namespace hydra {
namespace motion {

Status SafetyChain::configure(const Config& cfg) {
    cfg_ = cfg;
    return ok();
}

void SafetyChain::enable(bool enabled) { enabled_ = enabled; }

void SafetyChain::feedCommand(Millis now) {
    lastCommand_ = now;
    commandSeen_ = true;
}

void SafetyChain::emergencyStop(StopReason reason) {
    // Wyłącznie zapis flag — bez publikacji na magistrali, bez logowania,
    // bez blokad. Dzięki temu wolno to wywołać także z przerwania.
    estopReason_.store(static_cast<u8>(reason));
    estop_.store(true);
}

Status SafetyChain::clearEmergencyStop() {
    if (!estop_.load()) return ok();
    estop_.store(false);
    estopReason_.store(static_cast<u8>(StopReason::Unknown));
    HYDRA_LOGI("zatrzymanie awaryjne skasowane");
    return ok();
}

void SafetyChain::reportCurrent(u32 milliamps, Millis now, u8 channel) {
    lastCurrent_ = milliamps;
    if (cfg_.currentLimitMa == 0) return;

    if (milliamps <= cfg_.currentLimitMa) {
        overCurrent_ = false;
        return;
    }

    if (!overCurrent_) {
        // Zaczynamy odliczać dopiero od pierwszego przekroczenia: prąd
        // rozruchowy silnika bywa wielokrotnie większy od roboczego i trwa
        // kilkadziesiąt milisekund.
        overCurrent_      = true;
        overCurrentSince_ = now;
        overChannel_      = channel;
    }
}

void SafetyChain::transition(SafetyState next, StopReason reason) {
    if (next == state_) return;

    const SafetyState previous = state_;
    state_  = next;
    reason_ = reason;

    EventBus::publish(SafetyChanged{previous, next, reason});
    if (next == SafetyState::Ready) {
        HYDRA_LOGI("napęd gotowy");
    } else {
        HYDRA_LOGW("napęd zatrzymany: %s", toString(next));
    }
}

SafetyState SafetyChain::evaluate(Millis now) {
    // Kolejność ma znaczenie: zatrzymanie awaryjne przesłania wszystko inne
    // i nie może zostać wyparte przez łagodniejszą przyczynę.
    if (estop_.load()) {
        transition(SafetyState::EmergencyStop,
                   static_cast<StopReason>(estopReason_.load()));
        return state_;
    }

    if (!enabled_) {
        transition(SafetyState::NotEnabled, StopReason::Operator);
        return state_;
    }

    if (overCurrent_ && cfg_.currentLimitMa > 0 &&
        now - overCurrentSince_ >= cfg_.overCurrentGraceMs) {
        EventBus::publish(
            CurrentLimitTripped{lastCurrent_, cfg_.currentLimitMa, overChannel_});
        // Przekroczenie prądu jest na tyle poważne, że przechodzi w zatrzymanie
        // awaryjne: wymaga obejrzenia maszyny, a nie samoczynnego wznowienia.
        emergencyStop(StopReason::OverCurrent);
        transition(SafetyState::EmergencyStop, StopReason::OverCurrent);
        return state_;
    }

    if (cfg_.commandTimeoutMs > 0) {
        const bool stale = !commandSeen_ || (now - lastCommand_) >= cfg_.commandTimeoutMs;
        if (stale) {
            // Watchdog komend kasuje się sam, gdy komendy wrócą — w odróżnieniu
            // od zatrzymania awaryjnego to stan przejściowy, nie awaria.
            transition(SafetyState::CommandTimeout, StopReason::CommandTimeout);
            return state_;
        }
    }

    transition(SafetyState::Ready, StopReason::Unknown);
    return state_;
}

}  // namespace motion
}  // namespace hydra

#endif  // HYDRA_ENABLE_MOTION
