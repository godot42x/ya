#pragma once

#include "Core/Api.h"
#include "Core/Scripting/ScriptApiRegistry.h"
#include "Core/System/System.h"

#include <memory>
#include <string>

namespace ya
{

/**
 * @brief JSScriptingSystem - engine-level quickjs-ng scripting runtime.
 *
 * Binds every function of the ScriptApiRegistry into the JS global `ya` table
 * (e.g. `ya.entity.create({name: "Box"})`) and exposes evalJS() / invoke()
 * for the automation RPC. This is the shared engine-side scripting surface:
 * gameplay scripts and external agents call the exact same registered APIs.
 *
 * The quickjs runtime lives behind a pimpl so this header stays free of
 * quickjs headers.
 */
struct YA_ECS_API JSScriptingSystem : public ISystem
{
    struct EvalResult
    {
        bool                   ok = false;
        ScriptApiRegistry::Json value = nullptr;
        std::string            error;
    };

    JSScriptingSystem();
    ~JSScriptingSystem() override;
    JSScriptingSystem(const JSScriptingSystem&) = delete;
    JSScriptingSystem& operator=(const JSScriptingSystem&) = delete;

    void init() override;
    void shutdown() override;

    /// Evaluates a JS snippet in the shared global context. The last
    /// expression's value is returned as JSON (null when the result is
    /// undefined). On JS exceptions ok=false and error carries the message.
    EvalResult evalJS(const std::string& source, const std::string& filename = "<eval>");

    /// Direct registry invocation with the same semantics as the JS wrappers.
    bool invoke(const std::string& name,
                const ScriptApiRegistry::Json& args,
                ScriptApiRegistry::Json&       outResult,
                std::string&                   outError);

    [[nodiscard]] ScriptApiRegistry::Json buildCommandList() const
    {
        return ScriptApiRegistry::get().buildCommandList();
    }

  public:
    /// Opaque quickjs runtime state (defined in JSScriptingSystem.cpp). Public
    /// so the binding helpers in the translation unit can reference it.
    struct Impl;

  private:
    std::unique_ptr<Impl> _impl;
};

} // namespace ya
