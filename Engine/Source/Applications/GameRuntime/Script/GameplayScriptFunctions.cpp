#include "GameRuntime/Script/GameplayScriptFunctions.h"

#include "GameRuntime/App.h"

#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Entity.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"

#include <format>

namespace ya
{
namespace
{

using script::ScriptArgs;
using script::ScriptError;
using script::ScriptValue;

void expectArgCount(ScriptArgs args, size_t count)
{
    if (args.size() != count) {
        throw ScriptError(std::format("expects {} argument(s), got {}", count, args.size()));
    }
}

ScriptValue find(ScriptArgs args)
{
    expectArgCount(args, 1);
    App*   app   = App::get();
    Scene* scene = app ? app->getSceneServices().getActiveScene() : nullptr;
    if (!scene) {
        throw ScriptError("no active scene");
    }
    const Entity* entity = scene->getEntityByName(script::scriptToString(args[0]));
    if (!entity) {
        return {};
    }
    return script::entityRef(entity);
}

ScriptValue viewSize(ScriptArgs args)
{
    expectArgCount(args, 0);
    App* app = App::get();
    if (!app) {
        return glm::vec2(0.0f);
    }
    const Extent2D resolution = app->getRenderServices().getRenderResolution();
    return glm::vec2(static_cast<float>(resolution.width), static_cast<float>(resolution.height));
}

} // namespace

void registerGameplayScriptFunctions()
{
    script::registerModuleFunction("world", "find", &find);
    script::registerModuleFunction("world", "viewSize", &viewSize);
}

} // namespace ya
