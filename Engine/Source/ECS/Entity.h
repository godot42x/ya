#pragma once
#include "Core/Base.h"
#include "Core/FName.h"
#include "Core/Log.h"

#include <entt/entt.hpp>

#include "ECS/Component.h"
#include "Scene/Scene.h"


namespace ya
{
struct Scene;

struct Entity
{
  private:
    entt::entity _entityHandle = {entt::null};
    Scene       *_scene        = nullptr;

  public:
    std::string        name;
    std::vector<FName> _components;

  public:

    Entity() = default;
    Entity(entt::entity handle, Scene *scene)
        : _entityHandle(handle), _scene(scene)
    {
    }
    Entity(const Entity &other)            = default;
    Entity &operator=(const Entity &other) = default;
    ~Entity()                              = default;

    template <typename T, typename... Args>
        requires(!std::is_base_of_v<T, IComponent>)
    T *addComponent(Args &&...args)
    {
        YA_CORE_ASSERT(!hasComponent<T>(), "Entity already has component!");
        T &component = _scene->_registry.emplace<T>(_entityHandle, std::forward<Args>(args)...);
        static_cast<IComponent &>(component).setOwner(this);

        return &component;
    }

    // template <typename T, typename... Args>
    // T &addOrReplaceComponent(Args &&...args) { return _scene->_registry.emplace_or_replace<T>(_entityHandle, std::forward<Args>(args)...); }

    template <typename T>
    T *getComponent()
    {
        assert(hasComponent<T>() && "Entity does not have component!");
        return &_scene->_registry.get<T>(_entityHandle);
    }
    template <typename T>
    const T *getComponent() const
    {
        assert(hasComponent<T>() && "Entity does not have component!");
        return &_scene->_registry.get<T>(_entityHandle);
    }
    template <typename T>
    [[nodiscard]] bool hasComponent() const
    {
        YA_CORE_ASSERT(_entityHandle != entt::null, "Entity handle is null!");
        return _scene->_registry.all_of<T>(_entityHandle);
    }

    template <typename T>
    void removeComponent()
    {
        assert(hasComponent<T>() && "Entity does not have component!");
        _scene->_registry.remove<T>(_entityHandle);
    }

    template <typename... Components>
    [[nodiscard]] bool hasComponents() const { return _scene->_registry.all_of<Components...>(_entityHandle); }

    template <typename... Components>
    auto getComponents() { return _scene->_registry.get<Components...>(_entityHandle); }

    // Utility functions
    [[nodiscard]] bool     isValid() const { return this && this->operator bool(); }
    [[nodiscard]] uint32_t getId() const { return static_cast<uint32_t>(_entityHandle); }
    entt::entity           getHandle() const { return _entityHandle; }
    Scene                 *getScene() const { return _scene; }

    operator bool() const;
    operator entt::entity() const { return _entityHandle; }
    operator uint32_t() const { return static_cast<uint32_t>(_entityHandle); }

    bool operator==(const Entity &other) const { return _entityHandle == other._entityHandle && _scene == other._scene; }
    bool operator!=(const Entity &other) const { return !(*this == other); }

    const std::string &getName() const { return name; }
    void setName(const std::string &newName) { name = newName; }
};

} // namespace ya