#include "SceneSerializer.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Entity.h"



namespace ya
{


// std::unordered_map<std::string, ComponentSerializer>   SceneSerializer::_componentSerializers;
// std::unordered_map<std::string, ComponentDeserializer> SceneSerializer::_componentDeserializers;

// ============================================================================
// 保存/加载文件
// ============================================================================

bool SceneSerializer::saveToFile(const std::string &filepath)
{
    YA_PROFILE_FUNCTION_LOG();
    try {
        nlohmann::json j = serialize();
        VirtualFileSystem::get()->saveToFile(filepath, j.dump(4));
        YA_CORE_INFO("Scene saved to: {}", filepath);
        return true;
    }
    catch (const std::exception &e) {
        YA_CORE_ERROR("Failed to save scene: {}", e.what());
        return false;
    }
}

bool SceneSerializer::loadFromFile(const std::string &filepath)
{
    YA_PROFILE_FUNCTION_LOG();
    try {
        std::string content;
        VirtualFileSystem::get()->readFileToString(filepath, content);

        nlohmann::json j;
        j = nlohmann::json::parse(content);

        deserialize(j);
        YA_CORE_INFO("Scene loaded from: {}", filepath);
        return true;
    }
    catch (const std::exception &e) {
        YA_CORE_ERROR("Failed to load scene: {}", e.what());
        return false;
    }
}

// ============================================================================
// Scene 序列化
// ============================================================================

nlohmann::json SceneSerializer::serialize()
{
    nlohmann::json j;

    // Scene metadata
    j["version"] = "1.0";
    j["name"]    = _scene->getName();

    // ★ Step 1: 平铺序列化所有 Entities（跳过 scene_root）
    j["entities"]  = nlohmann::json::array();
    auto &registry = _scene->getRegistry();
    
    // 获取 scene_root 的 Entity handle（避免在循环中重复字符串比较）
    entt::entity sceneRootHandle = entt::null;
    if (_scene->_rootNode && _scene->_rootNode->getEntity()) {
        sceneRootHandle = _scene->_rootNode->getEntity()->getHandle();
    }
    
    registry.view<entt::entity>().each([&](auto entityID) {
        Entity *entity = _scene->getEntityByEnttID(entityID);
        if (entity) {
            // ★ 跳过 scene_root Entity（使用句柄比较代替字符串比较，性能更好）
            if (entity->getHandle() == sceneRootHandle) {
                return;
            }
            j["entities"].push_back(serializeEntity(entity));
        }
    });

    // ★ Step 2: 树状序列化 NodeTree（只存引用）
    Node *rootNode = _scene->getRootNode();
    if (rootNode && rootNode->hasChildren()) {
        j["nodeTree"]             = nlohmann::json::object();
        j["nodeTree"]["name"]     = rootNode->getName();
        j["nodeTree"]["children"] = nlohmann::json::array();

        for (Node *child : rootNode->getChildren()) {
            j["nodeTree"]["children"].push_back(serializeNodeTree(child));
        }
    }

    return j;
}

void SceneSerializer::deserialize(const nlohmann::json &j)
{
    // 清空当前场景
    _scene->clear();

    // 设置场景名称
    if (j.contains("name")) {
        _scene->setName(j["name"].get<std::string>());
    }

    // ★ Step 1: 先反序列化所有 Entities（平铺创建）
    std::unordered_map<uint64_t, Entity *> entityMap; // uuid -> Entity*
    if (j.contains("entities")) {
        for (const auto &entityJson : j["entities"]) {
            Entity *entity = deserializeEntity(entityJson);
            if (entity) {
                uint64_t uuid   = entityJson["id"].get<uint64_t>();
                entityMap[uuid] = entity;
            }
        }
    }

    // TODO: where to attach?
    //      like godot, attach one scene to one node
    auto node = _scene->getRootNode();

    // ★ Step 2: 反序列化 NodeTree（重建树状结构）
    if (j.contains("nodeTree")) {
        const auto &nodeTreeJson = j["nodeTree"];
        if (nodeTreeJson.contains("children")) {
            for (const auto &childJson : nodeTreeJson["children"]) {
                deserializeNodeTree(childJson, node, entityMap);
            }
        }
    }
}

// ============================================================================
// Entity 序列化
// ============================================================================

nlohmann::json SceneSerializer::serializeEntity(Entity *entity)
{
    nlohmann::json j;

    // Entity ID
    j["id"] = entity->getComponents<IDComponent>()._id.value; // uuid

    // ★ Entity 名字直接从 Entity 读取（不再从 Node 读取）
    j["name"] = entity->name.empty() ? "Entity" : entity->name;

    // Serialize components
    j["components"] = nlohmann::json::object();

    auto        &registry   = _scene->getRegistry();
    entt::entity handle     = entity->getHandle();
    auto        &components = j["components"];

    auto &reg = ECSRegistry::get();

    static std::unordered_set<FName> ignoredComponents = {
        FName("IDComponent"),
    };

    for (auto &[name, typeIndex] : reg._typeIndexCache) {
        if (ignoredComponents.contains(name)) {
            continue;
        }

        void *componentPtr = reg.getComponent(name, registry, handle);

        if (componentPtr) {
            components[name.toString()] = ::ya::ReflectionSerializer::serializeByRuntimeReflection(componentPtr, typeIndex, name.toString());
            // YA_CORE_TRACE("  -> Serialized component: {}", name.toString());
        }
    }
    return j;
}

Entity *SceneSerializer::deserializeEntity(const nlohmann::json &j)
{
    // ★ 只创建 Entity（不创建 Node，Node 由 NodeTree 反序列化时创建）
    std::string name = j["name"].get<std::string>();
    uint64_t    uuid = j["id"].get<uint64_t>();

    Entity *entity = _scene->createEntityWithUUID(uuid, name);
    if (!entity) {
        YA_CORE_ERROR("Failed to create entity '{}'", name);
        return nullptr;
    }

    static std::unordered_set<FName> ignoredComponents = {
        FName("IDComponent"),
    };

    // 反序列化组件
    if (j.contains("components")) {
        auto &components = j["components"];

        if (components.is_null() || components.size() < 1) {
            return entity;
        }

        auto &registry = _scene->getRegistry();
        auto &reg      = ECSRegistry::get();

        for (auto &[typeName, componentJ] : components.items()) {
            if (ignoredComponents.contains(FName(typeName))) {
                continue;
            }
            auto typeIndex = reg.getTypeIndex(FName(typeName));
            if (typeIndex) {
                auto  id           = *typeIndex;
                void *componentPtr = reg.addComponent(FName(typeName), registry, entity->getHandle());
                auto  cls          = ClassRegistry::instance().getClass(id);
                if (cls) {
                    ::ya::ReflectionSerializer::deserializeByRuntimeReflection(componentPtr, id, componentJ, cls->name);
                }
            }
        }
    }

    return entity;
}


// ============================================================================
// NodeTree 序列化（树状结构，只存引用）
// ============================================================================

nlohmann::json SceneSerializer::serializeNodeTree(Node *node)
{
    if (!node) {
        return nlohmann::json();
    }

    nlohmann::json j;
    j["name"] = node->getName();

    // ★ 如果 Node 关联了 Entity，存储 Entity 的 UUID 引用
    Entity *entity = node->getEntity();
    if (entity) {
        if (auto idComp = entity->getComponent<IDComponent>()) {
            j["entityRef"] = idComp->_id.value;
        }
    }

    // ★ 递归序列化子节点
    if (node->hasChildren()) {
        j["children"] = nlohmann::json::array();
        for (Node *child : node->getChildren()) {
            j["children"].push_back(serializeNodeTree(child));
        }
    }

    return j;
}

void SceneSerializer::deserializeNodeTree(const nlohmann::json &j, Node *parent,
                                          const std::unordered_map<uint64_t, Entity *> &entityMap)
{
    if (!j.contains("name")) {
        return;
    }

    std::string name   = j["name"].get<std::string>();
    Entity     *entity = nullptr;

    // ★ 如果有 entityRef，从 entityMap 中查找对应的 Entity
    if (j.contains("entityRef")) {
        uint64_t uuid = j["entityRef"].get<uint64_t>();
        auto     it   = entityMap.find(uuid);
        if (it != entityMap.end()) {
            entity = it->second;
        }
        else {
            YA_CORE_WARN("NodeTree: Entity with UUID {} not found in entityMap", uuid);
        }
    }

    // ★ 创建 Node3D（如果有 Entity 则关联，否则创建空 Node）
    // Node *node = nullptr;
    // if (entity) {
    //     // 为已存在的 Entity 创建 Node3D

    // 先默认必定存在 Entity 关联
    Node *node = _scene->createNode(name, parent, entity);

    // }
    // else {
    //     // 创建空 Node（没有关联 Entity）
    //     node = _scene->createNode(name, parent);
    // }

    if (!node) {
        YA_CORE_ERROR("Failed to create node '{}'", name);
        return;
    }

    // node->setName(name);

    // ★ 递归反序列化子节点
    if (j.contains("children")) {
        for (const auto &childJson : j["children"]) {
            deserializeNodeTree(childJson, node, entityMap);
        }
    }
}


} // namespace ya
