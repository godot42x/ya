#pragma once

#include "Core/Api.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace ya
{

struct Scene;

/**
 * @brief ScriptApiRegistry - the single engine capability registry.
 *
 * Every engine operation exposed to scripts / agents is a named callable with
 * a JSON argument contract and a doc string. JS bindings, the automation RPC
 * (`invoke` / `list_commands`) and the agent-facing schema all consume this
 * one registry, so a new capability is registered once and appears everywhere.
 *
 * Component / entity access is intentionally NOT hardcoded here: the core
 * registrations in ScriptApiCore.cpp build on the existing ECSRegistry
 * (name -> component ops) and ReflectionSerializer (component <-> JSON).
 */
struct ENGINE_API ScriptApiRegistry
{
    using Json     = nlohmann::json;
    using Callable = std::function<Json(const Json& args)>;

    using ActiveSceneProvider = std::function<Scene*()>;
    using SaveSceneFn         = std::function<bool(const std::string& path, Scene& scene)>;
    using LoadSceneFn         = std::function<bool(const std::string& path)>;

    struct FunctionInfo
    {
        std::string name;
        std::string doc;
        Json        argSchema = Json::object();
        Callable    callable;
    };

    /// Thrown by registered callables; the registry turns it into an error string.
    struct Error : std::runtime_error
    {
        explicit Error(const std::string& message) : std::runtime_error(message) {}
    };

    void setActiveSceneProvider(ActiveSceneProvider provider) { _activeSceneProvider = std::move(provider); }
    void setSaveSceneFn(SaveSceneFn fn) { _saveSceneFn = std::move(fn); }
    void setLoadSceneFn(LoadSceneFn fn) { _loadSceneFn = std::move(fn); }

    void registerFunction(std::string name, std::string doc, Json argSchema, Callable callable);

    /// Returns false and fills outError on unknown name or callable failure.
    bool invoke(const std::string& name, const Json& args, Json& outResult, std::string& outError) const;

    /// JSON array of {name, doc, args} for capability discovery.
    [[nodiscard]] Json buildCommandList() const;

    [[nodiscard]] const std::unordered_map<std::string, FunctionInfo>& functions() const { return _functions; }

    [[nodiscard]] Scene* getActiveScene() const { return _activeSceneProvider ? _activeSceneProvider() : nullptr; }
    [[nodiscard]] bool   saveScene(const std::string& path, Scene& scene) const
    {
        return _saveSceneFn && _saveSceneFn(path, scene);
    }
    [[nodiscard]] bool loadScene(const std::string& path) const { return _loadSceneFn && _loadSceneFn(path); }

    static ScriptApiRegistry& get();

  private:
    std::unordered_map<std::string, FunctionInfo> _functions;
    ActiveSceneProvider                           _activeSceneProvider;
    SaveSceneFn                                   _saveSceneFn;
    LoadSceneFn                                   _loadSceneFn;
};

/// Idempotent registration of the core authoring API (scene / entity / component).
ENGINE_API void registerCoreScriptApis(ScriptApiRegistry& registry);

} // namespace ya
