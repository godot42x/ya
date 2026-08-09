#include "SceneSerializer.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Log.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/AssetManager.h"
#include "Scene3D/ManagedChildComponent.h"
#include "ECS/Entity.h"
#include "GUI/Widgets/LegacyUIMigration.h"
#include "GUI/Widgets/SceneWidgetEntry.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ya
{

namespace
{

/// Stable-ish entryId for migrated legacy UI nodes: the node name,
/// deduplicated against existing entries.
std::string makeEntryId(const Scene& scene, const std::string& base)
{
    const auto& entries = scene.getWidgetEntries();
    const auto  bTaken  = [&](const std::string& id) {
        for (const auto& entry : entries) {
            if (entry.entryId == id) {
                return true;
            }
        }
        return false;
    };

    std::string id = base;
    int         suffix = 1;
    while (bTaken(id)) {
        id = base + "_" + std::to_string(suffix++);
    }
    return id;
}

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
        VirtualFileSystem::get()->saveToFile(filepath, j.dump(4, ' ', false));
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
                if (!entry.entryId.empty() || entry.inlineDocument || !entry.documentPath.empty()) {
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
        if (!ops || ops->useReflectionSerialization(componentPtr)) {
            componentJson = ::ya::ReflectionSerializer::serializeByRuntimeReflection(componentPtr, typeIndex, name.toString());
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
                void* componentPtr = reg.addComponent(FName(typeName), _scene->getRegistry(), entity->getHandle());
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
    // ★ 递归序列化子节点（跳过被动态管理的子节点）
    if (node->hasChildren()) {
        j["children"] = nlohmann::json::array();
        for (Node* child : node->getChildren()) {
            if (Entity* childEntity = child->getEntity()) {
                if (_scene->getRegistry().any_of<ManagedChildComponent>(childEntity->getHandle())) {
                    continue;
                }
            }
            j["children"].push_back(serializeNodeTree(child));
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

    // Legacy Game UI: Node2D-era subtrees migrate to SceneWidgetEntry authoring
    // data (inline UIDocuments) instead of live scene-tree nodes. Children are
    // consumed by the migration (canvas children become separate entries).
    if (!entity && j.contains("nodeType")) {
        for (const auto& result : migrateLegacyUINode(j)) {
            SceneWidgetEntry entry;
            entry.entryId        = makeEntryId(*_scene, result.name);
            entry.inlineDocument = result.document;
            entry.zOrder         = result.zOrder;
            entry.autoMount      = true;
            _scene->addWidgetEntry(std::move(entry));
        }
        return;
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
