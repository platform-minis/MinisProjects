#pragma once
/**
 * Hydra — interfejs modułu (rozdz. 4.1).
 *
 * Każdy moduł (ui, net, sense, motion, a także moduły aplikacji) implementuje
 * init() / start() / stop(). App wywołuje je w deterministycznej kolejności
 * rejestracji, co daje powtarzalny start i możliwość miękkiego restartu
 * pojedynczego podsystemu — np. restart sieci bez restartu robota.
 *
 * Kontrakt faz:
 *   init()  — konfiguracja i wszystkie alokacje; brak tasków, brak I/O w tle,
 *   start() — utworzenie tasków i subskrypcji; po powrocie moduł działa,
 *   stop()  — zatrzymanie tasków i zwolnienie subskrypcji; musi być idempotentne
 *             i bezpieczne do wywołania po nieudanym init().
 */

#include "hydra/core/Events.hpp"
#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {

class IModule {
public:
    virtual ~IModule() = default;

    /** Krótka, stała nazwa modułu — również klucz logów i telemetrii ("net", "sense"). */
    virtual const char* name() const = 0;

    virtual Status init()  = 0;
    virtual Status start() = 0;
    virtual void   stop()  = 0;

    virtual ModuleState state() const = 0;
};

/**
 * Baza z gotową obsługą stanu i publikacją ModuleStateChanged.
 * Moduł implementuje wyłącznie onInit/onStart/onStop.
 */
class ModuleBase : public IModule {
public:
    explicit ModuleBase(const char* name) : name_(name), id_(nameId(name)) {}

    const char* name()  const override { return name_; }
    u16         id()    const { return id_; }
    ModuleState state() const override { return state_; }

    Status init() override {
        if (state_ == ModuleState::Initialized || state_ == ModuleState::Running) return ok();
        auto r = onInit();
        setState(r ? ModuleState::Initialized : ModuleState::Failed, r.error());
        return r;
    }

    Status start() override {
        if (state_ == ModuleState::Running) return ok();
        if (state_ != ModuleState::Initialized && state_ != ModuleState::Stopped) {
            return fail(Err::NotInitialized);
        }
        auto r = onStart();
        setState(r ? ModuleState::Running : ModuleState::Failed, r.error());
        return r;
    }

    void stop() override {
        if (state_ == ModuleState::Created || state_ == ModuleState::Stopped) return;
        onStop();
        setState(ModuleState::Stopped, Err::None);
    }

protected:
    virtual Status onInit()  = 0;
    virtual Status onStart() = 0;
    virtual void   onStop()  = 0;

    void setState(ModuleState s, Err e = Err::None) {
        state_ = s;
        EventBus::publish(ModuleStateChanged{id_, s, e});
    }

private:
    const char* name_;
    u16         id_;
    ModuleState state_ = ModuleState::Created;
};

}  // namespace hydra
