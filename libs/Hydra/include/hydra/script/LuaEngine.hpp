#pragma once
/**
 * @file LuaEngine.hpp
 * @brief Lua za interfejsem `IScriptEngine`.
 *
 * Cienka warstwa nad `Interp` i `Job` — nie wnosi zachowania, tylko nadaje
 * istniejącemu interpreterowi kształt wspólny z pozostałymi silnikami.
 *
 * Dostęp do `Interp` zostaje wystawiony przez `interp()`, bo bindingi Hydry
 * rejestrują się w sposób właściwy dla Lua (`registerLib`, `NativeFn` operujące
 * na `Ctx`). Nie jest to wyciek abstrakcji: warstwa bindingów z definicji zna
 * język, w którym woła, a jej ujednolicenie to osobna decyzja — dopiero
 * w chwili, gdy drugi silnik będzie miał czym te same funkcje udostępnić.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT

#include "hydra/script/IScriptEngine.hpp"
#include "hydra/script/Script.hpp"

namespace hydra {
namespace script {

class LuaEngine : public IScriptEngine {
public:
    LuaEngine() = default;

    /** Ustawienia interpretera. Podaje się je przed `open()`. */
    void configure(const Interp::Config& cfg) { cfg_ = cfg; }

    EngineInfo info() const override {
        EngineInfo out;
        out.name       = "lua";
        out.language   = ScriptLanguage::Lua;
        // Jedyny silnik, który da się przerwać w punkcie i wznowić od niego.
        out.preemption = Preemption::Cooperative;
        out.acceptsSource = true;
        // Lua kompiluje też własny bytecode, ale wczytywanie go z sieci to
        // wykonywanie cudzych instrukcji maszyny bez weryfikacji — dopóki
        // nie ma sprawdzania podpisu, moduł tej drogi nie wystawia.
        out.acceptsBinary = false;
        return out;
    }

    Status open() override { return interp_.open(cfg_); }
    void   close() override;
    bool   ready() const override { return interp_.ready(); }

    Status loadSource(const char* source, const char* chunkName) override {
        return interp_.doString(source, chunkName);
    }

    bool   hasFunction(const char* name) const override { return interp_.hasFunction(name); }
    Status callFunction(const char* name) override { return interp_.callGlobal(name); }

    Status   startJob(const char* name) override { return job_.start(interp_, name); }
    RunState resumeJob(u32 budget) override;
    RunState jobState() const override;
    u32      jobSteps() const override { return job_.steps(); }
    void     cancelJob() override { job_.cancel(); }

    const char*  error() const override { return interp_.error(); }
    void         clearError() override { interp_.clearError(); }
    ScriptMemory memory() const override;
    u32          collect() override { return interp_.collect(); }

    /** Interpreter pod spodem — dla warstwy bindingów. */
    Interp& interp() { return interp_; }

private:
    static RunState translate(Job::State state);

    Interp         interp_{};
    Job            job_{};
    Interp::Config cfg_{};
};

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT
