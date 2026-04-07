#include "Scene.h"
#include "ECS/Component.h"
#include "ECS/Component/ManagedChildComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"

#include "Core/Reflection/ECSRegistry.h"

#include "Runtime/App/App.h"
#include "Scene/SceneManager.h"

#include "Core/UUID.h"


namespace ya
{

Scene::Scene(const std::string &name)
    : _name(name)
{
    if (auto *app = App::get()) {
        if (auto *sceneManager = app->getSceneManager()) {
            sceneManager->registerScenePointer(this);
        }
    }
}

Scene::~Scene()
{
    // _magic = 0xDEADBEEF; // Mark as destroyed

    if (auto *app = App::get()) {
        if (auto *sceneManager = app->getSceneManager()) {
            sceneManager->unregisterScenePointer(this);
        }
    }

    clear();
}



Entity *Scene::createEntity(const std::string &name)
{
    return createEntityWithUUID(UUID{}, name);
}

Entity *Scene::createEntityWithUUID(uint64_t uuid, const std::string &name)
{
    Entity entity = {_registry.create(), this};

    // Add basic components with specific UUID
    auto idComponent = entity.addComponent<IDComponent>();
    idComponent->_id = UUID(uuid);

    // Set entity name directly
    entity.name = name.empty() ? "Entity" : name;

    auto it = _entityMap.insert({entity.getHandle(), std::move(entity)});
    YA_CORE_ASSERT(it.second, "Entity ID collision!");

    return &it.first->second;
}

void Scene::createRootNode()
{
    if (_rootNode) {
        return;
    }

    Entity *entity = createEntity("scene_root");
    entity->addComponent<TransformComponent>();
    auto node = makeShared<Node3D>(entity, "scene_root");
    _rootNode = node;

    // ★ 将 rootNode 添加到 _nodeMap 中（这样 createNode 检查时才能发现）
    _nodeMap[entity->getHandle()] = node;
}

void Scene::onNodeCreated(stdptr<Node> node, Node *parent)
{
    if (!node) {
        return;
    }
    _nodeMap[node->getEntity()->getHandle()] = node;

    if (parent) {
        parent->addChild(node.get());
    }
    else {
        addToScene(node.get());
    }
}

void Scene::destroyEntity(Entity *entity)
{
    if (isValidEntity(entity))
    {
        auto handle = entity->getHandle();

        // Clean up associated Node if exists
        auto nodeIt = _nodeMap.find(handle);
        if (nodeIt != _nodeMap.end()) {
            auto *node = nodeIt->second.get();
            // Remove from parent
            node->removeFromParent();
            // Clear children (they become orphans)
            node->clearChildren();
            _nodeMap.erase(nodeIt);
        }

        _registry.destroy(handle);
        _entityMap.erase(handle);
    }
}

Node *Scene::createNode(const std::string &name, Node *parent, Entity *entity)
{
    // ★ 如果 Entity 已经有关联的 Node，直接返回（避免重复创建）
    if (entity) {
        auto it = _nodeMap.find(entity->getHandle());
        YA_CORE_ASSERT(it == _nodeMap.end(), "Entity '{}' already has an associated Node", entity->name);

        if (it != _nodeMap.end()) {
            YA_CORE_WARN("Entity '{}' already has an associated Node, returning existing one", entity->name);
            return it->second.get();
        }
    }

    if (!entity) {
        entity = createEntity(name);
    }
    YA_CORE_ASSERT(entity, "entity is none");
    if (entity->hasComponent<TransformComponent>()) {
        return createNode3D(name, parent, entity);
    }

    auto node = makeShared<Node>(name, entity);
    onNodeCreated(node, parent);

    return node.get();
}

Node3D *Scene::createNode3D(const std::string &name, Node *parent, Entity *entity)
{
    // ★ 如果 Entity 已经有关联的 Node，直接返回（避免重复创建）
    if (entity) {
        auto it = _nodeMap.find(entity->getHandle());
        if (it != _nodeMap.end()) {
            YA_CORE_WARN("Entity '{}' already has an associated Node3D, returning existing one", entity->name);
            return static_cast<Node3D *>(it->second.get());
        }
    }

    // Create the Entity
    if (!entity) {
        entity = createEntity(name);
    }
    YA_CORE_ASSERT(entity, "entity is none");
    if (!entity->hasComponent<TransformComponent>()) {
        entity->addComponent<TransformComponent>();
    }

    // Create and associate Node
    auto node = makeShared<Node3D>(entity, name);
    onNodeCreated(node, parent);

    return static_cast<Node3D *>(_nodeMap[entity->getHandle()].get());
}


void Scene::destroyNode(Node *node)
{
    if (!node) {
        return;
    }

    // Cast to Node3D to access Entity
    auto entity = node->getEntity();
    if (entity) {
        destroyEntity(entity);
    }
}



Node *Scene::getNodeByEntity(Entity *entity)
{
    if (!entity) {
        return nullptr;
    }
    return getNodeByEntity(entity->getHandle());
}

Node *Scene::getNodeByEntity(entt::entity handle)
{
    auto it = _nodeMap.find(handle);
    if (it != _nodeMap.end()) {
        return it->second.get();
    }
    return nullptr;
}



bool Scene::isValidEntity(const Entity *entity) const
{
    return entity &&
           _entityMap.contains(entity->getHandle()) &&
           _registry.valid(entity->getHandle());
}

// Check if Scene pointer is safe to access
bool Scene::isValid() const
{
    // if (_magic != SCENE_MAGIC) {
    //     return false;
    // }

    auto *app = App::get();
    if (!app) {
        YA_CORE_ASSERT(false, "App instance not found");
        return false;
    }

    auto *sceneManager = app->getSceneManager();
    if (!sceneManager) {
        YA_CORE_ASSERT(false, "SceneManager instance not found");
        return false;
    }

    return sceneManager->isSceneValid(this);
}


Entity *Scene::getEntityByEnttID(entt::entity id)
{
    auto it = _entityMap.find(id);
    if (it != _entityMap.end())
    {
        return &it->second;
    }
    return nullptr;
}

const Entity *Scene::getEntityByEnttID(entt::entity id) const
{
    auto it = _entityMap.find(id);
    if (it != _entityMap.end())
    {
        return &it->second;
    }
    return nullptr;
}

Entity *Scene::getEntityByName(const std::string &name)
{
    for (auto &[id, entity] : _entityMap)
    {
        if (entity.name == name)
        {
            return &entity;
        }
    }
    return nullptr;
}

Entity *Scene::getEntityByUUID(uint64_t uuid)
{
    for (auto &[id, entity] : _entityMap)
    {
        if (auto *idComponent = entity.getComponent<IDComponent>()) {
            if (idComponent->_id.value == uuid) {
                return &entity;
            }
        }
    }
    return nullptr;
}

void Scene::clear()
{
    _entityMap.clear();
    _nodeMap.clear();
    _rootNode.reset();
    _registry.clear();
    _entityCounter = 0;
}

void Scene::onUpdateRuntime(float deltaTime)
{
    // Update systems here
    // Example: Update transform hierarchy, physics, animations, etc.

    // For now, just iterate through entities with transform components
    // auto view = _registry.view<TransformComponent>();
    // for (auto entity : view)
    // {
    // Update entity logic here
    // }
}

void Scene::onUpdateEditor(float deltaTime)
{
    // Editor-specific updates
    onUpdateRuntime(deltaTime);
}

void Scene::onRenderRuntime()
{
    // Render entities with renderable components
    // auto view = _registry.view<TransformComponent, SpriteRendererComponent>();
    // for (auto entity : view)
    // {
    //     auto [transform, sprite] = view.get<TransformComponent, SpriteRendererComponent>(entity);
    // Render sprite with transform
    // }
}

void Scene::onRenderEditor()
{
    // Editor-specific rendering
    onRenderRuntime();

    // Render editor-specific elements (gizmos, outlines, etc.)
}

Entity Scene::findEntityByName(const std::string &name)
{
    for (auto &[id, entity] : _entityMap)
    {
        if (entity.name == name)
        {
            return entity;
        }
    }
    return Entity{};
}

std::vector<Entity> Scene::findEntitiesByTag(const std::string &tag)
{
    std::vector<Entity> entities;
    for (auto &[id, entity] : _entityMap)
    {
        if (entity.name == tag)
        {
            entities.push_back(entity);
        }
    }
    return entities;
}

stdptr<Scene> Scene::clone()
{
    YA_PROFILE_FUNCTION_LOG();
    return Scene::cloneSceneByReflection(this);
}

static bool shouldSkipClonedNode(const Node* node, const entt::registry& registry)
{
    if (!node) {
        return true;
    }

    const Entity* entity = node->getEntity();
    return entity && registry.all_of<ManagedChildComponent>(entity->getHandle());
}

static Node *cloneReferencedNodeTree(Node *srcNode, Scene *dstScene, Node *dstParent,
                                     const entt::registry &srcRegistry,
                                     const std::unordered_map<UUID, entt::entity> &dstEntityMap)
{
    if (!srcNode || !dstScene) {
        return nullptr;
    }

    if (shouldSkipClonedNode(srcNode, srcRegistry)) {
        return nullptr;
    }

    Entity *dstEntity = nullptr;
    if (const Entity *srcEntity = srcNode->getEntity()) {
        const auto *idComponent = srcEntity->getComponent<IDComponent>();
        if (!idComponent) {
            YA_CORE_WARN("Node '{}' has no IDComponent during clone", srcNode->getName());
            return nullptr;
        }

        const auto dstEntityIt = dstEntityMap.find(idComponent->_id);
        if (dstEntityIt == dstEntityMap.end()) {
            YA_CORE_WARN("Node '{}' references unmapped entity UUID {} during clone", srcNode->getName(), idComponent->_id.value);
            return nullptr;
        }

        dstEntity = dstScene->getEntityByEnttID(dstEntityIt->second);
        if (!dstEntity) {
            YA_CORE_WARN("Node '{}' failed to resolve destination entity during clone", srcNode->getName());
            return nullptr;
        }
    }

    Node *dstNode = dstScene->createNode(srcNode->getName(), dstParent, dstEntity);
    if (!dstNode) {
        YA_CORE_ERROR("Failed to clone node '{}'", srcNode->getName());
        return nullptr;
    }

    for (Node *child : srcNode->getChildren()) {
        cloneReferencedNodeTree(child, dstScene, dstNode, srcRegistry, dstEntityMap);
    }

    return dstNode;
}

stdptr<Scene> Scene::cloneSceneByReflection(const Scene *scene)
{
    stdptr<Scene> newScene = makeShared<Scene>(scene ? scene->getName() : "Untitled Scene");

    std::unordered_map<UUID, entt::entity> srcEntityMap;
    std::unordered_map<UUID, entt::entity> dstEntityMap;

    const auto &srcRegistry = scene->getRegistry();
    auto       &dstRegistry = newScene->getRegistry();

    const entt::entity srcRootHandle =
        (scene->_rootNode && scene->_rootNode->getEntity()) ? scene->_rootNode->getEntity()->getHandle() : entt::null;


    srcRegistry.view<IDComponent>(entt::exclude<ManagedChildComponent>).each([&](auto entity, const IDComponent& id) {
        if (entity == srcRootHandle) {
            return;
        }

        const Entity*     srcEntity = scene->getEntityByEnttID(entity);
        const std::string name      = srcEntity ? srcEntity->name : "Entity";
        entt::entity      newEntity = newScene->createEntityWithUUID(id._id, name)->getHandle();
        srcEntityMap.insert({id._id, entity});
        dstEntityMap.insert({id._id, newEntity});
    });

    // Step 2: Clone all components (default: Reflection for safety)
    auto &ecsRegistry = ya::ECSRegistry::get();

    for (const auto &[fName, typeIndex] : ecsRegistry.getTypeIndexCache())
    {
        if (fName.toString() == "IDComponent") {
            continue;
        }

        for (auto &[uuid, dstEntity] : dstEntityMap)
        {
            entt::entity srcEntity = srcEntityMap[uuid];
            if (srcEntity == entt::null) {
                continue;
            }

            void* dst = ecsRegistry.cloneComponent(typeIndex, srcRegistry, srcEntity, dstRegistry, dstEntity);
            if (dst) {
                if (auto *component = static_cast<IComponent *>(dst)) {
                    if (auto *entity = newScene->getEntityByEnttID(dstEntity)) {
                        component->setOwner(entity);
                    }
                    component->onPostSerialize();
                }
            }
        }
    }

    if (scene->_rootNode && scene->_rootNode->hasChildren()) {
        Node* dstRootNode = newScene->getRootNode();
        for (Node* child : scene->_rootNode->getChildren()) {
            if (shouldSkipClonedNode(child, srcRegistry)) {
                continue;
            }
            cloneReferencedNodeTree(
                child, newScene.get(), dstRootNode, srcRegistry, dstEntityMap);
        }
    }

    return newScene;
}

Node *Scene::duplicateNode(Node *node, Node *parent)
{
    if (!node) {
        return nullptr;
    }
    Node   *newNode   = nullptr;
    Entity *newEntity = nullptr;

    if (Entity *entity = node->getEntity())
    {
        newEntity = createEntity(entity->name + " Duplicate");

        auto  srcEntt     = entity->getHandle();
        auto  newEntt     = newEntity->getHandle();
        auto &registry    = getRegistry();
        auto &ecsRegistry = ya::ECSRegistry::get();

        // Clone all components (default: Reflection for safety)
        for (const auto &[fName, typeIndex] : ecsRegistry.getTypeIndexCache())
        {
            if (fName.toString() == "IDComponent") {
                continue;
            }
            void* dst = ecsRegistry.cloneComponent(typeIndex, registry, srcEntt, registry, newEntt);
            if (dst) {
                if (auto *component = static_cast<IComponent *>(dst)) {
                    component->setOwner(newEntity);
                    component->onPostSerialize();
                }
            }
        }
    }

    newNode = createNode(node->getName() + " Duplicate", parent, newEntity);

    return newNode;
}



} // namespace ya
