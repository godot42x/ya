#pragma once
#include "Core/Base.h"
#include "Core/FName.h"
#include "Core/Log.h"

#include <entt/entt.hpp>

#include "ECS/Component.h"

namespace ya
{
struct Scene;

struct ENGINE_API Entity
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

    operator bool() const;
    operator entt::entity() const { return _entityHandle; }
    operator uint32_t() const { return static_cast<uint32_t>(_entityHandle); }

    bool operator==(const Entity& other) const { return _entityHandle == other._entityHandle && _scene == other._scene; }
    bool operator!=(const Entity& other) const { return !(*this == other); }

    const std::string& getName() const { return name; }
    void               setName(const std::string& newName);
};

} // namespace ya

#include "ECS/Entity.inl"
