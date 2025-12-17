#include "SceneSerializer.h"
#include "Core/Log.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/Material/MaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"


namespace ya
{

// ============================================================================
// 保存/加载文件
// ============================================================================

bool SceneSerializer::saveToFile(const std::string &filepath)
{
    try {
        nlohmann::json j = serialize();

        std::ofstream file(filepath);
        if (!file.is_open()) {
            YA_CORE_ERROR("Failed to open file for writing: {}", filepath);
            return false;
        }

        file << j.dump(4); // Pretty print with 4 spaces
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
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            YA_CORE_ERROR("Failed to open file for reading: {}", filepath);
            return false;
        }

        nlohmann::json j;
        file >> j;

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

    auto &registry = _scene->getRegistry();
    // 遍历所有 Entity
    registry.view<entt::entity>().each([&](auto entityID) {
        // 通过 Scene 获取 Entity 包装器
        Entity *entity = _scene->getEntityByID(entityID);
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

    auto        &registry = _scene->getRegistry();
    entt::entity handle   = entity->getHandle();

    // TransformComponent - 使用反射自动序列化
    if (registry.all_of<TransformComponent>(handle)) {
        auto &transform                       = registry.get<TransformComponent>(handle);
        j["components"]["TransformComponent"] = ReflectionSerializer::serialize(transform);
    }

    // CameraComponent - 使用反射自动序列化
    if (registry.all_of<CameraComponent>(handle)) {
        auto &camera                       = registry.get<CameraComponent>(handle);
        j["components"]["CameraComponent"] = ReflectionSerializer::serialize(camera);
    }

    // TODO: 添加更多组件的序列化
    // 可以通过反射自动遍历所有组件类型

    return j;
}

Entity *SceneSerializer::deserializeEntity(const nlohmann::json &j)
{
    // 创建 Entity
    std::string name = j.contains("name") ? j["name"].get<std::string>() : "Entity";
    uint64_t    uuid = j.contains("id") ? j["id"].get<uint64_t>() : 0;

    Entity *entity = uuid ? _scene->createEntityWithUUID(uuid, name)
                          : _scene->createEntity(name);

    // 反序列化组件
    if (j.contains("components")) {
        auto &components = j["components"];
        auto &registry   = _scene->getRegistry();

        // TransformComponent - 使用反射自动反序列化
        if (components.contains("TransformComponent")) {
            auto transform = ReflectionSerializer::deserialize<TransformComponent>(
                components["TransformComponent"]);
            registry.emplace<TransformComponent>(entity->getHandle(), transform);
        }

        // CameraComponent - 使用反射自动反序列化
        if (components.contains("CameraComponent")) {
            auto camera = ReflectionSerializer::deserialize<CameraComponent>(
                components["CameraComponent"]);
            registry.emplace<CameraComponent>(entity->getHandle(), camera);
        }

        // TODO: 添加更多组件的反序列化
    }

    return _scene->getEntityByID(entity->getHandle());
}

// ============================================================================
// 组件序列化器注册（基于反射的自动化版本）
// ============================================================================

void SceneSerializer::registerComponentSerializers()
{
    // 通过反射自动注册所有组件的序列化器
    // 这里展示手动注册的模板，未来可以通过反射系统自动化

    // TransformComponent
    _componentSerializers["TransformComponent"] =
        [](entt::registry &registry, entt::entity entity) -> nlohmann::json {
        auto &transform = registry.get<TransformComponent>(entity);
        return {
            {"position", SerializerHelper::toJson(transform._position)},
            {"rotation", SerializerHelper::toJson(transform._rotation)},
            {"scale", SerializerHelper::toJson(transform._scale)}};
    };

    _componentDeserializers["TransformComponent"] =
        [](entt::registry &registry, entt::entity entity, const nlohmann::json &j) {
            auto &transform = registry.emplace_or_replace<TransformComponent>(entity);
            transform.setPosition(SerializerHelper::toVec3(j["position"]));
            transform.setRotation(SerializerHelper::toVec3(j["rotation"]));
            transform.setScale(SerializerHelper::toVec3(j["scale"]));
        };

    // TODO: 使用反射系统自动注册
    // 伪代码：
    // for (auto& [typeName, classInfo] : ClassRegistry::instance().classes) {
    //     if (classInfo->hasBaseClass<IComponent>()) {
    //         registerComponentSerializer(typeName, classInfo);
    //     }
    // }
}

nlohmann::json SceneSerializer::serializeComponentByName(
    const std::string &typeName,
    entt::registry    &registry,
    entt::entity       entity)
{
    auto it = _componentSerializers.find(typeName);
    if (it != _componentSerializers.end()) {
        return it->second(registry, entity);
    }

    YA_CORE_WARN("No serializer found for component type: {}", typeName);
    return nlohmann::json::object();
}

} // namespace ya
