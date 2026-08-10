/**
 * Hydra — silnik skryptowy Lua za umową `IScriptEngine`.
 *
 * Same przekłady. Jedyne miejsce, gdzie coś się dzieje, to `load()`: Lua
 * przyjmuje tekst zakończony zerem, a umowa dopuszcza obraz z jawną długością.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_LUA

#include "hydra/script/LuaEngine.hpp"

namespace hydra {
namespace script {

Status LuaEngine::open(void* pool, size_t poolBytes) {
    Interp::Config icfg{};
    icfg.libs      = cfg_.libs;
    icfg.pool      = pool;
    icfg.poolBytes = poolBytes;
    return interp_.open(icfg);
}

Status LuaEngine::installBindings(const BindingSet& set) {
    // Kwalifikacja konieczna: nazwa metody przesłania wolną funkcję o tej samej
    // nazwie, a to na nią chodzi.
    return ::hydra::script::installBindings(interp_, set);
}

void LuaEngine::removeBindings() {
    ::hydra::script::removeBindings(interp_);
}

u32 LuaEngine::dispatchSignals(u32 maxSignals) {
    return ::hydra::script::dispatchSignals(interp_, maxSignals);
}

Status LuaEngine::load(const void* image, size_t bytes, const char* name) {
    if (image == nullptr) return fail(Err::BadArgument);

    // Lua kompiluje z tekstu zakończonego zerem i nie przyjmuje długości.
    // Umowa dopuszcza `bytes`, bo silnik binarny bez niej się nie obejdzie —
    // tutaj służy wyłącznie do wykrycia obrazu, który tekstem nie jest.
    const char* source = static_cast<const char*>(image);
    if (bytes > 0 && source[bytes - 1] != '\0') {
        interp_.setError("obraz nie jest tekstem zakonczonym zerem");
        return fail(Err::BadArgument);
    }

    return interp_.doString(source, name);
}

IScriptEngine::JobState LuaEngine::translate(Job::State state) {
    switch (state) {
        case Job::State::Idle:    return JobState::Idle;
        case Job::State::Running: return JobState::Running;
        case Job::State::Done:    return JobState::Done;
        case Job::State::Failed:  return JobState::Failed;
    }
    // `JobState::Exhausted` nie ma tu odpowiednika i mieć nie będzie: korutyna
    // Lua zawsze daje się wznowić, więc wyczerpany budżet to `Running`.
    return JobState::Idle;
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_SCRIPT_ENGINE_LUA
