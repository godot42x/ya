#include "Scene.h"
#include "ECS/Component.h"
#include "ECS/Entity.h"
#include <algorithm>



namespace ya
{
Scene::Scene(const std::string &name)
    : _name(name), _entityCounter(0)
{
}

Scene::~Scene()
{
    clear();
}



Entity *Scene::createEntity(const std::string &name)
{
    auto   id = _registry.create();
    Entity entity{id, this};
    entity._name = name;

    // Add basic components
    auto *idComponent = entity.addComponent<IDComponent>();
    idComponent->_id  = _entityCounter++;

    auto *tagComponent = entity.addComponent<TagComponent>();
    tagComponent->_tag = name.empty() ? "Entity" : name;


    // auto &transformComponent = entity.addComponent<TransformComponent>();

    auto it = _entityMap.insert({id, std::move(entity)});
    YA_CORE_ASSERT(it.second, "Entity ID collision!");

    return &it.first->second;
}

Entity *Scene::createEntityWithUUID(uint64_t uuid, const std::string &name)
{
    Entity entity = {_registry.create(), this};
    entity._name  = name;

    // Add basic components with specific UUID
    auto idComponent = entity.addComponent<IDComponent>();
    idComponent->_id = static_cast<uint32_t>(uuid);

    auto tagComponent  = entity.addComponent<TagComponent>();
    tagComponent->_tag = name.empty() ? "Entity" : name;

    auto it = _entityMap.insert({entity.getHandle(), std::move(entity)});
    YA_CORE_ASSERT(it.second, "Entity ID collision!");

    return &it.first->second;
}

void Scene::destroyEntity(Entity entity)
{
    if (isValidEntity(entity))
    {
        _registry.destroy(entity.getHandle());
    }
}

bool Scene::isValidEntity(Entity entity) const
{
    return _registry.valid(entity.getHandle());
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


} // namespace ya
