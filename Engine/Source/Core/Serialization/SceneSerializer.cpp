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

    // Serialize all entities
    j["entities"] = nlohmann::json::array();

    // 处理 ECS
    // TODO: 怎么处理树状Scene?
    auto &registry = _scene->getRegistry();
    // 遍历所有 Entity
    registry.view<entt::entity>().each([&](auto entityID) {
        // 通过 Scene 获取 Entity 包装器
        Entity *entity = _scene->getEntityByEnttID(entityID);
        if (entity) {
            j["entities"].push_back(serializeEntity(entity));
        }
    });

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

    // 反序列化所有 Entity
    if (j.contains("entities")) {
        for (const auto &entityJson : j["entities"]) {
            deserializeEntity(entityJson);
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

    // ★ 优先从 Node 读取名字，如果没有 Node 则从 Entity 读取
    Node* node = _scene->getNodeByEntity(entity);
    if (node) {
        j["name"] = node->getName();
    } else {
        j["name"] = entity->name.empty() ? "Entity" : entity->name;
    }

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
    // 创建 Entity
    std::string name = j["name"].get<std::string>();
    uint64_t    uuid = j["id"].get<uint64_t>();

    // ★ 改用 createNode 创建（自动创建 Node + Entity + TransformComponent）
    Node* node = _scene->createNode3D(name);
    if (!node) {
        YA_CORE_ERROR("Failed to create node for entity '{}'", name);
        return nullptr;
    }

    // ★ 设置 Node 的名字
    node->setName(name);

    // Cast to Node3D to access Entity
    auto *node3D = dynamic_cast<Node3D *>(node);
    Entity* entity = node3D ? node3D->getEntity() : nullptr;
    if (!entity) {
        YA_CORE_ERROR("Failed to get entity from node '{}'", name);
        return nullptr;
    }

    // 设置 UUID（覆盖自动生成的）
    if (auto idComp = entity->getComponent<IDComponent>()) {
        idComp->_id = UUID(uuid);
    }

    static std::unordered_set<FName> ignoredComponents = {
        FName("IDComponent"),
        FName("TransformComponent"), // ★ 跳过 TransformComponent，因为 createNode 已经创建了
    };

    // 反序列化组件
    if (j.contains("components")) {
        auto &components = j["components"];

        if (components.is_null() || components.size() < 1) {
            return entity;
        }

        auto &registry = _scene->getRegistry();

        auto &reg = ECSRegistry::get();
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
                    ::ya::ReflectionSerializer::deserializeByRuntimeReflection(componentPtr, id, componentJ, cls->_name);
                }
            }
        }

        // ★ 特殊处理：如果 JSON 中有 TransformComponent，需要反序列化到已存在的组件
        if (components.contains("TransformComponent")) {
            if (auto tc = entity->getComponent<TransformComponent>()) {
                auto typeIndex = reg.getTypeIndex(FName("TransformComponent"));
                if (typeIndex) {
                    ::ya::ReflectionSerializer::deserializeByRuntimeReflection(
                        tc, *typeIndex, components["TransformComponent"], "TransformComponent");
                }
            }
        }
    }

    return entity;
}


} // namespace ya
