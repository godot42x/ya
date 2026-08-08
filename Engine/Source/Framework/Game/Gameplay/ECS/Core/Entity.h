#pragma once
#include "Core/Base.h"
#include "Core/FName.h"
#include "Core/Log.h"
#include "Core/Reflection/InstanceRef.h"
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

    template <typename T>
    [[nodiscard]] bool hasComponent() const;

    template <typename... Components>
    [[nodiscard]] bool hasComponents() const;

    template <typename... Components>
    auto getComponents();

    [[nodiscard]] bool     isValid() const { return this && this->operator bool(); }
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

    // === Script-facing reflected API ===
    // Non-template wrappers so the reflection system can export them to
    // scripts (template methods cannot be reflected). Component access
    // returns an InstanceRef; the script bridge materializes it into a
    // wrapped object with the concrete component prototype.
    [[nodiscard]] bool               hasComponentByName(const std::string& typeName) const;
    InstanceRef                      componentByName(const std::string& typeName);
    InstanceRef                      addComponentByName(const std::string& typeName);
    [[nodiscard]] bool               removeComponentByName(const std::string& typeName);
    [[nodiscard]] nlohmann::json     components() const;

    YA_REFLECT_BEGIN(Entity)
    YA_REFLECT_METHOD(getId, .tooltip("Entity id in the active scene"))
    YA_REFLECT_METHOD(getName, .tooltip("Entity display name"))
    YA_REFLECT_METHOD(setName, .tooltip("Rename the entity"))
    YA_REFLECT_METHOD(hasComponentByName, .tooltip("True when the entity has the component type"))
    YA_REFLECT_METHOD(componentByName, .tooltip("Returns the component instance or null"))
    YA_REFLECT_METHOD(addComponentByName, .tooltip("Adds (or returns) the component instance"))
    YA_REFLECT_METHOD(removeComponentByName, .tooltip("Removes the component; true when it existed"))
    YA_REFLECT_METHOD(components, .tooltip("Map of component type name -> instance"))
    YA_REFLECT_END()

    /// True when the handle is valid in its owning scene/registry. The scene
    /// side of the check goes through the entity-scene contract (ecs-core does
    /// not depend on Scene).
    operator bool() const;
};

} // namespace ya

#include "Entity.inl"
