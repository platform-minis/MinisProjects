/** Hydra — implementacja huba czujników (rozdz. 8). */

#include "hydra/sense/SensorHub.hpp"

#if HYDRA_ENABLE_SENSE

#include "hydra/core/Events.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/hal/Hal.hpp"

HYDRA_LOG_MODULE("sense")

namespace hydra {
namespace sense {

u32 gcd(u32 a, u32 b) {
    while (b != 0) {
        const u32 t = b;
        b = a % b;
        a = t;
    }
    return a;
}

// ---------------------------------------------------------------------------
// Rejestracja
// ---------------------------------------------------------------------------

Result<u8> SensorHub::add(ISensor& sensor, const Registration& reg) {
    if (state() != ModuleState::Created) return unexpected(Err::AlreadyExists);
    if (count_ >= HYDRA_SENSE_MAX_SENSORS) return unexpected(Err::OutOfMemory);
    if (sensor.channels() == 0 || sensor.channels() > kMaxChannels) {
        return unexpected(Err::BadArgument);
    }
    if (sensor.pollMode() == PollMode::Periodic && reg.sensor.periodMs == 0) {
        return unexpected(Err::BadArgument);
    }
    if (sensor.pollMode() == PollMode::DataReadyIrq && reg.sensor.irqPin == hal::kNoPin) {
        return unexpected(Err::BadArgument);
    }

    Entry& e    = entries_[count_];
    e.sensor    = &sensor;
    e.cfg       = reg;
    e.topic     = nameId(sensor.name());
    e.mode      = sensor.pollMode();
    e.channels  = sensor.channels();
    e.index     = count_;

    return count_++;
}

bool SensorHub::available(u8 index) const {
    return index < count_ && entries_[index].ready;
}

TopicId SensorHub::topicOf(u8 index) const {
    return index < count_ ? entries_[index].topic : kInvalidTopic;
}

u16 SensorHub::dividerOf(u8 index) const {
    return index < count_ ? entries_[index].divider : 0;
}

SensorHub::Stats SensorHub::stats(u8 index) const {
    return index < count_ ? entries_[index].stats : Stats{};
}

SensorCal SensorHub::calibration(u8 index) const {
    return index < count_ ? entries_[index].cal : SensorCal{};
}

Status SensorHub::setCalibration(u8 index, const SensorCal& cal, bool persist) {
    if (index >= count_) return fail(Err::NotFound);
    entries_[index].cal = cal;
    if (!persist) return ok();
    return Calibration::save(entries_[index].sensor->name(), cal);
}

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status SensorHub::onInit() {
    if (count_ == 0) {
        HYDRA_LOGW("brak zarejestrowanych czujników");
    }

    // Okres tyknięcia to GCD okresów czujników okresowych. Czujniki data-ready
    // i swobodne nie narzucają okresu, ale muszą być obsługiwane wystarczająco
    // często — stąd domyślne 100 ms, gdy nie ma żadnego czujnika okresowego.
    u32 tick = 0;
    for (u8 i = 0; i < count_; ++i) {
        if (entries_[i].mode != PollMode::Periodic) continue;
        tick = (tick == 0) ? entries_[i].cfg.sensor.periodMs
                           : gcd(tick, entries_[i].cfg.sensor.periodMs);
    }
    tickMs_ = tick > 0 ? tick : 100;

    for (u8 i = 0; i < count_; ++i) {
        Entry& e = entries_[i];

        if (e.mode == PollMode::Periodic) {
            const u32 div = e.cfg.sensor.periodMs / tickMs_;
            if (div > 0xFFFF) {
                HYDRA_LOGE("%s: okres %lu ms nie da się wyrazić w tyknięciach %lu ms",
                           e.sensor->name(),
                           static_cast<unsigned long>(e.cfg.sensor.periodMs),
                           static_cast<unsigned long>(tickMs_));
                return fail(Err::OutOfRange);
            }
            e.divider = static_cast<u16>(div);
            // Rozsuwamy fazy czujników o ten sam okres, żeby nie odpytywać ich
            // wszystkich w jednym tyknięciu — inaczej jedno tyknięcie na kilka
            // byłoby długie, a reszta pusta.
            e.counter = static_cast<u16>(i % (e.divider ? e.divider : 1));
        } else {
            e.divider = 1;
            e.counter = 0;
        }

        // Wykrycie układu. Nieobecny czujnik nie blokuje startu urządzenia.
        if (auto r = e.sensor->probe(); !r) {
            HYDRA_LOGW("%s: nie odpowiada (%s) — pominięty", e.sensor->name(),
                       toString(r.error()));
            e.ready = false;
            continue;
        }
        if (auto r = e.sensor->configure(e.cfg.sensor); !r) {
            HYDRA_LOGE("%s: konfiguracja nieudana (%s)", e.sensor->name(),
                       toString(r.error()));
            e.ready = false;
            continue;
        }

        if (auto r = Calibration::load(e.sensor->name(), e.cal); !r) {
            HYDRA_LOGW("%s: kalibracja niewczytana (%s) — współczynniki neutralne",
                       e.sensor->name(), toString(r.error()));
        }

        // Filtr Butterwortha musi znać częstotliwość próbkowania, a tę wyznacza
        // dopiero harmonogram — dlatego konfigurujemy go tutaj, nie przy add().
        FilterCfg fc = e.cfg.filter;
        if (fc.kind == FilterKind::Butterworth && fc.sampleHz <= 0.0f) {
            const u32 periodMs = tickMs_ * e.divider;
            fc.sampleHz = periodMs > 0 ? 1000.0f / static_cast<float>(periodMs) : 1.0f;
        }
        for (u8 c = 0; c < e.channels; ++c) {
            if (auto r = e.filter[c].configure(fc); !r) {
                HYDRA_LOGE("%s: błędna konfiguracja filtru (%s)", e.sensor->name(),
                           toString(r.error()));
                return r;
            }
        }

        e.anomaly.configure(e.cfg.anomaly);
        e.ready = true;

        HYDRA_LOGI("%s: gotowy, %u kan., co %lu ms", e.sensor->name(),
                   static_cast<unsigned>(e.channels),
                   static_cast<unsigned long>(tickMs_ * e.divider));
    }

    return ok();
}

Status SensorHub::onStart() {
    // Skrzynka odbiera zgłoszenia data-ready odłożone z przerwań, dzięki czemu
    // odczyt magistrali dzieje się w tasku sense.poll, a nie w ISR ani
    // w kontekście taska porządkowego.
    HYDRA_CHECK(inbox_.create(HYDRA_SENSE_INBOX_DEPTH));

    auto sub = EventBus::subscribe<SensorDataReady>(inbox_, [this](const SensorDataReady& e) {
        if (e.index >= count_) return;
        Entry& entry = entries_[e.index];
        if (!entry.ready) return;
        process(entry, e.t_us);
    });
    if (!sub) return fail(sub.error());
    dataReadySub_ = *sub;

    for (u8 i = 0; i < count_; ++i) {
        Entry& e = entries_[i];
        if (!e.ready || e.mode != PollMode::DataReadyIrq) continue;

        HYDRA_CHECK(hal::Hal::gpio().configure(e.cfg.sensor.irqPin, hal::PinMode::InputPullUp));
        if (auto r = hal::Hal::gpio().attachInterrupt(e.cfg.sensor.irqPin,
                                                     e.cfg.sensor.irqEdge,
                                                     &SensorHub::onDataReadyIsr, &e);
            !r) {
            HYDRA_LOGE("%s: nie udało się podpiąć przerwania (%s)", e.sensor->name(),
                       toString(r.error()));
            return r;
        }
    }

    Task::Cfg cfg;
    cfg.name = "sense.poll";
    cfg.prio = Prio::High;
    cfg.core = Core::Core1;  // razem z pętlą sterowania, z dala od sieci i UI
    return task_.startPeriodic(cfg, tickMs_, [this] { tick(); });
}

void SensorHub::onStop() {
    task_.stopAndWait();

    for (u8 i = 0; i < count_; ++i) {
        Entry& e = entries_[i];
        if (e.mode == PollMode::DataReadyIrq && e.cfg.sensor.irqPin != hal::kNoPin) {
            hal::Hal::gpio().detachInterrupt(e.cfg.sensor.irqPin);
        }
    }

    if (dataReadySub_ != kInvalidSub) {
        EventBus::unsubscribe(dataReadySub_);
        dataReadySub_ = kInvalidSub;
    }
}

// ---------------------------------------------------------------------------
// Przerwanie i pętla
// ---------------------------------------------------------------------------

HYDRA_ISR_ATTR void SensorHub::onDataReadyIsr(void* arg) {
    auto* e = static_cast<Entry*>(arg);
    // Wszystko, co wolno zrobić w przerwaniu: odczyt zegara i publikacja
    // zdarzenia. Bez magistral, bez alokacji, bez logowania (rozdz. 10).
    EventBus::publishFromIsr(SensorDataReady{rtos::nowUs(), e->index});
}

void SensorHub::tick() {
    // Najpierw dane zgłoszone przerwaniem — są starsze niż to tyknięcie.
    inbox_.pump(0);

    for (u8 i = 0; i < count_; ++i) {
        Entry& e = entries_[i];
        if (!e.ready || e.mode == PollMode::DataReadyIrq) continue;

        if (e.mode == PollMode::Periodic) {
            if (++e.counter < e.divider) continue;
            e.counter = 0;
        }
        process(e, 0);
    }
}

Status SensorHub::pollOnce(u8 index) {
    if (index >= count_) return fail(Err::NotFound);
    if (!entries_[index].ready) return fail(Err::NotInitialized);
    return process(entries_[index], 0);
}

// ---------------------------------------------------------------------------
// Łańcuch przetwarzania
// ---------------------------------------------------------------------------

Status SensorHub::process(Entry& e, Micros stampUs) {
    Sample s;
    // Znacznik pobierany PRZED transferem: pomiar istniał już wtedy, a odczyt
    // przez I2C potrafi trwać setki mikrosekund. W trybie data-ready dostajemy
    // znacznik z ISR i jest on jeszcze dokładniejszy.
    const Micros t = stampUs != 0 ? stampUs : hal::Hal::time().sampleStamp();

    auto r = e.sensor->read(s);
    if (!r) {
        if (r.error() == Err::WouldBlock) {
            ++e.stats.skipped;
            ++total_.skipped;
            return r;
        }
        ++e.stats.faults;
        ++total_.faults;
        ++e.consecutiveFaults;
        EventBus::publish(SensorFault{e.topic, r.error(), e.consecutiveFaults});
        HYDRA_LOGW("%s: odczyt nieudany (%s), z rzędu: %lu", e.sensor->name(),
                   toString(r.error()), static_cast<unsigned long>(e.consecutiveFaults));
        return r;
    }

    e.consecutiveFaults = 0;
    ++e.stats.reads;
    ++total_.reads;

    s.topic = e.topic;
    // Czujnik może podać własny, dokładniejszy znacznik (np. z bufora FIFO).
    if (s.t_us == 0) s.t_us = t;
    if (s.n > e.channels) s.n = e.channels;

    Calibration::apply(e.cal, s);
    for (u8 c = 0; c < s.n; ++c) s.value[c] = e.filter[c].apply(s.value[c]);

    const auto hit = e.anomaly.check(s);
    if (hit.kind != AnomalyKind::None) {
        // Zamrożona wartość to dane nieaktualne, skok i przekroczenie zakresu
        // to dane podejrzane — subskrybent widzi różnicę bez zaglądania
        // w osobne zdarzenie.
        s.q = (hit.kind == AnomalyKind::Frozen) ? Quality::Stale : Quality::Suspect;
        ++e.stats.anomalies;
        ++total_.anomalies;
        EventBus::publish(SensorAnomaly{e.topic, hit.kind, hit.channel, hit.value});
    }

    EventBus::publish(s);
    ++e.stats.published;
    ++total_.published;
    return ok();
}

}  // namespace sense
}  // namespace hydra

#endif  // HYDRA_ENABLE_SENSE
