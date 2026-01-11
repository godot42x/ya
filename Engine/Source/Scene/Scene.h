#pragma once

#include "Node.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace ya
{
struct Entity;

struct [[refl]] Scene
{
    friend struct Entity;

    // Magic number for dangling pointer detection
    static constexpr uint32_t SCENE_MAGIC = 0x5343454E; // 'SCEN'
    uint32_t                  _magic      = SCENE_MAGIC;

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
    Entity *createEntity(const std::string &name = "Entity");
    Entity *createEntityWithUUID(uint64_t           uuid,
                                 const std::string &name = "Entity");
    void    destroyEntity(Entity *entity);
    bool    isValidEntity(const Entity *entity) const;

    // Check if Scene pointer is safe to access
    bool isValid() const
    {
        // return this && _magic == SCENE_MAGIC;
        return _magic == SCENE_MAGIC;
    }

    Entity *getEntityByEnttID(entt::entity id);
    Entity *getEntityByID(uint32_t id)
    {
        return getEntityByEnttID(static_cast<entt::entity>(id));
    }

    Entity *getEntityByName(const std::string &name);

    // Scene management
    void clear();
    void onUpdateRuntime(float deltaTime);
    void onUpdateEditor(float deltaTime);
    void onRenderEditor();
    void onRenderRuntime();

    // Getters
    const std::string &getName() const { return _name; }
    void               setName(const std::string &name) { _name = name; }

    // Registry access
    entt::registry       &getRegistry() { return _registry; }
    const entt::registry &getRegistry() const { return _registry; }

    // Find entities
    Entity              findEntityByName(const std::string &name);
    std::vector<Entity> findEntitiesByTag(const std::string &tag);

    void addToScene(stdptr<Node> node)
    {
        if (!_rootNode) {
            _rootNode = std::make_shared<Node>();
            _rootNode->setName("Root");
        }
        _rootNode->addChild(node.get());
    }

    stdptr<Scene>        clone() { return Scene::cloneScene(this); }
    static stdptr<Scene> cloneScene(const Scene *scene);

  private:

    void createEntityImpl(Entity &entity);
};

} // namespace ya