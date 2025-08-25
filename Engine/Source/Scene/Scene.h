#pragma once

#include "Node.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace ya
{
class Entity;

class Scene
{
    friend class Entity;

    std::string    _name;
    entt::registry _registry;
    uint32_t       _entityCounter = 0;

    std::unordered_map<entt::entity, Entity> _entityMap;
    std::shared_ptr<Node>                    _rootNode = nullptr;

  public:
    Scene(const std::string &name = "Untitled Scene");
    ~Scene();

    // Delete copy constructor and assignment operator
    Scene(const Scene &)            = delete;
    Scene &operator=(const Scene &) = delete;

    // Add move constructor and assignment operator
    Scene(Scene &&)            = default;
    Scene &operator=(Scene &&) = default;

    // Entity management
    Entity createEntity(const std::string &name = "Entity");
    Entity createEntityWithUUID(uint64_t uuid, const std::string &name = "Entity");
    void   destroyEntity(Entity entity);
    bool   isValidEntity(Entity entity) const;

    // Scene management
    void clear();
    void onUpdateRuntime(float deltaTime);
    void onUpdateEditor(float deltaTime);
    void onRenderRuntime();
    void onRenderEditor();

    // Getters
    const std::string &getName() const { return _name; }
    void               setName(const std::string &name) { _name = name; }

    // Registry access
    entt::registry       &getRegistry() { return _registry; }
    const entt::registry &getRegistry() const { return _registry; }

    // Find entities
    Entity              findEntityByName(const std::string &name);
    std::vector<Entity> findEntitiesByTag(const std::string &tag);
};

} // namespace ya