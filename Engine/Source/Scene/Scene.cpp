#include "Scene.h"
#include "ECS/Component.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include <algorithm>

#include "Core/Reflection/ECSRegistry.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "reflects-core/lib.h"

#include "Core/UUID.h"


namespace ya
{
Scene::Scene(const std::string &name)
    : _name(name)
{
}

Scene::~Scene()
{
    _magic = 0xDEADBEEF; // Mark as destroyed
    clear();
}


Entity *Scene::createEntity(const std::string &name)
{
    return createEntityWithUUID(UUID{}, name);
}

Entity *Scene::createEntityWithUUID(uint64_t uuid, const std::string &name)
{
    Entity entity = {_registry.create(), this};
    entity._name  = name;

    // Add basic components with specific UUID
    auto idComponent = entity.addComponent<IDComponent>();
    idComponent->_id = UUID(uuid);

    auto tagComponent  = entity.addComponent<TagComponent>();
    tagComponent->_tag = name.empty() ? "Entity" : name;

    auto it = _entityMap.insert({entity.getHandle(), std::move(entity)});
    YA_CORE_ASSERT(it.second, "Entity ID collision!");

    return &it.first->second;
}

void Scene::destroyEntity(Entity *entity)
{
    if (isValidEntity(entity))
    {
        auto handle = entity->getHandle();
        _registry.destroy(handle);
        _entityMap.erase(handle); // 清理 _entityMap，防止悬空指针
    }
}

bool Scene::isValidEntity(const Entity *entity) const
{
    return entity &&
           _entityMap.contains(entity->getHandle()) &&
           _registry.valid(entity->getHandle());
}


Entity *Scene::getEntityByEnttID(entt::entity id)
{
    auto it = _entityMap.find(id);
    if (it != _entityMap.end())
    {
        return &it->second;
    }
    return nullptr;
}

Entity *Scene::getEntityByName(const std::string &name)
{
    for (auto &[id, entity] : _entityMap)
    {
        if (entity.hasComponent<TagComponent>())
        {
            auto tag = entity.getComponent<TagComponent>();
            if (tag->getTag() == name)
            {
                return &entity;
            }
        }
    }
    return nullptr;
}

void Scene::clear()
{
    _registry.clear();
    _entityMap.clear();
    _entityCounter = 0;
}

void Scene::onUpdateRuntime(float deltaTime)
{
    // Update systems here
    // Example: Update transform hierarchy, physics, animations, etc.

    // For now, just iterate through entities with transform components
    // auto view = _registry.view<TransformComponent>();
    // for (auto entity : view)
    // {
    // Update entity logic here
    // }
}

void Scene::onUpdateEditor(float deltaTime)
{
    // Editor-specific updates
    onUpdateRuntime(deltaTime);
}

void Scene::onRenderRuntime()
{
    // Render entities with renderable components
    // auto view = _registry.view<TransformComponent, SpriteRendererComponent>();
    // for (auto entity : view)
    // {
    //     auto [transform, sprite] = view.get<TransformComponent, SpriteRendererComponent>(entity);
    // Render sprite with transform
    // }
}

void Scene::onRenderEditor()
{
    // Editor-specific rendering
    onRenderRuntime();

    // Render editor-specific elements (gizmos, outlines, etc.)
}

Entity Scene::findEntityByName(const std::string &name)
{
    auto view = _registry.view<TagComponent>();
    for (auto entity : view)
    {
        const TagComponent &tag = view.get<TagComponent>(entity);
        if (tag._tag == name)
        {
            return Entity{entity, this};
        }
    }
    return Entity{};
}

std::vector<Entity> Scene::findEntitiesByTag(const std::string &tag)
{
    std::vector<Entity> entities;
    auto                view = _registry.view<TagComponent>();

    for (auto entity : view)
    {
        const TagComponent &tagComponent = view.get<TagComponent>(entity);
        if (tagComponent._tag == tag)
        {
            entities.emplace_back(entity, this);
        }
    }

    return entities;
}

stdptr<Scene> Scene::clone()
{
    YA_PROFILE_FUNCTION_LOG();

    return Scene::cloneScene(this);
    // TODO: 暂时不支持 ScriptComponent、Container Property的克隆
    // return Scene::cloneSceneByReflection(this);
}

template <class Component>
static void copyComponent(const entt::registry &src, entt::registry &dst, const std::unordered_map<UUID, entt::entity> &entityMap)
{
    for (auto e : src.view<Component>())
    {
        UUID uuid = src.get<IDComponent>(e)._id;
        YA_CORE_ASSERT(entityMap.contains(uuid), "UUID not found in entity map");
        auto dstEnttID = entityMap.at(uuid);

        auto &srcComponent = src.get<Component>(e);
        dst.emplace_or_replace<Component>(dstEnttID, srcComponent);
    }
}

// Helper function to copy a component from source to destination entity using reflection
static void copyComponentByReflection(
    const entt::registry &srcRegistry,
    entt::registry       &dstRegistry,
    entt::entity          srcEntity,
    entt::entity          dstEntity,
    const std::string    &componentName,
    uint32_t              componentTypeIndex)
{
    auto &classRegistry = ClassRegistry::instance();
    auto &ecsRegistry   = ya::ECSRegistry::get();

    auto *cls = classRegistry.getClass(componentName);
    if (!cls) {
        YA_CORE_WARN("Component class {} not found in class registry", componentName);
        return;
    }


    void *srcComponent = ecsRegistry.getComponent(componentName, srcRegistry, srcEntity);
    if (!srcComponent) {
        YA_CORE_WARN("Failed to get component {} for entity", componentName);
        return;
    }

    void *dstComponent = ecsRegistry.addComponent(componentName, dstRegistry, dstEntity);
    if (!dstComponent) {
        YA_CORE_WARN("Failed to create component {} for entity", componentName);
        return;
    }

    // data -> json -> data
    // TODO: iterate properties?
    try {
        // Serialize source component to JSON
        auto json =
            ya::ReflectionSerializer::serializeByRuntimeReflection(srcComponent, componentTypeIndex, cls->getName());
        ya::ReflectionSerializer::deserializeByRuntimeReflection(dstComponent, componentTypeIndex, json, cls->getName());
    }
    catch (const std::exception &e) {
        YA_CORE_ERROR("Failed to copy component {}: {}", componentName, e.what());
    }
}

stdptr<Scene> Scene::cloneScene(const Scene *scene)
{
    stdptr<Scene> newScene = makeShared<Scene>();

    std::unordered_map<UUID, entt::entity> entityMap;

    const auto &srcRegistry = scene->getRegistry();
    auto       &dstRegistry = newScene->getRegistry();

    auto idView = srcRegistry.view<IDComponent>();
    for (auto entity : idView)
    {
        auto         id        = srcRegistry.get<IDComponent>(entity)._id;
        const auto  &name      = srcRegistry.get<TagComponent>(entity)._tag;
        entt::entity newEntity = newScene->createEntityWithUUID(id, name)->getHandle();
        entityMap.insert({id, newEntity});
    }

    // LACK: need to manually list all components to copy
    using components_to_copy = refl::type_list<
        IDComponent,
        TagComponent,
        TransformComponent,
        SimpleMaterialComponent,
        UnlitMaterialComponent,
        LitMaterialComponent,
        LuaScriptComponent,
        PointLightComponent,
        MeshComponent>;

    refl::foreach_in_typelist<components_to_copy>([&](const auto &T) {
        copyComponent<std::decay_t<decltype(T)>>(srcRegistry, dstRegistry, entityMap);
    });
    // refl::foreach_types(components_to_copy{}, [&](const auto &T) {
    //     copyComponent<std::decay_t<decltype(T)>>(srcRegistry, dstRegistry, entityMap);
    // });

    return newScene;
}

stdptr<Scene> Scene::cloneSceneByReflection(const Scene *scene)
{
    stdptr<Scene> newScene = makeShared<Scene>();

    std::unordered_map<UUID, entt::entity> srcEntityMap;
    std::unordered_map<UUID, entt::entity> dstEntityMap;

    const auto &srcRegistry = scene->getRegistry();
    auto       &dstRegistry = newScene->getRegistry();


    srcRegistry.view<IDComponent, TagComponent>().each([&](auto entity, const IDComponent &id, const TagComponent &tag) {
        entt::entity newEntity = newScene->createEntityWithUUID(id._id, tag._tag)->getHandle();
        srcEntityMap.insert({id._id, entity});
        dstEntityMap.insert({id._id, newEntity});
    });

    // Step 2: Use ECSRegistry to automatically discover and copy ALL registered component types
    auto &ecsRegistry = ya::ECSRegistry::get();

    for (const auto &[fName, typeIndex] : ecsRegistry._typeIndexCache)
    {
        std::string componentName = fName.toString();

        // Skip IDComponent and TagComponent as they are already handled
        if (componentName == "IDComponent" ||
            componentName == "TagComponent") {
            continue;
        }


        for (auto &[uuid, dstEntity] : dstEntityMap)
        {
            entt::entity srcEntity = srcEntityMap[uuid];
            if (srcEntity == entt::null) {
                continue;
            }
            if (!ecsRegistry.hasComponent(fName, srcRegistry, srcEntity)) {
                continue;
            }
            copyComponentByReflection(srcRegistry, dstRegistry, srcEntity, dstEntity, componentName, typeIndex);
        }
    }

    return newScene;
}



} // namespace ya
