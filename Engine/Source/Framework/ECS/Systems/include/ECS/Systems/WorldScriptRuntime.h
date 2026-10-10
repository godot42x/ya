#pragma once

#include "Core/Api.h"
#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Systems/ScriptInstance.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ya
{

struct InputManager;
struct Scene;
struct Entity;

/// Narrow services the world-script runtime may use. Injected by the host.
/// The runtime never reaches Host or App types.
struct WorldScriptServices
{
    const InputManager*                                             input = nullptr;
    std::function<bool()>                                           isMouseCaptured;
    std::function<double()>                                         elapsedSeconds;
    std::function<uint64_t()>                                       frameIndex;
    std::function<Scene*()>                                         activeScene;
    std::function<bool(const std::string& path, std::string& out)> readScript;
};

/// What GameRuntime drives. The concrete runtime lives in the language
/// backend. The destructor is virtual so GameRuntime can `delete` this
/// pointer across the DLL boundary; the derived destructor runs in the
/// backend via the vtable.
struct YA_ECS_SYSTEMS_API IWorldScriptRuntime
{
    virtual ~IWorldScriptRuntime() = default;

    virtual void setServices(WorldScriptServices services) = 0;
    virtual void init()                                     = 0;
    virtual void shutdown()                                 = 0;
    virtual void onUpdate(float deltaTime)                  = 0;
    virtual void onStop()                                   = 0;
    virtual void onEntityDestroying(Entity& entity)         = 0;
    virtual void resetPersistentState()                     = 0;
    /// JSON object text of the `Persist` table. Unsupported Lua values are
    /// dropped. An empty table is `{}`.
    [[nodiscard]] virtual std::string exportPersistentState() = 0;
    /// Replace `Persist` from a JSON object. False leaves the current table.
    virtual bool importPersistentState(std::string_view jsonText) = 0;
    /// Call `onSave` on each loaded world script so it can write into `Persist`.
    /// A script with no `onSave` is skipped.
    virtual void flushPersistentScripts() = 0;
    /// The next world-script init pass follows a save load. Scene transfer does
    /// not set this. Cleared when that pass finishes, and when play stops.
    virtual void markSaveRestored() = 0;
    [[nodiscard]] virtual bool saveRestored() const = 0;
    virtual void dropInstance(uint64_t runtimeId)           = 0;
};

using WorldScriptRuntimeFactory = IWorldScriptRuntime* (*)();

YA_ECS_SYSTEMS_API void                 setWorldScriptRuntimeFactory(WorldScriptRuntimeFactory factory);
YA_ECS_SYSTEMS_API IWorldScriptRuntime* createWorldScriptRuntime();

/// What one host adds to a freshly bound `self`. The language backend rawsets
/// these onto the table; the host never sees the language runtime.
struct ScriptSelfBuilder
{
    struct Field
    {
        std::string         name;
        script::ScriptValue value;
    };
    struct Method
    {
        std::string            name;
        script::ScriptFunction fn;
    };

    std::vector<Field>  fields;
    std::vector<Method> methods;

    void field(std::string name, script::ScriptValue value)
    {
        fields.push_back(Field{.name = std::move(name), .value = std::move(value)});
    }

    template <class T>
        requires(!std::is_same_v<std::decay_t<T>, script::ScriptValue>)
    void field(std::string name, T&& value)
    {
        field(std::move(name), script::scriptMake(std::forward<T>(value)));
    }

    void method(std::string name, script::ScriptFunction fn)
    {
        methods.push_back(Method{.name = std::move(name), .fn = std::move(fn)});
    }
};

/// What a script instance is attached to. The runtime keeps one host per live
/// instance and never learns the host's type.
struct IScriptInstanceHost
{
    virtual ~IScriptInstanceHost() = default;

    /// The instance this host carries (`runtimeId == instanceId`), or null once
    /// the host or the instance is gone. Called on every operation, so an
    /// instance stored in movable storage may be looked up afresh.
    [[nodiscard]] virtual ScriptInstance* resolve(uint64_t instanceId) = 0;
    /// Fields and methods the backend rawsets onto `self`. Lifecycle callbacks
    /// run this when the chunk is first bound and when the host object
    /// changes. Named calls still run it on every call.
    virtual void contributeSelf(ScriptSelfBuilder& out) = 0;
};

} // namespace ya
