#include "Scene.h"
#include "ECS/Component.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include <algorithm>

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

    using components_to_copy = refl::type_list<
        IDComponent,
        TagComponent,
        TransformComponent,
        SimpleMaterialComponent,
        UnlitMaterialComponent,
        LitMaterialComponent,
        LuaScriptComponent,
        PointLightComponent>;

    refl::foreach_in_typelist<components_to_copy>([&](const auto &T) {
        copyComponent<std::decay_t<decltype(T)>>(srcRegistry, dstRegistry, entityMap);
    });
    // refl::foreach_types(components_to_copy{}, [&](const auto &T) {
    //     copyComponent<std::decay_t<decltype(T)>>(srcRegistry, dstRegistry, entityMap);
    // });

    return newScene;
}



} // namespace ya
