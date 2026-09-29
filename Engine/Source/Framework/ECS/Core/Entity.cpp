#include "ECS/Entity.h"
#include "ECS/Component.h"
#include "ECS/ECSRegistry.h"

#include <stdexcept>

namespace ya
{

namespace detail
{
namespace
{
EntityRenameFn g_entityRename     = nullptr;
EntityValidFn  g_entitySceneValid = nullptr;
}

void entityRenameViaScene(Entity& entity, const std::string& newName)
{
    if (g_entityRename) {
        g_entityRename(entity, newName);
    }
}

bool entityIsSceneValid(const Entity& entity)
{
    return g_entitySceneValid ? g_entitySceneValid(entity) : true;
}

void setEntitySceneBridge(EntityRenameFn rename, EntityValidFn isValid)
{
    g_entityRename     = rename;
    g_entitySceneValid = isValid;
}

} // namespace detail

void Entity::setName(const std::string& newName)
{
    name = newName;
    if (_scene) {
        detail::entityRenameViaScene(*this, newName);
    }
}

Entity::operator bool() const
{
    if (_entityHandle == entt::null || !_registry) {
        return false;
    }
    if (_scene && !detail::entityIsSceneValid(*this)) {
        return false;
    }
    return _registry->valid(_entityHandle);
}

namespace
{
type_index_t componentTypeOf(const std::string& typeName)
{
    const auto typeIndex = ECSRegistry::get().getTypeIndex(FName(typeName));
    if (!typeIndex) {
        throw std::runtime_error("unknown component type: " + typeName);
    }
    return *typeIndex;
}
} // namespace

void* Entity::addComponentByName(const std::string& typeName)
{
    const type_index_t typeIndex = componentTypeOf(typeName);
    if (_registry == nullptr) {
        return nullptr;
    }
    auto& ecs = ECSRegistry::get();
    void* ptr = ecs.getComponent(typeIndex, *_registry, _entityHandle);
    if (ptr == nullptr) {
        ptr = ecs.addComponent(typeIndex, *_registry, _entityHandle, this);
    }
    else if (auto* component = static_cast<IComponent*>(ptr)) {
        // "Get or create this component on this entity" answers for this entity
        // either way, so a component that arrived here through some other path
        // still ends up knowing its owner.
        component->setOwner(this);
    }
    return ptr;
}

bool Entity::removeComponentByName(const std::string& typeName)
{
    const type_index_t typeIndex = componentTypeOf(typeName);
    return _registry != nullptr && ECSRegistry::get().removeComponent(typeIndex, *_registry, _entityHandle);
}

} // namespace ya
