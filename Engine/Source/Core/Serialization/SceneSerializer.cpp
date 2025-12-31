#include "SceneSerializer.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/Log.h"
#include "Core/Serialization/ReflectionSerializer.h"
#include "Core/System/FileSystem.h"
#include "ECS/Entity.h"
#include "SerializerRegistry.h"



namespace ya
{


// std::unordered_map<std::string, ComponentSerializer>   SceneSerializer::_componentSerializers;
// std::unordered_map<std::string, ComponentDeserializer> SceneSerializer::_componentDeserializers;

// ============================================================================
// 保存/加载文件
// ============================================================================

bool SceneSerializer::saveToFile(const std::string &filepath)
{
    YA_PROFILE_FUNCTION();
    try {
        nlohmann::json j = serialize();
        FileSystem::get()->saveToFile(filepath, j.dump(4));
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
    YA_PROFILE_FUNCTION();
    try {
        std::string content;
        FileSystem::get()->readFileToString(filepath, content);

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

    // Entity ID and name
    j["id"]   = (uint32_t)entity->getHandle();
    j["name"] = entity->_name;

    // Serialize components
    j["components"] = nlohmann::json::object();

    auto        &registry   = _scene->getRegistry();
    entt::entity handle     = entity->getHandle();
    auto        &components = j["components"];

    for (auto &[typeName, serializer] : ECSSerializerRegistry::get().getSerializers()) {
        serializer(registry, handle, components);
    }
    return j;
}

Entity *SceneSerializer::deserializeEntity(const nlohmann::json &j)
{
    // 创建 Entity
    std::string name = j["name"].get<std::string>();
    uint64_t    uuid = j["id"].get<uint64_t>();

    Entity *entity = uuid ? _scene->createEntityWithUUID(uuid, name)
                          : _scene->createEntity(name);

    // 反序列化组件
    if (j.contains("components")) {
        auto &components = j["components"];
        auto &registry   = _scene->getRegistry();

        for (auto &[typeName, deserializer] : ECSSerializerRegistry::get().getDeserializers()) {
            if (components.contains(typeName)) {
                deserializer(registry, entity->getHandle(), components[typeName]);
            }
        }
    }

    return _scene->getEntityByEnttID(entity->getHandle());
}


} // namespace ya
