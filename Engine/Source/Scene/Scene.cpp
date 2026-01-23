#include "Scene.h"
#include "ECS/Component.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include <algorithm>

#include "Core/Reflection/ECSRegistry.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "reflects-core/lib.h"

#include "Core/UUID.h"


namespace ya
{
Scene::Scene(const std::string &name)
    : _name(name)
{
}

Scene::~Scene()
{
    _magic = 0xDEADBEEF; // Mark as destroyed
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

void Scene::clear()
{
    _registry.clear();
    _entityMap.clear();
    _nodeMap.clear();
    _rootNode.reset();
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

    return Scene::cloneScene(this);
    // TODO: 暂时不支持 ScriptComponent、Container Property的克隆
    // return Scene::cloneSceneByReflection(this);
}

template <class Component>
static void copyComponent(const entt::registry &src, entt::registry &dst, const std::unordered_map<UUID, entt::entity> &entityMap)
{
    for (auto e : src.view<Component>())
    {
        UUID uuid = src.get<IDComponent>(e)._id;
        YA_CORE_ASSERT(entityMap.contains(uuid), "UUID not found in entity map");
        auto dstEnttID = entityMap.at(uuid);

        auto &srcComponent = src.get<Component>(e);
        dst.emplace_or_replace<Component>(dstEnttID, srcComponent);
    }
}

// Helper function to copy a component from source to destination entity using reflection
static void copyComponentByReflection(
    const entt::registry &srcRegistry,
    entt::registry       &dstRegistry,
    entt::entity          srcEntity,
    entt::entity          dstEntity,
    const std::string    &componentName,
    uint32_t              componentTypeIndex)
{
    auto &classRegistry = ClassRegistry::instance();
    auto &ecsRegistry   = ya::ECSRegistry::get();

    auto *cls = classRegistry.getClass(componentName);
    if (!cls) {
        YA_CORE_WARN("Component class {} not found in class registry", componentName);
        return;
    }


    void *srcComponent = ecsRegistry.getComponent(componentName, srcRegistry, srcEntity);
    if (!srcComponent) {
        YA_CORE_WARN("Failed to get component {} for entity", componentName);
        return;
    }

    void *dstComponent = ecsRegistry.addComponent(componentName, dstRegistry, dstEntity);
    if (!dstComponent) {
        YA_CORE_WARN("Failed to create component {} for entity", componentName);
        return;
    }

    // data -> json -> data
    // TODO: iterate properties?
    try {
        // Serialize source component to JSON
        auto json =
            ya::ReflectionSerializer::serializeByRuntimeReflection(srcComponent, componentTypeIndex, cls->getName());
        ya::ReflectionSerializer::deserializeByRuntimeReflection(dstComponent, componentTypeIndex, json, cls->getName());
    }
    catch (const std::exception &e) {
        YA_CORE_ERROR("Failed to copy component {}: {}", componentName, e.what());
    }
}

stdptr<Scene> Scene::cloneScene(const Scene *scene)
{
    stdptr<Scene> newScene = makeShared<Scene>();

    std::unordered_map<UUID, entt::entity> entityMap;
    std::unordered_map<UUID, Node *>       nodeMap; // ★ 新增：UUID -> Node 映射

    const auto &srcRegistry = scene->getRegistry();
    auto       &dstRegistry = newScene->getRegistry();

    auto idView = srcRegistry.view<IDComponent>();
    for (auto entity : idView)
    {
        auto              id        = srcRegistry.get<IDComponent>(entity)._id;
        const Entity     *srcEntity = scene->getEntityByEnttID(entity);
        const std::string name      = srcEntity ? srcEntity->name : "Entity";

        // ★ 改用 createNode 创建（而非 createEntityWithUUID）
        Node *newNode = newScene->createNode3D(name);
        if (!newNode) {
            YA_CORE_ERROR("Failed to create node during clone for entity '{}'", name);
            continue;
        }

        // ★ 设置 Node 的名字
        newNode->setName(name);

        // Cast to Node3D to access Entity
        auto   *newNode3D = dynamic_cast<Node3D *>(newNode);
        Entity *newEntity = newNode3D ? newNode3D->getEntity() : nullptr;
        if (!newEntity) {
            YA_CORE_ERROR("Failed to get entity from node during clone");
            continue;
        }

        // 设置正确的 UUID
        if (auto idComp = newEntity->getComponent<IDComponent>()) {
            idComp->_id = id;
        }

        entityMap.insert({id, newEntity->getHandle()});
        nodeMap.insert({id, newNode}); // ★ 记录 Node 映射
    }

    // LACK: need to manually list all components to copy
    using components_to_copy = refl::type_list<
        IDComponent,
        TransformComponent,
        SimpleMaterialComponent,
        UnlitMaterialComponent,
        LitMaterialComponent,
        LuaScriptComponent,
        PointLightComponent,
        MeshComponent,
        ModelComponent>;

    refl::foreach_in_typelist<components_to_copy>([&](const auto &T) {
        copyComponent<std::decay_t<decltype(T)>>(srcRegistry, dstRegistry, entityMap);
    });

    // ★ TODO: 克隆 Node 层级关系
    // 当前所有 Node 都是 root 的子节点
    // 未来需要遍历源场景的 Node 树，重建父子关系

    return newScene;
}

stdptr<Scene> Scene::cloneSceneByReflection(const Scene *scene)
{
    stdptr<Scene> newScene = makeShared<Scene>();

    std::unordered_map<UUID, entt::entity> srcEntityMap;
    std::unordered_map<UUID, entt::entity> dstEntityMap;

    const auto &srcRegistry = scene->getRegistry();
    auto       &dstRegistry = newScene->getRegistry();


    srcRegistry.view<IDComponent>().each([&](auto entity, const IDComponent &id) {
        const Entity     *srcEntity = scene->getEntityByEnttID(entity);
        const std::string name      = srcEntity ? srcEntity->name : "Entity";
        entt::entity      newEntity = newScene->createEntityWithUUID(id._id, name)->getHandle();
        srcEntityMap.insert({id._id, entity});
        dstEntityMap.insert({id._id, newEntity});
    });

    // Step 2: Use ECSRegistry to automatically discover and copy ALL registered component types
    auto &ecsRegistry = ya::ECSRegistry::get();

    for (const auto &[fName, typeIndex] : ecsRegistry._typeIndexCache)
    {
        std::string componentName = fName.toString();

        // Skip IDComponent as it is already handled
        if (componentName == "IDComponent") {
            continue;
        }


        for (auto &[uuid, dstEntity] : dstEntityMap)
        {
            entt::entity srcEntity = srcEntityMap[uuid];
            if (srcEntity == entt::null) {
                continue;
            }
            if (!ecsRegistry.hasComponent(fName, srcRegistry, srcEntity)) {
                continue;
            }
            copyComponentByReflection(srcRegistry, dstRegistry, srcEntity, dstEntity, componentName, typeIndex);
        }
    }

    return newScene;
}



} // namespace ya
