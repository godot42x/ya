#include "ScriptApiRegistry.h"

#include "Core/FName.h"
#include "Core/Reflection/ECSRegistry.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <format>

namespace ya
{

namespace
{

using Json = ScriptApiRegistry::Json;
using Error = ScriptApiRegistry::Error;

Scene& requireActiveScene()
{
    Scene* scene = ScriptApiRegistry::get().getActiveScene();
    if (scene == nullptr) {
        throw Error("no active scene");
    }
    return *scene;
}

entt::entity requireEntityId(const Json& args, Scene& scene)
{
    const uint32_t id = args.value("id", 0u);
    Entity*        entity = scene.getEntityByEnttID(entt::entity{id});
    if (entity == nullptr) {
        throw Error(std::format("entity {} not found", id));
    }
    return entity->getHandle();
}

std::vector<std::string> listRegisteredComponentTypes()
{
    std::vector<std::string> names;
    for (const auto& [fname, typeIndex] : ECSRegistry::get().getTypeIndexCache()) {
        (void)typeIndex;
        names.push_back(fname.toString());
    }
    std::sort(names.begin(), names.end());
    return names;
}

Json serializeComponent(const Scene& scene, entt::entity entity, const std::string& typeName)
{
    auto&       ecs       = ECSRegistry::get();
    const auto  typeIndex = ecs.getTypeIndex(FName(typeName));
    if (!typeIndex) {
        throw Error("unknown component type: " + typeName);
    }
    void* const ptr = ecs.getComponent(*typeIndex, scene, entity);
    if (ptr == nullptr) {
        throw Error(std::format("entity does not have component '{}'", typeName));
    }
    return ReflectionSerializer::serializeByRuntimeReflection(ptr, *typeIndex, typeName);
}

Json entityToJson(const Scene& scene, const Entity& entity)
{
    Json components = Json::object();
    for (const std::string& typeName : listRegisteredComponentTypes()) {
        if (ECSRegistry::get().hasComponent(FName(typeName), scene, entity.getHandle())) {
            components[typeName] = serializeComponent(scene, entity.getHandle(), typeName);
        }
    }
    return Json{
        {"id", static_cast<uint32_t>(entity.getHandle())},
        {"name", entity.getName()},
        {"components", std::move(components)},
    };
}

} // namespace

void registerCoreScriptApis(ScriptApiRegistry& registry)
{
    static bool bRegistered = false;
    if (bRegistered) {
        return;
    }
    bRegistered = true;

    // ========================================================================
    // Scene
    // ========================================================================
    registry.registerFunction(
        "scene.get_active",
        "Returns the active scene summary {name, entityCount}, or null when no scene is active.",
        Json::object(),
        [](const Json&) -> Json {
            Scene* scene = ScriptApiRegistry::get().getActiveScene();
            if (scene == nullptr) {
                return Json(nullptr);
            }
            return Json{
                {"name", scene->getName()},
                {"entityCount", scene->_entityMap.size()},
            };
        });

    registry.registerFunction(
        "scene.save",
        "Serializes the active scene to the given path.",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene&    scene = requireActiveScene();
            const auto path = args.at("path").get<std::string>();
            if (!ScriptApiRegistry::get().saveScene(path, scene)) {
                throw Error("scene.save failed");
            }
            return Json{{"saved", true}, {"path", path}};
        });

    registry.registerFunction(
        "scene.load",
        "Loads a scene from the given path and activates it.",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            const auto path = args.at("path").get<std::string>();
            if (!ScriptApiRegistry::get().loadScene(path)) {
                throw Error("scene.load failed");
            }
            return Json{{"loaded", true}, {"path", path}};
        });

    // ========================================================================
    // Entity
    // ========================================================================
    registry.registerFunction(
        "entity.create",
        "Creates an entity in the active scene. Args: {name}. Returns {id, name}.",
        Json{{"name", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene&    scene = requireActiveScene();
            const auto name = args.value("name", "Entity");
            // createEntity is private to internal systems; scripts create a
            // node-backed entity through the public API instead.
            Entity* entity = scene.createNode3D(name)->getEntity();
            return Json{
                {"id", static_cast<uint32_t>(entity->getHandle())},
                {"name", entity->getName()},
            };
        });

    registry.registerFunction(
        "entity.destroy",
        "Destroys the entity with the given id. Args: {id}.",
        Json{{"id", {{"type", "integer"}}}},
        [](const Json& args) -> Json {
            Scene&  scene = requireActiveScene();
            Entity* entity = scene.getEntityByEnttID(entt::entity{args.at("id").get<uint32_t>()});
            if (entity == nullptr) {
                throw Error("entity not found");
            }
            scene.destroyEntity(entity);
            return Json{{"destroyed", true}};
        });

    registry.registerFunction(
        "entity.list",
        "Lists all entities in the active scene: [{id, name, components:{TypeName: fields}}].",
        Json::object(),
        [](const Json&) -> Json {
            Scene& scene = requireActiveScene();
            Json   entities = Json::array();
            for (const auto& [handle, entity] : scene._entityMap) {
                (void)handle;
                entities.push_back(entityToJson(scene, entity));
            }
            return entities;
        });

    registry.registerFunction(
        "entity.get",
        "Returns one entity by id: {id, name, components:{TypeName: fields}}.",
        Json{{"id", {{"type", "integer"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle{args.at("id").get<uint32_t>()};
            Entity* const      entity = scene.getEntityByEnttID(handle);
            if (entity == nullptr) {
                throw Error("entity not found");
            }
            return entityToJson(scene, *entity);
        });

    // ========================================================================
    // Component
    // ========================================================================
    registry.registerFunction(
        "component.list_types",
        "Lists every component type registered in the engine.",
        Json::object(),
        [](const Json&) -> Json {
            Json types = Json::array();
            for (const std::string& name : listRegisteredComponentTypes()) {
                types.push_back(name);
            }
            return types;
        });

    registry.registerFunction(
        "component.add",
        "Adds a component to an entity. Args: {id, type}. Returns the component fields.",
        Json{{"id", {{"type", "integer"}}}, {"type", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle = requireEntityId(args, scene);
            const std::string  typeName = args.at("type").get<std::string>();
            auto&              ecs      = ECSRegistry::get();
            const auto         typeIndex = ecs.getTypeIndex(FName(typeName));
            if (!typeIndex) {
                throw Error("unknown component type: " + typeName);
            }
            void* ptr = ecs.getComponent(*typeIndex, scene, handle);
            if (ptr == nullptr) {
                ptr = ecs.addComponent(*typeIndex, scene, handle);
            }
            if (ptr == nullptr) {
                throw Error(std::format("failed to add component '{}'", typeName));
            }
            return serializeComponent(scene, handle, typeName);
        });

    registry.registerFunction(
        "component.remove",
        "Removes a component from an entity. Args: {id, type}. Returns {removed}.",
        Json{{"id", {{"type", "integer"}}}, {"type", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle = requireEntityId(args, scene);
            const std::string  typeName = args.at("type").get<std::string>();
            const bool removed = ECSRegistry::get().removeComponent(FName(typeName), scene, handle);
            return Json{{"removed", removed}};
        });

    registry.registerFunction(
        "component.get",
        "Returns the reflected fields of a component. Args: {id, type}.",
        Json{{"id", {{"type", "integer"}}}, {"type", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle = requireEntityId(args, scene);
            return serializeComponent(scene, handle, args.at("type").get<std::string>());
        });

    registry.registerFunction(
        "component.set",
        "Updates reflected fields of a component. Args: {id, type, fields:{name: value}}.",
        Json{{"id", {{"type", "integer"}}}, {"type", {{"type", "string"}}}, {"fields", {{"type", "object"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle = requireEntityId(args, scene);
            const std::string  typeName = args.at("type").get<std::string>();

            auto&      ecs       = ECSRegistry::get();
            const auto typeIndex = ecs.getTypeIndex(FName(typeName));
            if (!typeIndex) {
                throw Error("unknown component type: " + typeName);
            }
            void* const ptr = ecs.getComponent(*typeIndex, scene, handle);
            if (ptr == nullptr) {
                throw Error(std::format("entity does not have component '{}'", typeName));
            }

            // Merge the requested fields into the current reflected state, so
            // partial updates never clobber untouched fields.
            Json merged = ReflectionSerializer::serializeByRuntimeReflection(ptr, *typeIndex, typeName);
            for (const auto& [key, value] : args.at("fields").items()) {
                merged[key] = value;
            }
            ReflectionSerializer::deserializeByRuntimeReflection(ptr, *typeIndex, merged, typeName);
            static_cast<IComponent*>(ptr)->onPostSerialize();
            return merged;
        });
}

} // namespace ya
