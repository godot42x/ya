#include "Scene.h"
#include "Framework/Game/Gameplay/ECS/Component.h"
#include "Framework/Game/Gameplay/ECS/Component/ManagedChildComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/TransformComponent.h"
#include "Framework/Game/Gameplay/ECS/Entity.h"

#include "Framework/Game/Gameplay/ECS/ECSRegistry.h"
#include "Foundation/Core/Profiling/Profiling.h"

#include "Product/Host/App.h"
#include "Framework/Game/Render/Render3D/SceneManager.h"

#include "Foundation/Core/UUID.h"


namespace ya
{

Scene::Scene(const std::string &name)
    : _name(name)
{
    if (auto *app = App::get()) {
        if (auto *sceneManager = app->getSceneServices().getSceneManager()) {
            sceneManager->registerScenePointer(this);
        }
    }
}

Scene::~Scene()
{
    // _magic = 0xDEADBEEF; // Mark as destroyed

    if (auto *app = App::get()) {
        if (auto *sceneManager = app->getSceneServices().getSceneManager()) {
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
    YA_PROFILE_FUNCTION();

    Entity entity = {_registry.create(), this, &_registry};

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
    YA_PROFILE_FUNCTION();

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
    YA_PROFILE_FUNCTION();

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
    YA_PROFILE_FUNCTION();

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
    YA_PROFILE_FUNCTION();

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

Node2D *Scene::createUINode(const std::string &typeName, const std::string &name, Node *parent)
{
    auto node = createNode2DByTypeName(typeName, name);
    if (!node) {
        YA_CORE_WARN("Scene: unknown UI node type '{}'", typeName);
        return nullptr;
    }
    _entityLessNodes.push_back(node);
    if (parent) {
        parent->addChild(node.get());
    }
    else {
        addToScene(node.get());
    }
    return node.get();
}

void Scene::destroyNode(Node *node)
{
    if (!node) {
        return;
    }

    if (!node->getEntity()) {
        // Entity-less Node2D: detach and drop ownership. Children stay in the
        // tree as individually-owned nodes (same semantics as entity deletion).
        node->removeFromParent();
        std::erase_if(_entityLessNodes,
                      [node](const std::shared_ptr<Node2D> &owner) { return owner.get() == node; });
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
    auto *app = App::get();
    if (!app) {
        return true;
    }

    auto *sceneManager = app->getSceneServices().getSceneManager();
    if (!sceneManager) {
        return true;
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
    YA_PROFILE_FUNCTION();

    _entityMap.clear();
    _nodeMap.clear();
    _entityLessNodes.clear();
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

    if (const auto *src2D = dynamic_cast<const Node2D *>(srcNode)) {
        Node2D *dst2D = dstScene->createUINode(src2D->getUITypeName(), srcNode->getName(), dstParent);
        if (!dst2D) {
            YA_CORE_WARN("Node '{}' failed to clone UI node of type '{}'", srcNode->getName(), src2D->getUITypeName());
            return nullptr;
        }
        dst2D->deserializeFields(src2D->serializeFields());
        for (Node *child : srcNode->getChildren()) {
            cloneReferencedNodeTree(child, dstScene, dst2D, srcRegistry, dstEntityMap);
        }
        return dst2D;
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

    // Entity-less Node2D: recreate by type and deep-copy the reflected fields.
    if (!node->getEntity()) {
        if (auto* node2D = dynamic_cast<Node2D*>(node)) {
            if (Node2D* copy = createUINode(node2D->getUITypeName(),
                                            node2D->getName() + " Duplicate",
                                            parent)) {
                copy->deserializeFields(node2D->serializeFields());
                return copy;
            }
        }
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

Node* Scene::findNodeByPath(const std::string& path)
{
    Node* rootNode = getRootNode();
    if (!rootNode) {
        return nullptr;
    }
    if (path.empty() || path == "/") {
        return rootNode;
    }

    Node* current = rootNode;
    size_t start  = 0;
    while (start < path.size()) {
        if (path[start] == '/') {
            ++start;
            continue;
        }
        const size_t end = path.find('/', start);
        const std::string segment = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        Node* next = nullptr;
        for (Node* child : current->getChildren()) {
            if (child->getName() == segment) {
                next = child;
                break;
            }
        }
        if (!next) {
            return nullptr;
        }
        current = next;
        start   = end == std::string::npos ? path.size() : end;
    }
    return current;
}

std::string Scene::getNodePath(const Node* node) const
{
    if (!node || node == _rootNode.get()) {
        return {};
    }

    std::vector<std::string> segments;
    for (const Node* current = node; current && current != _rootNode.get(); current = current->getParent()) {
        segments.push_back(current->getName());
    }

    std::string path;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        path += "/";
        path += *it;
    }
    return path;
}

bool Scene::moveNode(Node* node, Node* newParent, size_t childIndex)
{
    if (!node) {
        return false;
    }

    Node* rootNode = getRootNode();
    if (!rootNode || node == rootNode) {
        return false;
    }

    if (!newParent) {
        newParent = rootNode;
    }

    if (node == newParent || node->isAncestorOf(newParent)) {
        return false;
    }

    node->setParent(newParent, childIndex);
    return true;
}



} // namespace ya
