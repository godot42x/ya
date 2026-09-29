#pragma once
#include "Core/Base.h"
#include "Core/FName.h"
#include "Core/Log.h"
#include "Core/Reflection/Reflection.h"

#include <entt/entt.hpp>

#include "ECS/Component.h"
#include "ECS/EntitySceneContract.h"

namespace ya
{
struct Scene;

struct YA_ECS_CORE_API Entity
{
  private:
    entt::entity    _entityHandle = {entt::null};
    Scene*          _scene        = nullptr;
    entt::registry* _registry     = nullptr;

  public:
    std::string        name;
    std::vector<FName> _components;

  public:
    Entity() = default;
    Entity(entt::entity handle, Scene* scene, entt::registry* registry = nullptr)
        : _entityHandle(handle), _scene(scene), _registry(registry)
    {
    }
    Entity(const Entity& other)            = default;
    Entity& operator=(const Entity& other) = default;
    ~Entity()                              = default;

    template <typename T, typename... Args>
    T* addComponent(Args&&... args);

    template <typename T>
    void removeComponent();

    template <typename T>
    T* getComponent();

    template <typename T>
    const T* getComponent() const;

    /// Component or nullptr. `getComponent` asserts when the component is
    /// absent, so "does it have one?" must be asked through this or through
    /// `hasComponent`; `if (auto* c = entity->getComponent<T>())` is only safe
    /// when `T` is guaranteed present on the entity.
    template <typename T>
    T* tryGetComponent();

    template <typename T>
    const T* tryGetComponent() const;

    template <typename T>
    [[nodiscard]] bool hasComponent() const;

    template <typename... Components>
    [[nodiscard]] bool hasComponents() const;

    template <typename... Components>
    auto getComponents();

    [[nodiscard]] bool     isValid() const { return  this->operator bool(); }
    [[nodiscard]] uint32_t getId() const { return static_cast<uint32_t>(_entityHandle); }
    entt::entity           getHandle() const { return _entityHandle; }
    Scene*                 getScene() const { return _scene; }
    entt::registry*        getRegistry() const { return _registry; }

    operator entt::entity() const { return _entityHandle; }
    operator uint32_t() const { return static_cast<uint32_t>(_entityHandle); }

    bool operator==(const Entity& other) const { return _entityHandle == other._entityHandle && _scene == other._scene; }
    bool operator!=(const Entity& other) const { return !(*this == other); }

    const std::string& getName() const { return name; }
    /// Renames the entity and keeps its scene node (if any) in sync through
    /// the entity-scene contract implemented by ya-scene-core.
    void setName(const std::string& newName);

    // Component ops by reflected type name, for callers that only know the
    // name (editor add/remove menus, script bindings). Unknown names throw.
    /// Adds the component, or returns the existing one.
    void*              addComponentByName(const std::string& typeName);
    [[nodiscard]] bool removeComponentByName(const std::string& typeName);

    YA_REFLECT_BEGIN(Entity)
    YA_REFLECT_METHOD(getId, .tooltip("Entity id in its scene"))
    YA_REFLECT_METHOD(getName, .tooltip("Entity display name"))
    YA_REFLECT_METHOD(setName, .tooltip("Rename the entity"))
    YA_REFLECT_END()

    /// True when the handle is valid in its owning scene/registry. The scene
    /// side of the check goes through the entity-scene contract (ecs-core does
    /// not depend on Scene).
    operator bool() const;
};

} // namespace ya

#include "Entity.inl"
