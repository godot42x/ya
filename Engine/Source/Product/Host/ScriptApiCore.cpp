#include "Core/Scripting/ScriptApiRegistry.h"

#include "Core/FName.h"
#include "ECS/ECSRegistry.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "Host/App.h"
#include "GUI/Widgets/SceneWidgetEntry.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Scene3D/Node3D.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace ya
{

namespace
{

using Json = ScriptApiRegistry::Json;
using Error = ScriptApiRegistry::Error;

Scene& requireActiveScene()
{
    Scene* scene = ScriptApiRegistry::get().getActiveScene();
    if (scene == nullptr) {
        throw Error("no active scene");
    }
    return *scene;
}

entt::entity requireEntityId(const Json& args, Scene& scene)
{
    const uint32_t id = args.value("id", 0u);
    Entity*        entity = scene.getEntityByEnttID(entt::entity{id});
    if (entity == nullptr) {
        throw Error(std::format("entity {} not found", id));
    }
    return entity->getHandle();
}

// Script widget handles: widgets created via ui.create are retained by the
// script session (detached by default); the Game UI service mounts/unmounts
// them into the presented world's WidgetTree.
using ScriptWidgetHandle = uint64_t;

std::unordered_map<ScriptWidgetHandle, UIElementRef>& scriptWidgets()
{
    static std::unordered_map<ScriptWidgetHandle, UIElementRef> widgets;
    return widgets;
}

ScriptWidgetHandle nextScriptWidgetHandle()
{
    static ScriptWidgetHandle next = 1;
    return next++;
}

UIElementRef requireScriptWidget(const Json& args)
{
    const auto handle = args.value("handle", 0ull);
    const auto it     = scriptWidgets().find(handle);
    if (it == scriptWidgets().end()) {
        throw Error(std::format("ui widget handle {} not found", handle));
    }
    return it->second;
}

std::vector<std::string> listRegisteredComponentTypes()
{
    std::vector<std::string> names;
    for (const auto& [fname, typeIndex] : ECSRegistry::get().getTypeIndexCache()) {
        (void)typeIndex;
        names.push_back(fname.toString());
    }
    std::sort(names.begin(), names.end());
    return names;
}

Json serializeComponent(const Scene& scene, entt::entity entity, const std::string& typeName)
{
    auto&       ecs       = ECSRegistry::get();
    const auto  typeIndex = ecs.getTypeIndex(FName(typeName));
    if (!typeIndex) {
        throw Error("unknown component type: " + typeName);
    }
    void* const ptr = ecs.getComponent(*typeIndex, scene.getRegistry(), entity);
    if (ptr == nullptr) {
        throw Error(std::format("entity does not have component '{}'", typeName));
    }
    return ReflectionSerializer::serializeByRuntimeReflection(ptr, *typeIndex, typeName);
}

Json entityToJson(const Scene& scene, const Entity& entity)
{
    Json components = Json::object();
    for (const std::string& typeName : listRegisteredComponentTypes()) {
        if (ECSRegistry::get().hasComponent(FName(typeName), scene.getRegistry(), entity.getHandle())) {
            components[typeName] = serializeComponent(scene, entity.getHandle(), typeName);
        }
    }
    return Json{
        {"id", static_cast<uint32_t>(entity.getHandle())},
        {"name", entity.getName()},
        {"components", std::move(components)},
    };
}

Json nodeToJson(const Scene& scene, const Node* node)
{
    Json entry{
        {"path", scene.getNodePath(node)},
        {"name", node->getName()},
    };

    if (const Entity* entity = node->getEntity()) {
        entry["type"]      = "Node3D";
        entry["entity_id"] = static_cast<uint32_t>(entity->getHandle());
    }
    else {
        entry["type"] = "Node";
    }

    Json children = Json::array();
    for (const Node* child : node->getChildren()) {
        children.push_back(nodeToJson(scene, child));
    }
    if (!children.empty()) {
        entry["children"] = std::move(children);
    }
    return entry;
}

Node* requireNodeByPath(Scene& scene, const Json& args)
{
    const std::string path = args.at("path").get<std::string>();
    Node* const       node = scene.findNodeByPath(path);
    if (!node) {
        throw Error(std::format("node path not found: {}", path));
    }
    return node;
}

/// Serialized reflected objects nest base-class fields under
/// `__base__.<BaseClass>`, so a flat field name like `_position` must be
/// resolved to its actual (possibly nested) location before writing.
bool setFieldDeep(Json& root, const std::string& key, const Json& value)
{
    if (root.is_object()) {
        if (auto it = root.find(key); it != root.end()) {
            *it = value;
            return true;
        }
        for (auto& [childKey, childValue] : root.items()) {
            if (childKey == "__base__" || childValue.is_object()) {
                if (setFieldDeep(childValue, key, value)) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

void registerCoreScriptApis(ScriptApiRegistry& registry)
{
    static bool bRegistered = false;
    if (bRegistered) {
        return;
    }
    bRegistered = true;

    // ========================================================================
    // Scene
    // ========================================================================
    registry.registerFunction(
        "scene.get_active",
        "Returns the active scene summary {name, entityCount}, or null when no scene is active.",
        Json::object(),
        [](const Json&) -> Json {
            Scene* scene = ScriptApiRegistry::get().getActiveScene();
            if (scene == nullptr) {
                return Json(nullptr);
            }
            return Json{
                {"name", scene->getName()},
                {"entityCount", scene->_entityMap.size()},
            };
        });

    registry.registerFunction(
        "scene.save",
        "Serializes the active scene to the given path.",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene&    scene = requireActiveScene();
            const auto path = args.at("path").get<std::string>();
            if (!ScriptApiRegistry::get().saveScene(path, scene)) {
                throw Error("scene.save failed");
            }
            return Json{{"saved", true}, {"path", path}};
        });

    registry.registerFunction(
        "scene.load",
        "Loads a scene from the given path and activates it.",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            const auto path = args.at("path").get<std::string>();
            if (!ScriptApiRegistry::get().loadScene(path)) {
                throw Error("scene.load failed");
            }
            return Json{{"loaded", true}, {"path", path}};
        });

    // ========================================================================
    // Entity
    // ========================================================================
    registry.registerFunction(
        "entity.create",
        "Creates an entity in the active scene. Args: {name}. Returns {id, name}.",
        Json{{"name", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene&    scene = requireActiveScene();
            const auto name = args.value("name", "Entity");
            // createEntity is private to internal systems; scripts create a
            // node-backed entity through the public API instead.
            Entity* entity = scene.createNode3D(name)->getEntity();
            return Json{
                {"id", static_cast<uint32_t>(entity->getHandle())},
                {"name", entity->getName()},
            };
        });

    registry.registerFunction(
        "entity.destroy",
        "Destroys the entity with the given id. Args: {id}.",
        Json{{"id", {{"type", "integer"}}}},
        [](const Json& args) -> Json {
            Scene&  scene = requireActiveScene();
            Entity* entity = scene.getEntityByEnttID(entt::entity{args.at("id").get<uint32_t>()});
            if (entity == nullptr) {
                throw Error("entity not found");
            }
            scene.destroyEntity(entity);
            return Json{{"destroyed", true}};
        });

    registry.registerFunction(
        "entity.list",
        "Lists all entities in the active scene: [{id, name, components:{TypeName: fields}}].",
        Json::object(),
        [](const Json&) -> Json {
            Scene& scene = requireActiveScene();
            Json   entities = Json::array();
            for (const auto& [handle, entity] : scene._entityMap) {
                (void)handle;
                entities.push_back(entityToJson(scene, entity));
            }
            return entities;
        });

    registry.registerFunction(
        "entity.get",
        "Returns one entity by id: {id, name, components:{TypeName: fields}}.",
        Json{{"id", {{"type", "integer"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle{args.at("id").get<uint32_t>()};
            Entity* const      entity = scene.getEntityByEnttID(handle);
            if (entity == nullptr) {
                throw Error("entity not found");
            }
            return entityToJson(scene, *entity);
        });

    // ========================================================================
    // Component
    // ========================================================================
    registry.registerFunction(
        "component.list_types",
        "Lists every component type registered in the engine.",
        Json::object(),
        [](const Json&) -> Json {
            Json types = Json::array();
            for (const std::string& name : listRegisteredComponentTypes()) {
                types.push_back(name);
            }
            return types;
        });

    registry.registerFunction(
        "component.add",
        "Adds a component to an entity. Args: {id, type}. Returns the component fields.",
        Json{{"id", {{"type", "integer"}}}, {"type", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle = requireEntityId(args, scene);
            const std::string  typeName = args.at("type").get<std::string>();
            auto&              ecs      = ECSRegistry::get();
            const auto         typeIndex = ecs.getTypeIndex(FName(typeName));
            if (!typeIndex) {
                throw Error("unknown component type: " + typeName);
            }
            void* ptr = ecs.getComponent(*typeIndex, scene.getRegistry(), handle);
            if (ptr == nullptr) {
                ptr = ecs.addComponent(*typeIndex, scene.getRegistry(), handle);
            }
            if (ptr == nullptr) {
                throw Error(std::format("failed to add component '{}'", typeName));
            }
            return serializeComponent(scene, handle, typeName);
        });

    registry.registerFunction(
        "component.remove",
        "Removes a component from an entity. Args: {id, type}. Returns {removed}.",
        Json{{"id", {{"type", "integer"}}}, {"type", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle = requireEntityId(args, scene);
            const std::string  typeName = args.at("type").get<std::string>();
            const bool removed = ECSRegistry::get().removeComponent(FName(typeName), scene.getRegistry(), handle);
            return Json{{"removed", removed}};
        });

    registry.registerFunction(
        "component.get",
        "Returns the reflected fields of a component. Args: {id, type}.",
        Json{{"id", {{"type", "integer"}}}, {"type", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle = requireEntityId(args, scene);
            return serializeComponent(scene, handle, args.at("type").get<std::string>());
        });

    registry.registerFunction(
        "component.set",
        "Updates reflected fields of a component. Args: {id, type, fields:{name: value}}.",
        Json{{"id", {{"type", "integer"}}}, {"type", {{"type", "string"}}}, {"fields", {{"type", "object"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            const entt::entity handle = requireEntityId(args, scene);
            const std::string  typeName = args.at("type").get<std::string>();

            auto&      ecs       = ECSRegistry::get();
            const auto typeIndex = ecs.getTypeIndex(FName(typeName));
            if (!typeIndex) {
                throw Error("unknown component type: " + typeName);
            }
            void* const ptr = ecs.getComponent(*typeIndex, scene.getRegistry(), handle);
            if (ptr == nullptr) {
                throw Error(std::format("entity does not have component '{}'", typeName));
            }

            // Merge the requested fields into the current reflected state, so
            // partial updates never clobber untouched fields.
            Json merged = ReflectionSerializer::serializeByRuntimeReflection(ptr, *typeIndex, typeName);
            for (const auto& [key, value] : args.at("fields").items()) {
                merged[key] = value;
            }
            ReflectionSerializer::deserializeByRuntimeReflection(ptr, *typeIndex, merged, typeName);
            static_cast<IComponent*>(ptr)->onPostSerialize();
            return merged;
        });

    // ========================================================================
    // Scene tree (world nodes, path-addressed). Game UI authoring uses the
    // ui.* service (WidgetTree); the legacy Node2D UI paths were removed.
    // ========================================================================

    registry.registerFunction(
        "node.list",
        "Lists the world scene tree: [{path, name, type, entity_id?, children?}].",
        Json::object(),
        [](const Json&) -> Json {
            Scene& scene = requireActiveScene();
            Json   tree  = Json::array();
            for (Node* child : scene.getRootNode()->getChildren()) {
                tree.push_back(nodeToJson(scene, child));
            }
            return Json{{"count", tree.size()}, {"nodes", std::move(tree)}};
        });

    registry.registerFunction(
        "node.get",
        "Returns one scene-tree node by slash path, e.g. /Root/Cube.",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            return nodeToJson(scene, requireNodeByPath(scene, args));
        });

    registry.registerFunction(
        "node.move",
        "Moves a node. Args: {path, parent_path?, index?} (root when parent_path omitted), "
        "or relative placement {path, before_path} / {path, after_path} against a sibling.",
        Json{{"path", {{"type", "string"}}},
             {"parent_path", {{"type", "string"}}},
             {"index", {{"type", "integer"}}},
             {"before_path", {{"type", "string"}}},
             {"after_path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            Node*  node  = requireNodeByPath(scene, args);

            // Relative placement against a sibling (same parent).
            if (args.contains("before_path") || args.contains("after_path")) {
                const bool     bBefore  = args.contains("before_path");
                const std::string relPath = args.value(bBefore ? "before_path" : "after_path", "");
                Node* const    sibling = scene.findNodeByPath(relPath);
                if (!sibling) {
                    throw Error(std::format("sibling path not found: {}", relPath));
                }
                Node* parent = sibling->getParent() ? sibling->getParent() : scene.getRootNode();
                size_t index = parent->getChildIndex(sibling);
                if (index == Node::NPOS) {
                    index = parent->getChildCount();
                }
                else if (!bBefore) {
                    ++index;
                }
                if (!scene.moveNode(node, parent, index)) {
                    throw Error(std::format("failed to move node '{}'", args.at("path").get<std::string>()));
                }
                return Json{{"path", scene.getNodePath(node)}};
            }

            Node*  parent = nullptr;
            if (const auto it = args.find("parent_path"); it != args.end() && !it->is_null()) {
                parent = scene.findNodeByPath(it->get<std::string>());
                if (!parent) {
                    throw Error(std::format("parent_path not found: {}", it->get<std::string>()));
                }
            }
            const size_t index = args.contains("index")
                                     ? args.at("index").get<size_t>()
                                     : (parent ? parent->getChildCount() : scene.getRootNode()->getChildCount());
            if (!scene.moveNode(node, parent, index)) {
                throw Error(std::format("failed to move node '{}'", args.at("path").get<std::string>()));
            }
            return Json{{"path", scene.getNodePath(node)}};
        });

    registry.registerFunction(
        "node.destroy",
        "Destroys a scene-tree node by path (entity-backed).",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            Scene& scene = requireActiveScene();
            Node*  node  = requireNodeByPath(scene, args);
            scene.destroyNode(node);
            return Json{{"destroyed", true}};
        });

    // ========================================================================
    // Game UI (WidgetTree / GameUIHost); scripts author UI through this
    // service.
    // ========================================================================
    registry.registerFunction(
        "ui.types",
        "Lists every registered Game UI widget type (stable registry type IDs).",
        Json::object(),
        [](const Json&) -> Json {
            Json types = Json::array();
            for (const std::string& typeId : UITypeRegistry::instance().getTypeIds()) {
                types.push_back(typeId);
            }
            return types;
        });

    registry.registerFunction(
        "ui.create",
        "Creates a detached Game UI widget. Args: {type, name?}. Returns a script handle.",
        Json{{"type", {{"type", "string"}}}, {"name", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            const std::string typeId = args.at("type").get<std::string>();
            UIElementRef widget = UITypeRegistry::instance().createInstance(typeId);
            if (!widget) {
                throw Error(std::format("unknown Game UI type '{}' (see ui.types)", typeId));
            }
            if (const auto it = args.find("name"); it != args.end() && !it->is_null()) {
                widget->_name = it->get<std::string>();
            }
            const ScriptWidgetHandle handle = nextScriptWidgetHandle();
            scriptWidgets().emplace(handle, widget);
            return Json{{"handle", handle}, {"type", typeId}, {"name", widget->_name}};
        });

    registry.registerFunction(
        "ui.set",
        "Updates reflected fields of a script widget. Args: {handle, fields:{name: value}}.",
        Json{{"handle", {{"type", "integer"}}}, {"fields", {{"type", "object"}}}},
        [](const Json& args) -> Json {
            UIElementRef widget = requireScriptWidget(args);
            UIInstanceOverrideSet overrides = UIInstanceOverrideSet::fromJson(args.at("fields"));
            if (!overrides.applyTo(*widget)) {
                throw Error(std::format("ui.set: one or more fields do not exist on type '{}'",
                                        widget->_typeId));
            }
            return Json{{"handle", args.at("handle").get<uint64_t>()}, {"type", widget->_typeId}};
        });

    registry.registerFunction(
        "ui.add_to_world",
        "Joins the widget to the active world's Game UI content layer. "
        "Args: {handle}. Unmounted automatically when the scene deactivates.",
        Json{{"handle", {{"type", "integer"}}}},
        [](const Json& args) -> Json {
            UIElementRef widget = requireScriptWidget(args);
            Scene*       scene  = ScriptApiRegistry::get().getActiveScene();
            App*         app    = App::get();
            if (!scene || !app || !app->getGameUIHost()) {
                throw Error("ui.add_to_world: no active world/game UI host");
            }
            const WidgetAttachment attachment = app->getGameUIHost()->addToWorld(*scene, widget);
            if (!attachment.valid()) {
                throw Error("ui.add_to_world: the active world is not presented");
            }
            return Json{{"handle", args.at("handle").get<uint64_t>()}, {"mounted", true}};
        });

    registry.registerFunction(
        "ui.detach",
        "Unmounts a script widget from its tree (widget stays alive). Args: {handle}.",
        Json{{"handle", {{"type", "integer"}}}},
        [](const Json& args) -> Json {
            UIElementRef widget = requireScriptWidget(args);
            if (WidgetTree* tree = widget->getTree()) {
                tree->detach(*widget);
            }
            return Json{{"handle", args.at("handle").get<uint64_t>()}, {"detached", true}};
        });

    registry.registerFunction(
        "ui.destroy",
        "Detaches and releases a script widget. Args: {handle}.",
        Json{{"handle", {{"type", "integer"}}}},
        [](const Json& args) -> Json {
            const auto handle = args.at("handle").get<uint64_t>();
            UIElementRef widget = requireScriptWidget(args);
            if (WidgetTree* tree = widget->getTree()) {
                tree->detach(*widget);
            }
            scriptWidgets().erase(handle);
            return Json{{"destroyed", true}};
        });

    // ========================================================================
    // Input mode (game / UI routing + cursor baseline)
    // ========================================================================
    auto& api = registry;
    const auto parseInputMode = [](const std::string& mode) -> EInputMode {
        if (mode == "gameandui") {
            return EInputMode::GameAndUI;
        }
        if (mode == "gameonly") {
            return EInputMode::GameOnly;
        }
        if (mode == "uionly") {
            return EInputMode::UIOnly;
        }
        throw Error("input mode must be 'gameandui' | 'gameonly' | 'uionly'");
    };
    const auto modeToString = [](EInputMode mode) -> std::string {
        switch (mode) {
            case EInputMode::GameAndUI: return "gameandui";
            case EInputMode::GameOnly: return "gameonly";
            case EInputMode::UIOnly: return "uionly";
        }
        return "gameandui";
    };
    const auto requireApp = []() -> App& {
        App* app = App::get();
        if (!app) {
            throw Error("input.*: app not available");
        }
        return *app;
    };

    api.registerFunction(
        "input.set_mode",
        "Hard-sets the runtime input mode ('gameandui'|'gameonly'|'uionly') and clears the mode stack.",
        Json{{"mode", {{"type", "string"}}}},
        [&](const Json& args) -> Json {
            App& app = requireApp();
            app.setInputMode(parseInputMode(args.value("mode", "gameandui")));
            return Json{{"mode", modeToString(app.getInputMode())}};
        });

    api.registerFunction(
        "input.push_mode",
        "Pushes a runtime input mode (e.g. 'uionly' for a pause menu); pop restores the previous one.",
        Json{{"mode", {{"type", "string"}}}},
        [&](const Json& args) -> Json {
            App& app = requireApp();
            app.pushInputMode(parseInputMode(args.value("mode", "gameandui")));
            return Json{{"mode", modeToString(app.getInputMode())}};
        });

    api.registerFunction(
        "input.pop_mode",
        "Restores the input mode active before the last push.",
        Json::object(),
        [&](const Json&) -> Json {
            App& app = requireApp();
            app.popInputMode();
            return Json{{"mode", modeToString(app.getInputMode())}};
        });

    api.registerFunction(
        "input.get_mode",
        "Returns the current runtime input mode: {mode: 'gameandui'|'gameonly'|'uionly'}.",
        Json::object(),
        [&](const Json&) -> Json {
            return Json{{"mode", modeToString(requireApp().getInputMode())}};
        });
}

} // namespace ya
