#include "Scene/Core/SceneScriptBindings.h"

#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Component.h"
#include "ECS/ECSRegistry.h"
#include "ECS/Entity.h"
#include "Scene/Core/Scene.h"

#include <format>

namespace ya::script
{

namespace
{

struct SceneRefKinds
{
    uint32_t scene     = 0;
    uint32_t entity    = 0;
    uint32_t component = 0;
};

const SceneRefKinds& kinds();

Entity* resolveEntity(uint64_t sceneId, uint64_t handle)
{
    Scene* scene = Scene::findByInstanceId(sceneId);
    if (!scene) {
        return nullptr;
    }
    Entity* entity = scene->getEntityByEnttID(static_cast<entt::entity>(handle));
    return scene->isValidEntity(entity) ? entity : nullptr;
}

type_index_t componentTypeNamed(const std::string& typeName)
{
    const auto type = ECSRegistry::get().getTypeIndex(FName(typeName));
    if (!type) {
        throw ScriptError(std::format("unknown component type '{}'", typeName));
    }
    return *type;
}

void* componentOn(Entity& entity, type_index_t type)
{
    entt::registry* registry = entity.getRegistry();
    return registry ? ECSRegistry::get().getComponent(type, *registry, entity.getHandle()) : nullptr;
}

void expectArgs(ScriptArgs args, size_t count)
{
    if (args.size() != count) {
        throw ScriptError(std::format("expects {} argument(s), got {}", count, args.size()));
    }
}

ScriptNativeFn entityMethod(ScriptValue (*body)(Entity&, ScriptArgs))
{
    return [body](void* self, const ScriptRef&, ScriptArgs args) { return body(*static_cast<Entity*>(self), args); };
}

/// `getTransform()` / `hasCamera()` and so on, for whichever components are
/// registered when the script asks.
std::optional<ScriptNativeFn> resolveComponentAccessor(std::string_view methodName)
{
    const bool bGet = methodName.starts_with("get");
    if ((!bGet && !methodName.starts_with("has")) || methodName.size() <= 3) {
        return std::nullopt;
    }
    const std::string shortName(methodName.substr(3));
    auto&             ecs  = ECSRegistry::get();
    auto              type = ecs.getTypeIndex(FName(shortName + "Component"));
    if (!type) {
        type = ecs.getTypeIndex(FName(shortName));
    }
    if (!type) {
        return std::nullopt;
    }
    const type_index_t componentType = *type;
    return ScriptNativeFn{[componentType, bGet](void* self, const ScriptRef&, ScriptArgs args) -> ScriptValue {
        expectArgs(args, 0);
        auto&       entity  = *static_cast<Entity*>(self);
        const void* present = componentOn(entity, componentType);
        if (!bGet) {
            return present != nullptr;
        }
        return present ? ScriptValue{componentRef(&entity, componentType)} : ScriptValue{};
    }};
}

void registerEntityMethods()
{
    const type_index_t entityType = type_index_v<Entity>;
    addNativeMethod(entityType, "get", entityMethod([](Entity& entity, ScriptArgs args) -> ScriptValue {
                        expectArgs(args, 1);
                        const type_index_t type = componentTypeNamed(scriptToString(args[0]));
                        return componentOn(entity, type) ? ScriptValue{componentRef(&entity, type)} : ScriptValue{};
                    }));
    addNativeMethod(entityType, "has", entityMethod([](Entity& entity, ScriptArgs args) -> ScriptValue {
                        expectArgs(args, 1);
                        return componentOn(entity, componentTypeNamed(scriptToString(args[0]))) != nullptr;
                    }));
    addNativeMethod(entityType, "add", entityMethod([](Entity& entity, ScriptArgs args) -> ScriptValue {
                        expectArgs(args, 1);
                        const std::string  typeName = scriptToString(args[0]);
                        const type_index_t type     = componentTypeNamed(typeName);
                        return entity.addComponentByName(typeName) ? ScriptValue{componentRef(&entity, type)} : ScriptValue{};
                    }));
    addNativeMethod(entityType, "remove", entityMethod([](Entity& entity, ScriptArgs args) -> ScriptValue {
                        expectArgs(args, 1);
                        const std::string typeName = scriptToString(args[0]);
                        (void)componentTypeNamed(typeName);
                        return entity.removeComponentByName(typeName);
                    }));
    setMethodResolver(entityType, &resolveComponentAccessor);
}

SceneRefKinds registerKinds()
{
    SceneRefKinds out;
    out.scene = registerRefKind(ScriptRefKind{
        .name    = "scene",
        .resolve = [](const ScriptRef& ref) -> void* { return Scene::findByInstanceId(ref.a); },
    });
    out.entity = registerRefKind(ScriptRefKind{
        .name    = "entity",
        .resolve = [](const ScriptRef& ref) -> void* { return resolveEntity(ref.a, ref.b); },
    });
    out.component = registerRefKind(ScriptRefKind{
        .name    = "component",
        .resolve = [](const ScriptRef& ref) -> void* {
            Entity* entity = resolveEntity(ref.a, ref.b);
            return entity ? componentOn(*entity, ref.type) : nullptr;
        },
        .afterWrite = [](const ScriptRef&, void* component) { static_cast<IComponent*>(component)->onPostSerialize(); },
    });
    registerEntityMethods();
    return out;
}

const SceneRefKinds& kinds()
{
    static const SceneRefKinds registered = registerKinds();
    return registered;
}

} // namespace

void ensureSceneScriptBindings()
{
    (void)kinds();
}

ScriptRef sceneRef(const Scene* scene)
{
    if (!scene) {
        return {};
    }
    return ScriptRef{.type = type_index_v<Scene>, .kind = kinds().scene, .a = scene->getInstanceId()};
}

ScriptRef entityRef(const Entity* entity)
{
    const Scene* scene = entity ? entity->getScene() : nullptr;
    if (!scene) {
        return {};
    }
    return ScriptRef{
        .type = type_index_v<Entity>,
        .kind = kinds().entity,
        .a    = scene->getInstanceId(),
        .b    = entt::to_integral(entity->getHandle()),
    };
}

ScriptRef componentRef(const Entity* entity, type_index_t componentType)
{
    const Scene* scene = entity ? entity->getScene() : nullptr;
    if (!scene) {
        return {};
    }
    return ScriptRef{
        .type = componentType,
        .kind = kinds().component,
        .a    = scene->getInstanceId(),
        .b    = entt::to_integral(entity->getHandle()),
    };
}

Scene* sceneOf(const ScriptRef& ref)
{
    return ref.kind == kinds().scene ? Scene::findByInstanceId(ref.a) : nullptr;
}

Entity* entityOf(const ScriptRef& ref)
{
    return ref.kind == kinds().entity ? resolveEntity(ref.a, ref.b) : nullptr;
}

} // namespace ya::script
