#include "GameEditor/UI/Ops/EditorComponentOps.h"

#include "ECS/ECSRegistry.h"
#include "ECS/Entity.h"
#include "GameEditor/UI/Ops/EditorHierarchyOps.h"
#include "Render/Adapters/Companion/CompanionManager.h"

#include <algorithm>

namespace ya
{
namespace
{

bool equalsTypeName(std::string_view name, std::string_view expected)
{
    return name == expected;
}

} // namespace

bool isIdentityAuthoringComponent(std::string_view typeName)
{
    return equalsTypeName(typeName, "IDComponent") ||
           equalsTypeName(typeName, "TransformComponent") ||
           equalsTypeName(typeName, "ManagedChildComponent") ||
           equalsTypeName(typeName, "ScriptComponent");
}

bool canMutateAuthoringComponents(const Entity& entity)
{
    if (!entity.isValid() || !entity.getScene() || !entity.getRegistry()) {
        return false;
    }
    if (!CompanionManager::isAuthorEditable(entity)) {
        return false;
    }
    return !editorIsInstanceChild(&entity);
}

bool canAddAuthoringComponent(const Entity& entity, type_index_t type)
{
    if (!canMutateAuthoringComponents(entity) || type == 0) {
        return false;
    }
    const std::string name = authoringComponentTypeName(type);
    if (name.empty() || isIdentityAuthoringComponent(name)) {
        return false;
    }
    auto& ecs = ECSRegistry::get();
    return !ecs.hasComponent(type, *entity.getRegistry(), entity.getHandle());
}

bool canRemoveAuthoringComponent(const Entity& entity, type_index_t type)
{
    if (!canMutateAuthoringComponents(entity) || type == 0) {
        return false;
    }
    const std::string name = authoringComponentTypeName(type);
    if (name.empty() || isIdentityAuthoringComponent(name)) {
        return false;
    }
    auto& ecs = ECSRegistry::get();
    return ecs.hasComponent(type, *entity.getRegistry(), entity.getHandle());
}

bool canAddAuthoringComponent(const std::vector<Entity*>& entities, type_index_t type)
{
    for (Entity* entity : entities) {
        if (entity && canAddAuthoringComponent(*entity, type)) {
            return true;
        }
    }
    return false;
}

bool canRemoveAuthoringComponent(const std::vector<Entity*>& entities, type_index_t type)
{
    if (entities.empty()) {
        return false;
    }
    for (Entity* entity : entities) {
        if (!entity || !canRemoveAuthoringComponent(*entity, type)) {
            return false;
        }
    }
    return true;
}

std::vector<std::pair<std::string, type_index_t>> authoringComponentTypes()
{
    std::vector<std::pair<std::string, type_index_t>> types;
    for (const auto& [fname, typeIndex] : ECSRegistry::get().getTypeIndexCache()) {
        const std::string name = fname.toString();
        if (isIdentityAuthoringComponent(name)) {
            continue;
        }
        types.emplace_back(name, typeIndex);
    }
    std::sort(types.begin(), types.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    return types;
}

std::string authoringComponentTypeName(type_index_t type)
{
    for (const auto& [fname, typeIndex] : ECSRegistry::get().getTypeIndexCache()) {
        if (typeIndex == type) {
            return fname.toString();
        }
    }
    return {};
}

bool addAuthoringComponent(const std::vector<Entity*>& entities, type_index_t type)
{
    const std::string name = authoringComponentTypeName(type);
    if (name.empty()) {
        return false;
    }
    bool any = false;
    for (Entity* entity : entities) {
        if (!entity || !canAddAuthoringComponent(*entity, type)) {
            continue;
        }
        if (entity->addComponentByName(name)) {
            any = true;
        }
    }
    return any;
}

bool removeAuthoringComponent(const std::vector<Entity*>& entities, type_index_t type)
{
    const std::string name = authoringComponentTypeName(type);
    if (name.empty()) {
        return false;
    }
    bool any = false;
    for (Entity* entity : entities) {
        if (!entity || !canRemoveAuthoringComponent(*entity, type)) {
            continue;
        }
        if (entity->removeComponentByName(name)) {
            any = true;
        }
    }
    return any;
}

} // namespace ya
