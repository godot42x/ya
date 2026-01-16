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

    // Entity ID and name
    j["id"]   = entity->getComponents<IDComponent>()._id.value; // uuid
    j["name"] = entity->_name;

    // Serialize components
    j["components"] = nlohmann::json::object();

    auto        &registry   = _scene->getRegistry();
    entt::entity handle     = entity->getHandle();
    auto        &components = j["components"];

    auto &reg = ECSRegistry::get();

    // YA_CORE_INFO("=== Serializing entity: {} (handle: {}) ===", entity->_name, (uint32_t)handle);
    // YA_CORE_INFO("Registry address: {}", (void *)&registry);

    // 直接检查 TagComponent
    // bool hasTag = registry.all_of<TagComponent>(handle);
    // YA_CORE_INFO("Entity has TagComponent (direct check): {}", hasTag);
    // if (hasTag) {
    //     auto *tagPtr = &registry.get<TagComponent>(handle);
    //     YA_CORE_INFO("TagComponent address: {}", (void *)tagPtr);
    // }

    // TODO: loop Entity::_components instead, now this field is not implemented
    //      so that we don't need to check the component_ptr or loop every component

    static std::unordered_set<FName> ignoredComponents = {
        FName("IDComponent"),
    };

    for (auto &[name, typeIndex] : reg._typeIndexCache) {
        if (ignoredComponents.contains(name)) {
            continue;
        }

        auto getterIt = reg._componentGetters.find(typeIndex);
        // if (getterIt == reg._componentGetters.end()) {
        //     YA_CORE_WARN("No getter found for component type: {}", name.toString());
        //     continue;
        // }
        // bool bTagComponent = registry.all_of<TagComponent>(handle);
        // YA_CORE_INFO("Checking component: {} (typeIndex: {}) - has component: {}", name.toString(), typeIndex, bTagComponent);

        auto getter = getterIt->second;
        // YA_CORE_TRACE("Checking component: {} for entity: {}", name.toString(), entity->_name);

        void *componentPtr = getter(registry, handle);
        // YA_CORE_TRACE("  -> componentPtr: {}", (void *)componentPtr);

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

    Entity                          *entity            = uuid ? _scene->createEntityWithUUID(uuid, name)
                                                              : _scene->createEntity(name);
    static std::unordered_set<FName> ignoredComponents = {
        FName("IDComponent"),
        FName("TagComponent"),
    };

    // 反序列化组件
    if (j.contains("components")) {
        auto &components = j["components"];

        if (components.is_null() || components.size() < 1) {
            return entity;
        }

        auto &registry = _scene->getRegistry();

        for (auto &[typeName, componentJ] : components.items()) {
            if (ignoredComponents.contains(FName(typeName))) {
                continue;
            }

            std::unordered_map<FName, uint32_t>::const_iterator typeIndexIt = ECSRegistry::get()._typeIndexCache.find(FName(typeName));
            if (typeIndexIt != ECSRegistry::get()._typeIndexCache.end()) {

                auto  typeIndex    = typeIndexIt->second;
                void *componentPtr = ECSRegistry::get()._componentCreators[typeIndex](registry, entity->getHandle());

                auto cls = ClassRegistry::instance().getClass(typeIndex);
                if (cls) {
                    ::ya::ReflectionSerializer::deserializeByRuntimeReflection(componentPtr, typeIndex, componentJ, cls->_name);
                    // static_cast<IComponent *>(componentPtr)->onPostSerialize();
                }
            }
        }
    }

    return entity;
}


} // namespace ya
