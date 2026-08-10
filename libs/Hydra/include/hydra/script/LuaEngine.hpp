#pragma once
/**
 * Hydra — silnik skryptowy oparty na osadzonym Lua.
 *
 * Cienka warstwa nad `Interp` i `Job`: cała mechanika interpretera została tam,
 * gdzie była, a tutaj jest wyłącznie przełożenie jej na umowę
 * `IScriptEngine`. Osobna klasa zamiast dziedziczenia przez `Interp` po
 * interfejsie, bo `Interp` ma być użyteczny sam — bez modułu, bez bindingów
 * i bez tablicy metod wirtualnych, której nikt nie zawoła.
 *
 *     script::LuaEngine engine;
 *     script::ScriptModule::Config cfg;
 *     cfg.engine = &engine;
 *     cfg.source = kSkrypt;
 *
 * Kto potrzebuje czegoś, czego umowa nie obejmuje — własnych bibliotek
 * natywnych, `registerLib()`, `setGlobal*()` — sięga po `interp()`. To jest
 * właściwy sposób: rzeczy specyficzne dla Lua zostają na typie Lua, zamiast
 * pęcznieć w interfejsie, który ma opisywać też WebAssembly.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_LUA

#include "hydra/script/IScriptEngine.hpp"
#include "hydra/script/Script.hpp"

namespace hydra {
namespace script {

class LuaEngine final : public IScriptEngine {
public:
    struct Config {
        /** Które biblioteki standardowe otworzyć. */
        Interp::Libs libs{};
    };

    LuaEngine() = default;
    explicit LuaEngine(const Config& cfg) : cfg_(cfg) {}

    /** Wołać przed `open()`. Później nie ma znaczenia — biblioteki otwiera się raz. */
    void configure(const Config& cfg) { cfg_ = cfg; }

    /** Interpreter pod spodem. Dla wszystkiego, czego `IScriptEngine` nie obejmuje. */
    Interp&       interp() { return interp_; }
    const Interp& interp() const { return interp_; }

    // --- IScriptEngine -----------------------------------------------------

    const char* name() const override { return "lua"; }

    Status open(void* pool, size_t poolBytes) override;
    void   close() override { interp_.close(); }
    bool   ready() const override { return interp_.ready(); }

    Status installBindings(const BindingSet& set) override;
    void   removeBindings() override;
    u32    dispatchSignals(u32 maxSignals) override;

    Status load(const void* image, size_t bytes, const char* name) override;
    bool   hasFunction(const char* fn) const override { return interp_.hasFunction(fn); }
    Status call(const char* fn) override { return interp_.callGlobal(fn); }

    Status   jobBegin(const char* fn) override { return job_.start(interp_, fn); }
    JobState jobStep(u32 budget) override { return translate(job_.resume(budget)); }
    JobState jobState() const override { return translate(job_.state()); }
    void     jobCancel() override { job_.cancel(); }
    u32      jobSteps() const override { return job_.steps(); }

    Heap::Stats memory() const override { return interp_.memory(); }
    u32         collect() override { return interp_.collect(); }
    const char* error() const override { return interp_.error(); }

    Status eval(const char* source, const char* name) override {
        return interp_.doString(source, name);
    }

private:
    static JobState translate(Job::State state);

    Interp interp_{};
    Job    job_{};
    Config cfg_{};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_SCRIPT_ENGINE_LUA
