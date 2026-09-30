#pragma once

#include "ECS/SceneBus.h"
#include "Core/Common/Types.h"
#include "Core/Log.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Reflection/Reflection.h"
#include "Core/TypeIndex.h"
#include "ECS/Component.h"
#include "ECS/ComponentMutation.h"
#include "ECS/Entity.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/SceneWidgetEntry.h"
#include "Scene/Core/ISceneLifecycleHost.h"
#include "Scene3D/Node3D.h"
#include "Resource/Model.h"
#include <entt/entt.hpp>
#include <atomic>
#include <cstdint>
#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{
struct YA_SCENE_CORE_API Scene
{
    friend struct Entity;

    /// Binds the lifecycle registration sink (implemented by SceneManager in
    /// scene-runtime; bound by the Host at startup). Null disables registration.
    static void setLifecycleHost(ISceneLifecycleHost* host);

    static ISceneLifecycleHost* getLifecycleHost();

    // Magic number for dangling pointer detection
    // static constexpr uint32_t SCENE_MAGIC = 0x5343454E; // 'SCEN'
    // uint32_t                  _magic      = SCENE_MAGIC;

    std::string    _name;
    uint64_t       _instanceId = 0;
    entt::registry _registry;
    uint32_t       _entityCounter = 0;

    std::unordered_map<entt::entity, Entity>                _entityMap;
    std::unordered_map<entt::entity, std::shared_ptr<Node>> _nodeMap; // Entity -> Node mapping
    std::shared_ptr<Node>                                   _rootNode = nullptr;

    /// Top-level Game UI authoring entries (new format; the runtime instantiates
    /// them into a WidgetTree via GameUIHost, Phase 3).
    std::vector<SceneWidgetEntry> _widgetEntries;

    /// Roots queued by `queueDestroyNode`, destroyed at the next flush.
    std::vector<entt::entity> _queuedDestroys;

  public:
    Scene(const std::string& name = "Untitled Scene");
    ~Scene();


    // Entities and the instance table hold this scene's address.
    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&)                 = delete;
    Scene& operator=(Scene&&)      = delete;

    /// The live scene with this instance id, or null once it is destroyed.
    /// Instance ids are never reused, so a stale id stays stale.
    static Scene* findByInstanceId(uint64_t instanceId);

    // === Public Node API (Application Layer) ===
    Node*   createNode(const std::string& name = "Entity", Node* parent = nullptr, Entity* entity = nullptr);
    Node3D* createNode3D(const std::string& name = "Entity", Node* parent = nullptr, Entity* entity = nullptr);

    // === Game UI authoring entries (SceneWidgetEntry) ===
    void addWidgetEntry(SceneWidgetEntry entry);
    /// Remove the entry with `entryId`; returns whether it existed.
    bool removeWidgetEntry(const std::string& entryId);
    void clearWidgetEntries();
    [[nodiscard]] const std::vector<SceneWidgetEntry>& getWidgetEntries() const { return _widgetEntries; }
    [[nodiscard]] std::vector<SceneWidgetEntry>&       getWidgetEntries() { return _widgetEntries; }


    template <typename ComponentType, typename... Args>
        requires(!std::is_base_of_v<ComponentType, IComponent>)
    ComponentType* addComponent(entt::entity entity, Args&&... args)
    {
        if (!isValid()) {
            YA_CORE_WARN("Scene is invalid");
            return nullptr;
        }
        // The Scene is the only thing that can name a wrapped entity for a given
        // handle, so it resolves the owner here instead of letting the caller
        // leave the component with no back-pointer.
        Entity* owner = getEntityByEnttID(entity);
        if (owner == nullptr) {
            YA_CORE_WARN("Scene::addComponent on an entity the Scene does not know");
            return nullptr;
        }
        return detail_component_mutation::addComponent<ComponentType>(_registry, entity, owner, std::forward<Args>(args)...);
    }

    template <typename ComponentType>
    void removeComponent(entt::entity entity)
    {
        if (!isValid()) {
            YA_CORE_WARN("Scene is invalid");
            return;
        }
        detail_component_mutation::removeComponent<ComponentType>(_registry, entity);
    }
    /**
     * @brief Destroy a Node and its underlying Entity
     */
    void destroyNode(Node* node);

    void destroyEntity(Entity* entity);

    /// Destroy `node` and its subtree at the next `flushQueuedDestroys`, not
    /// now. Gameplay code uses this while the frame is still iterating the
    /// registry; the node stays valid (and visible) until the flush.
    void queueDestroyNode(Node* node);
    /// Destroy every queued subtree. `beforeDestroy` sees each entity of a
    /// subtree while it is still intact. Destroys queued from inside it are
    /// flushed in the same call.
    void flushQueuedDestroys(const std::function<void(Entity&)>& beforeDestroy = {});
    [[nodiscard]] bool hasQueuedDestroys() const { return !_queuedDestroys.empty(); }


    /**
     * @brief Get the Node associated with an Entity
     * @return Node pointer or nullptr if entity has no associated Node
     * Q: why not add Entity::getNode(self) interface?
     * A: We in the early POC stage, we don't sure yet to make ECS and the NodeTree be integrated
     */
    Node* getNodeByEntity(Entity* entity);
    Node* getNodeByEntity(entt::entity handle);

    /**
     * @brief Get root node of scene hierarchy
     */
    Node* getRootNode()
    {
        createRootNode();
        return _rootNode.get();
    }
    bool isValidEntity(const Entity* entity) const;

    bool isValid() const;

    Entity*       getEntityByEnttID(entt::entity id);
    const Entity* getEntityByEnttID(entt::entity id) const;
    Entity*       getEntityByID(uint32_t id)
    {
        return getEntityByEnttID(static_cast<entt::entity>(id));
    }

    Entity* getEntityByName(const std::string& name);
    Entity* getEntityByUUID(uint64_t uuid);

    // Scene management
    void clear();
    void onUpdateRuntime(float deltaTime);
    void onRenderRuntime();

    // Getters
    const std::string& getName() const { return _name; }
    void               setName(const std::string& name) { _name = name; }
    [[nodiscard]] uint64_t getInstanceId() const { return _instanceId; }
    uint32_t           entityCount() const { return static_cast<uint32_t>(_entityMap.size()); }

    // Registry access
    entt::registry&       getRegistry() { return _registry; }
    const entt::registry& getRegistry() const { return _registry; }

    /// The unified edit funnel: announce an in-place edit of the component
    /// `typeIndex` on `entity`. Every write path that mutates an existing
    /// component outside a typed setter routes through here (inspector change
    /// hook, undo restore, script field writes), so derived-work processors
    /// listen to exactly one signal source. No-op for unknown component types.
    void notifyComponentEdited(entt::entity entity, ya::type_index_t typeIndex);

    // Find entities
    Entity              findEntityByName(const std::string& name);
    std::vector<Entity> findEntitiesByTag(const std::string& tag);

    void addToScene(Node* node)
    {
        if (!_rootNode) {
            createRootNode();
        }
        _rootNode->addChild(node);
    }

    stdptr<Scene>        clone();
    static stdptr<Scene> cloneSceneByReflection(const Scene* scene);

    Node* duplicateNode(Node* node, Node* parent = nullptr);
    bool  moveNode(Node* node, Node* newParent, size_t childIndex);

    /// Resolve a slash-separated node path ("/Canvas/Panel") from the scene
    /// root. Empty path or "/" resolves to the root node itself.
    Node* findNodeByPath(const std::string& path);
    /// Build the "/"-separated path of a node under this scene (root children
    /// are "/Name", nested nodes "/A/B/C"). Empty when the node is not in the
    /// tree.
    std::string getNodePath(const Node* node) const;

    // === Script-facing reflected API ===
    YA_REFLECT_BEGIN(Scene)
    YA_REFLECT_METHOD(getName, .tooltip("Scene display name"))
    YA_REFLECT_METHOD(setName, .tooltip("Rename the scene"))
    YA_REFLECT_METHOD(entityCount, .tooltip("Number of entities in the scene"))
    YA_REFLECT_END()

  private:
    // === Internal ECS API (Engine Systems Only) ===
    /**
     * @brief Create raw Entity without Node wrapper
     * @note Only for internal systems (Serialization, ModelInstantiation, ResourceResolve, etc.)
     * @note Application code should use createNode() instead
     */
    Entity* createEntity(const std::string& name = "Entity");
    Entity* createEntityWithUUID(uint64_t           uuid,
                                 const std::string& name = "Entity");

    void createEntityImpl(Entity& entity);

    void createRootNode();

    void onNodeCreated(stdptr<Node> node, Node* parent);

    // Allow internal systems to access createEntity
    friend struct SceneSerializer;
    friend struct ModelInstantiationSystem;
    friend struct GameplayResourceBinding;
};

} // namespace ya
