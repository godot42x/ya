#pragma once

#include "Core/Debug/Instrumentor.h"
#include "Node.h"
#include "Render/Model.h"
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

    std::unordered_map<entt::entity, Entity>                _entityMap;
    std::unordered_map<entt::entity, std::shared_ptr<Node>> _nodeMap; // Entity -> Node mapping
    std::shared_ptr<Node>                                   _rootNode = nullptr;

  public:
    Scene(const std::string &name = "Untitled Scene");
    ~Scene();


    // Delete copy constructor and assignment operator
    Scene(const Scene &)            = delete;
    Scene &operator=(const Scene &) = delete;

    // Add move constructor and assignment operator
    Scene(Scene &&)            = default;
    Scene &operator=(Scene &&) = default;

    // === Public Node API (Application Layer) ===
    Node   *createNode(const std::string &name = "Entity", Node *parent = nullptr, Entity *entity = nullptr);
    Node3D *createNode3D(const std::string &name = "Entity", Node *parent = nullptr, Entity *entity = nullptr);


    /**
     * @brief Destroy a Node and its underlying Entity
     */
    void destroyNode(Node *node);

    void destroyEntity(Entity *entity);


    /**
     * @brief Get the Node associated with an Entity
     * @return Node pointer or nullptr if entity has no associated Node
     * Q: why not add Entity::getNode(self) interface?
     * A: We in the early POC stage, we don't sure yet to make ECS and the NodeTree be integrated
     */
    Node *getNodeByEntity(Entity *entity);
    Node *getNodeByEntity(entt::entity handle);

    /**
     * @brief Get root node of scene hierarchy
     */
    Node *getRootNode()
    {
        createRootNode();
        return _rootNode.get();
    }
    bool isValidEntity(const Entity *entity) const;

    // Check if Scene pointer is safe to access
    bool isValid() const
    {
        // return this && _magic == SCENE_MAGIC;
        return _magic == SCENE_MAGIC;
    }

    Entity       *getEntityByEnttID(entt::entity id);
    const Entity *getEntityByEnttID(entt::entity id) const;
    Entity       *getEntityByID(uint32_t id)
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

    void addToScene(Node *node)
    {
        if (!_rootNode) {
            createRootNode();
        }
        _rootNode->addChild(node);
    }

    stdptr<Scene>        clone();
    static stdptr<Scene> cloneScene(const Scene *scene);
    static stdptr<Scene> cloneSceneByReflection(const Scene *scene);

    Node *duplicateNode(Node *node, Node *parent = nullptr);

  private:
    // === Internal ECS API (Engine Systems Only) ===
    /**
     * @brief Create raw Entity without Node wrapper
     * @note Only for internal systems (Serialization, ResourceResolve, etc.)
     * @note Application code should use createNode() instead
     */
    Entity *createEntity(const std::string &name = "Entity");
    Entity *createEntityWithUUID(uint64_t           uuid,
                                 const std::string &name = "Entity");

    void createEntityImpl(Entity &entity);

    void createRootNode();

    void onNodeCreated(stdptr<Node> node, Node *parent);

    // Allow internal systems to access createEntity
    friend class SceneSerializer;
    friend class ResourceResolveSystem;
};

} // namespace ya