#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/LuaEngine.hpp"

namespace hydra {
namespace script {

RunState LuaEngine::translate(Job::State state) {
    switch (state) {
        case Job::State::Idle:    return RunState::Idle;
        case Job::State::Running: return RunState::Running;
        case Job::State::Done:    return RunState::Done;
        case Job::State::Failed:  return RunState::Failed;
    }
    return RunState::Idle;
}

void LuaEngine::close() {
    // Porzucenie zadania przed zamknięciem interpretera, nie po: `Job` trzyma
    // wątek Lua zakotwiczony w rejestrze, a zamknięty interpreter nie ma już
    // rejestru, z którego dałoby się go zdjąć.
    job_.cancel();
    interp_.close();
}

RunState LuaEngine::resumeJob(u32 budget) {
    return translate(job_.resume(budget));
}

RunState LuaEngine::jobState() const {
    return translate(job_.state());
}

ScriptMemory LuaEngine::memory() const {
    const Heap::Stats stats = interp_.memory();

    ScriptMemory out;
    out.capacityBytes = stats.capacity;
    out.usedBytes     = stats.used;
    out.peakBytes     = stats.peak;
    return out;
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
