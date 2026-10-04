#include "Scene/Serialization/SceneSerializer.h"
#include "Core/Common/JsonFormat.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Log.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/AssetManager.h"
#include "Scene3D/ManagedChildComponent.h"
#include "ECS/ECSRegistry.h"
#include "ECS/Entity.h"
#include "Scene/Core/SceneWidgetEntry.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace ya
{

namespace
{

constexpr double SCENE_JSON_FLOAT_EPSILON = 1e-6;
constexpr double SCENE_JSON_FLOAT_SCALE   = 1000000.0;

double normalizeSceneFloat(double value)
{
    const double rounded = std::round(value * SCENE_JSON_FLOAT_SCALE) / SCENE_JSON_FLOAT_SCALE;
    return std::abs(rounded) < SCENE_JSON_FLOAT_EPSILON ? 0.0 : rounded;
}

void normalizeSceneJsonNumbers(nlohmann::json& json)
{
    if (json.is_number_float()) {
        json = normalizeSceneFloat(json.get<double>());
        return;
    }

    if (json.is_array()) {
        for (auto& item : json) {
            normalizeSceneJsonNumbers(item);
        }
        return;
    }

    if (json.is_object()) {
        for (auto& [_, value] : json.items()) {
            normalizeSceneJsonNumbers(value);
        }
    }
}

void normalizeSceneJsonPaths(nlohmann::json& json)
{
    const auto normalizeStringField = [&](nlohmann::json& value, bool bAssetPath) {
        if (!value.is_string()) {
            return;
        }

        auto path = value.get<std::string>();
        if (path.empty()) {
            return;
        }

        value = bAssetPath ? AssetManager::normalizeAssetPath(path)
                           : AssetManager::normalizeScriptAssetPath(path);
    };

    if (json.is_array()) {
        for (auto& item : json) {
            normalizeSceneJsonPaths(item);
        }
        return;
    }

    if (!json.is_object()) {
        return;
    }

    for (auto& [key, value] : json.items()) {
        if (key == "scriptPath") {
            normalizeStringField(value, false);
            continue;
        }
        if (key == "filepath") {
            normalizeStringField(value, true);
            continue;
        }
        if (key == "files" && value.is_array()) {
            for (auto& entry : value) {
                normalizeStringField(entry, true);
            }
            continue;
        }

        normalizeSceneJsonPaths(value);
    }
}

// Drop reflected fields that still equal a default-constructed instance.
// Objects recurse (including `__base__`). An object that trims down to empty
// is removed when the default is also an object. Arrays compare as a whole.
// Returns true when `value` itself should be removed.
bool omitFieldsEqualToDefault(nlohmann::json& value, const nlohmann::json& defaults)
{
    if (value.is_object() && defaults.is_object()) {
        std::vector<std::string> drop;
        for (auto it = value.begin(); it != value.end(); ++it) {
            const auto found = defaults.find(it.key());
            if (found == defaults.end()) {
                continue;
            }
            if (omitFieldsEqualToDefault(it.value(), *found)) {
                drop.push_back(it.key());
            }
        }
        for (const std::string& key : drop) {
            value.erase(key);
        }
        return value.empty();
    }

    if (value.is_array() && defaults.is_array()) {
        return value == defaults;
    }

    if (value.is_object() || value.is_array() || defaults.is_object() || defaults.is_array()) {
        return false;
    }

    return value == defaults;
}

// Same serializeByRuntimeReflection path as the live component (typeIndex +
// name), cached per type so each entity does not construct another T{}.
// The pointer stays valid until the next cache insertion.
const nlohmann::json* cachedDefaultReflectionJson(ya::type_index_t typeIndex, const std::string& typeName)
{
    struct Entry
    {
        bool           bHasDefault = false;
        nlohmann::json json;
    };

    static std::unordered_map<ya::type_index_t, Entry> cache;
    if (const auto it = cache.find(typeIndex); it != cache.end()) {
        return it->second.bHasDefault ? &it->second.json : nullptr;
    }

    Entry entry;
    const auto* ops = ECSRegistry::get().getComponentOps(typeIndex);
    const std::shared_ptr<void> instance = ops ? ops->createDefaultInstance() : nullptr;
    if (instance) {
        entry.bHasDefault = true;
        entry.json = ReflectionSerializer::serializeByRuntimeReflection(instance.get(), typeIndex, typeName);
    }

    const auto it = cache.emplace(typeIndex, std::move(entry)).first;
    return it->second.bHasDefault ? &it->second.json : nullptr;
}

} // namespace

// std::unordered_map<std::string, ComponentSerializer>   SceneSerializer::_componentSerializers;
// std::unordered_map<std::string, ComponentDeserializer> SceneSerializer::_componentDeserializers;

// ============================================================================
// 保存/加载文件
// ============================================================================

bool SceneSerializer::saveToFile(const std::string& filepath)
{
    YA_PROFILE_FUNCTION_LOG();
    try {
        nlohmann::json j = serialize();
        normalizePaths(j);
        normalizeSceneJsonNumbers(j);
        VirtualFileSystem::get()->saveToFile(filepath, dumpJsonCompactLeaves(j));
        YA_CORE_INFO("Scene saved to: {}", filepath);
        return true;
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("Failed to save scene: {}", e.what());
        return false;
    }
}

bool SceneSerializer::loadFromFile(const std::string& filepath)
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
    catch (const std::exception& e) {
        YA_CORE_ERROR("Failed to load scene: {}", e.what());
        return false;
    }
}

// ============================================================================
// Scene 序列化
// ============================================================================

nlohmann::json SceneSerializer::serialize()
{
    YA_PROFILE_FUNCTION();

    // Ensure all deferred reflection registrations are processed before serializing.
    // Template classes may trigger their registration during component construction
    // (e.g., when a scene is loaded), which queues deferred init lambdas. Without
    // this flush, parent-class reflection info may be missing from __base__ blocks.
    ::ya::reflection::DeferredInitializerQueue::instance().executeAll();

    nlohmann::json j;

    // Scene metadata
    j["version"] = "1.0";
    j["name"]    = _scene->getName();

    // ★ Step 1: 平铺序列化所有 Entities（跳过 scene_root）
    j["entities"]  = nlohmann::json::array();
    auto& registry = _scene->getRegistry();

    // 获取 scene_root 的 Entity handle（避免在循环中重复字符串比较）
    entt::entity sceneRootHandle = entt::null;
    if (_scene->_rootNode && _scene->_rootNode->getEntity()) {
        sceneRootHandle = _scene->_rootNode->getEntity()->getHandle();
    }

    std::vector<Entity*> entities;
    registry.view<entt::entity>(entt::exclude<ManagedChildComponent>).each([&](auto entityID)
                                                                           {
        Entity* entity = _scene->getEntityByEnttID(entityID);
        if (entity) {
            // ★ 跳过 scene_root Entity（使用句柄比较代替字符串比较，性能更好）
            if (entity->getHandle() == sceneRootHandle) {
                return;
            }

            entities.push_back(entity);
        } });

    std::sort(entities.begin(), entities.end(), [](const Entity* lhs, const Entity* rhs)
              {
        const auto* lhsIdComponent = lhs->getComponent<IDComponent>();
        const auto* rhsIdComponent = rhs->getComponent<IDComponent>();
        const uint64_t lhsId       = lhsIdComponent ? lhsIdComponent->_id.value : 0;
        const uint64_t rhsId       = rhsIdComponent ? rhsIdComponent->_id.value : 0;
        if (lhsId != rhsId) {
            return lhsId < rhsId;
        }
        return lhs->name < rhs->name; });

    {
        YA_PROFILE_SCOPE("SceneSerializer::SerializeEntities");
        for (Entity* entity : entities) {
            j["entities"].push_back(serializeEntity(entity));
        }
    }

    // ★ Step 2: 树状序列化 NodeTree（只存引用）
    Node* rootNode = _scene->getRootNode();
    if (rootNode && rootNode->hasChildren()) {
        j["nodeTree"]             = nlohmann::json::object();
        j["nodeTree"]["name"]     = rootNode->getName();
        j["nodeTree"]["children"] = nlohmann::json::array();

        YA_PROFILE_SCOPE("SceneSerializer::SerializeNodeTree");
        for (Node* child : rootNode->getChildren()) {
            const nlohmann::json childJson = serializeNodeTree(child);
            if (!childJson.empty()) {
                j["nodeTree"]["children"].push_back(childJson);
            }
        }
    }

    // ★ Step 3: Game UI authoring entries. The scene tree never contains UI
    // anymore; entries are the only authoring fact source.
    {
        YA_PROFILE_SCOPE("SceneSerializer::SerializeWidgetEntries");
        if (!_scene->_widgetEntries.empty()) {
            j["widgetEntries"] = nlohmann::json::array();
            for (const auto& entry : _scene->_widgetEntries) {
                j["widgetEntries"].push_back(entry.toJson());
            }
        }
    }

    return j;
}

void SceneSerializer::normalizePaths(nlohmann::json& j)
{
    normalizeSceneJsonPaths(j);
}

void SceneSerializer::deserialize(const nlohmann::json& j)
{
    YA_PROFILE_FUNCTION();

    auto normalizedJson = j;
    normalizePaths(normalizedJson);

    // 清空当前场景
    _scene->clear();

    // 设置场景名称
    if (normalizedJson.contains("name")) {
        _scene->setName(normalizedJson["name"].get<std::string>());
    }

    // ★ Step 1: 先反序列化所有 Entities（平铺创建）
    std::unordered_map<uint64_t, Entity*> entityMap; // uuid -> Entity*
    if (normalizedJson.contains("entities")) {
        YA_PROFILE_SCOPE("SceneSerializer::DeserializeEntities");
        for (const auto& entityJson : normalizedJson["entities"]) {
            Entity* entity = deserializeEntity(entityJson);
            if (entity) {
                uint64_t uuid   = entityJson["id"].get<uint64_t>();
                entityMap[uuid] = entity;
            }
        }
    }

    auto node = _scene->getRootNode();

    // ★ Step 2: 反序列化 NodeTree（重建树状结构）
    if (normalizedJson.contains("nodeTree")) {
        const auto& nodeTreeJson = normalizedJson["nodeTree"];
        if (nodeTreeJson.contains("children")) {
            YA_PROFILE_SCOPE("SceneSerializer::DeserializeNodeTree");
            for (const auto& childJson : nodeTreeJson["children"]) {
                deserializeNodeTree(childJson, node, entityMap);
            }
        }
    }

    // ★ Step 3: Game UI authoring entries (new format). Legacy UI stored as
    // nodeType subtrees is converted to entries by the deserializer below.
    if (normalizedJson.contains("widgetEntries")) {
        YA_PROFILE_SCOPE("SceneSerializer::DeserializeWidgetEntries");
        const auto& entriesJson = normalizedJson["widgetEntries"];
        if (entriesJson.is_array()) {
            for (const auto& entryJson : entriesJson) {
                if (!entryJson.is_object()) {
                    continue;
                }
                SceneWidgetEntry entry = SceneWidgetEntry::fromJson(entryJson);
                if (!entry.entryId.empty() || !entry.documentPath.empty()) {
                    _scene->addWidgetEntry(std::move(entry));
                }
            }
        }
    }
}

// ============================================================================
// Entity 序列化
// ============================================================================

nlohmann::json SceneSerializer::serializeEntity(Entity* entity)
{
    YA_PROFILE_FUNCTION();

    nlohmann::json j;

    // Entity ID
    j["id"] = entity->getComponents<IDComponent>()._id.value; // uuid

    // ★ Entity 名字直接从 Entity 读取（不再从 Node 读取）
    j["name"] = entity->name.empty() ? "Entity" : entity->name;

    // Serialize components
    j["components"] = nlohmann::json::object();

    auto&        registry   = _scene->getRegistry();
    entt::entity handle     = entity->getHandle();
    auto&        components = j["components"];

    auto& reg = ECSRegistry::get();

    static std::unordered_set<FName> ignoredComponents = {
        FName("IDComponent"),
    };

    for (auto& [name, typeIndex] : reg.getTypeIndexCache()) {
        if (ignoredComponents.contains(name)) {
            continue;
        }

        void* componentPtr = reg.getComponent(name, registry, handle);
        if (!componentPtr) {
            continue;
        }

        nlohmann::json componentJson;
        const auto* ops = reg.getComponentOps(typeIndex);
        const std::string typeName = name.toString();
        // Only the reflection payload is trimmed. Custom output is appended
        // afterwards and is kept even when every reflected field is default.
        if (!ops || ops->useReflectionSerialization(componentPtr)) {
            componentJson = ::ya::ReflectionSerializer::serializeByRuntimeReflection(componentPtr, typeIndex, typeName);
            if (ops) {
                if (const nlohmann::json* defaults = cachedDefaultReflectionJson(typeIndex, typeName)) {
                    omitFieldsEqualToDefault(componentJson, *defaults);
                }
            }
            if (!componentJson.is_object()) {
                componentJson = nlohmann::json::object();
            }
        }
        if (ops) {
            ops->serializeCustom(componentPtr, componentJson);
        }

        components[name.toString()] = std::move(componentJson);
    }

    return j;
}

Entity* SceneSerializer::deserializeEntity(const nlohmann::json& j)
{
    YA_PROFILE_FUNCTION();

    // ★ 只创建 Entity（不创建 Node，Node 由 NodeTree 反序列化时创建）
    std::string name = j["name"].get<std::string>();
    uint64_t    uuid = j["id"].get<uint64_t>();

    Entity* entity = _scene->createEntityWithUUID(uuid, name);
    if (!entity) {
        YA_CORE_ERROR("Failed to create entity '{}'", name);
        return nullptr;
    }

    static std::unordered_set<FName> ignoredComponents = {
        FName("IDComponent"),
    };

    // 反序列化组件
    if (j.contains("components")) {
        auto& components = j["components"];

        if (components.is_null() || components.size() < 1) {
            return entity;
        }

        auto& reg = ECSRegistry::get();

        for (auto& [typeName, componentJ] : components.items()) {
            if (ignoredComponents.contains(FName(typeName))) {
                continue;
            }
            auto typeIndex = reg.getTypeIndex(FName(typeName));
            if (typeIndex) {
                auto  id           = *typeIndex;
                // A component created here belongs to this entity and says so.
                // Several consumers ask a component for its owner (a camera
                // builds its view from the owner transform), so an instance that
                // loads with no owner silently reads the wrong pose.
                void* componentPtr =
                    reg.addComponent(FName(typeName), _scene->getRegistry(), entity->getHandle(), entity);
                ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
                auto* ops = reg.getComponentOps(id);
                auto  cls = ClassRegistry::instance().getClass(id);
                if (!ops || ops->useReflectionSerialization(componentPtr)) {
                    if (cls) {
                        ::ya::ReflectionSerializer::deserializeByRuntimeReflection(componentPtr, id, componentJ, cls->name);
                    }
                }
                if (ops) {
                    ops->deserializeCustom(componentPtr, componentJ);
                }
                if (auto* component = static_cast<IComponent*>(componentPtr)) {
                    component->onPostSerialize();
                }
            }
        }
    }

    return entity;
}

// ============================================================================
// NodeTree 序列化（树状结构，只存引用）
// ============================================================================

nlohmann::json SceneSerializer::serializeNodeTree(Node* node)
{
    YA_PROFILE_FUNCTION();

    if (!node) {
        return nlohmann::json();
    }
    nlohmann::json j;
    j["name"] = node->getName();

    // ★ 如果 Node 关联了 Entity，存储 Entity 的 UUID 引用
    Entity* entity = node->getEntity();
    if (entity) {
        if (auto idComp = entity->getComponent<IDComponent>()) {
            j["entityRef"] = idComp->_id.value;
        }
    }
    // Recurse into authored children. Companions are skipped, and a node whose
    // only children are companions writes no key: deserialize treats a missing
    // `children` the same as an empty array.
    if (node->hasChildren()) {
        nlohmann::json children = nlohmann::json::array();
        for (Node* child : node->getChildren()) {
            if (Entity* childEntity = child->getEntity()) {
                if (_scene->getRegistry().any_of<ManagedChildComponent>(childEntity->getHandle())) {
                    continue;
                }
            }
            const nlohmann::json childJson = serializeNodeTree(child);
            if (!childJson.empty()) {
                children.push_back(childJson);
            }
        }
        if (!children.empty()) {
            j["children"] = std::move(children);
        }
    }

    return j;
}

void SceneSerializer::deserializeNodeTree(const nlohmann::json& j, Node* parent,
                                          const std::unordered_map<uint64_t, Entity*>& entityMap)
{
    YA_PROFILE_FUNCTION();

    if (!j.contains("name")) {
        return;
    }

    std::string name   = j["name"].get<std::string>();
    Entity*     entity = nullptr;

    // ★ 如果有 entityRef，从 entityMap 中查找对应的 Entity
    if (j.contains("entityRef")) {
        uint64_t uuid = j["entityRef"].get<uint64_t>();
        auto     it   = entityMap.find(uuid);
        if (it != entityMap.end()) {
            entity = it->second;
            entity->setName(name);
        }
        else {
            YA_CORE_WARN("NodeTree: Entity with UUID {} not found in entityMap", uuid);
        }
    }

    Node* node = nullptr;
    if (!node) {
        node = _scene->createNode(name, parent, entity);
    }

    if (!node) {
        YA_CORE_ERROR("Failed to create node '{}'", name);
        return;
    }

    if (j.contains("children")) {
        for (const auto& childJson : j["children"]) {
            deserializeNodeTree(childJson, node, entityMap);
        }
    }
}

} // namespace ya
