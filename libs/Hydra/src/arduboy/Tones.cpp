#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_ARDUBOY

#include "hydra/arduboy/Tones.hpp"

#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace arduboy {

void Tones::emit(u16 frequency, bool loud) {
    // Wyciszenie zawsze przechodzi do ujścia, nawet gdy gracz wyłączył dźwięk:
    // inaczej nuta grająca w chwili wyłączenia zostałaby na brzęczyku na stałe.
    const bool allowed = (enabled_ == nullptr) || enabled_();
    const u16  actual  = allowed ? frequency : 0;

    if (actual == currentFreq_) return;
    currentFreq_ = actual;
    if (sink_) sink_(actual, loud);
}

void Tones::startNote(u16 frequency, u16 durationMs) {
    bool loud = (frequency & kToneHighVolume) != 0;
    frequency = static_cast<u16>(frequency & ~kToneHighVolume);

    if (volumeMode_ == kVolumeAlwaysHigh)   loud = true;
    if (volumeMode_ == kVolumeAlwaysNormal) loud = false;

    remainingMs_ = static_cast<i32>(durationMs);
    emit(frequency, loud);
}

void Tones::tone(u16 frequency, u16 durationMs) {
    noTone();
    inline_[0]   = frequency;
    inline_[1]   = durationMs;
    inlineCount_ = 1;
    inlineIndex_ = 0;
    startNote(frequency, durationMs);
    lastUpdateMs_ = static_cast<u32>(rtos::nowMs());
    primed_ = true;
}

void Tones::tone(u16 f1, u16 d1, u16 f2, u16 d2) {
    tone(f1, d1);
    inline_[2]   = f2;
    inline_[3]   = d2;
    inlineCount_ = 2;
}

void Tones::tone(u16 f1, u16 d1, u16 f2, u16 d2, u16 f3, u16 d3) {
    tone(f1, d1, f2, d2);
    inline_[4]   = f3;
    inline_[5]   = d3;
    inlineCount_ = 3;
}

void Tones::tones(const u16* sequence) {
    noTone();
    if (sequence == nullptr) return;

    sequence_ = sequence;
    cursor_   = sequence;
    lastUpdateMs_ = static_cast<u32>(rtos::nowMs());
    primed_ = true;
    advance();
}

void Tones::noTone() {
    sequence_    = nullptr;
    cursor_      = nullptr;
    inlineCount_ = 0;
    inlineIndex_ = 0;
    remainingMs_ = 0;
    emit(0, false);
}

/** Bierze kolejną nutę z bieżącego źródła albo kończy odtwarzanie. */
void Tones::advance() {
    if (sequence_ != nullptr) {
        const u16 frequency = *cursor_++;

        if (frequency == kTonesEnd) {
            noTone();
            return;
        }
        if (frequency == kTonesRepeat) {
            cursor_ = sequence_;
            advance();
            return;
        }

        const u16 duration = *cursor_++;
        startNote(frequency, duration);
        return;
    }

    if (inlineIndex_ + 1 < inlineCount_) {
        ++inlineIndex_;
        startNote(inline_[inlineIndex_ * 2], inline_[inlineIndex_ * 2 + 1]);
        return;
    }

    noTone();
}

void Tones::update() {
    if (!playing()) return;

    const u32 now = static_cast<u32>(rtos::nowMs());
    if (!primed_) {
        // Pierwsze wywołanie ustala tylko punkt odniesienia. Bez tego melodia
        // rozpoczęta tuż po starcie systemu dostałaby na wejściu cały czas,
        // jaki upłynął od zera, i przeskoczyła do końca w jednej klatce.
        lastUpdateMs_ = now;
        primed_ = true;
        return;
    }

    const u32 elapsed = now - lastUpdateMs_;
    lastUpdateMs_ = now;

    remainingMs_ -= static_cast<i32>(elapsed);

    // Pętla, nie „if": przy krótkich nutach i wolnej klatce jedno wywołanie
    // może przeskoczyć kilka nut naraz. Warunek `playing()` przerywa ją,
    // gdy melodia dobiegnie końca w trakcie.
    while (remainingMs_ <= 0 && playing()) {
        const i32 overshoot = remainingMs_;
        advance();
        if (!playing()) break;
        // Nadmiar z poprzedniej nuty przechodzi na następną, żeby melodia
        // nie rozjeżdżała się rytmicznie przy każdej dłuższej klatce.
        remainingMs_ += overshoot;
    }
}

}  // namespace arduboy
}  // namespace hydra

#endif  // HYDRA_ENABLE_ARDUBOY
