#include "GameEditor/Inspector/EditorComponentSectionRegistry.h"

#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/Animation/SpriteAnimationSetEditModel.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Sections/EditorLuaScriptSection.h"
#include "GameEditor/UI/Sections/EditorSpriteAnimationSection.h"
#include "Scene/Core/Scene.h"
#include "Scene2D/SpriteAnimationComponent.h"

namespace ya
{
namespace
{

SpriteAnimationComponent* animationOn(EditorLayer* layer, uint64_t uuid)
{
    if (!layer || uuid == 0) {
        return nullptr;
    }
    Scene* scene = layer->getHierarchyScene();
    if (!scene) {
        return nullptr;
    }
    Entity* entity = scene->getEntityByUUID(uuid);
    if (!entity || !entity->isValid()) {
        return nullptr;
    }
    return entity->tryGetComponent<SpriteAnimationComponent>();
}

EditorInspectorSectionHost makeLuaSection(const EditorInspectorSectionRequest& request)
{
    if (!request.layer) {
        return {};
    }
    auto section = std::make_shared<EditorLuaScriptSection>(
        request.name, *request.layer, request.entityUuid, request.onMutated, request.scriptPicker);
    return {
        .widget = section,
        .sync   = [section](WidgetTree& tree) { section->sync(tree); },
    };
}

EditorInspectorSectionHost makeSpriteAnimationSection(const EditorInspectorSectionRequest& request)
{
    auto section = std::make_shared<EditorSpriteAnimationSection>(
        request.name,
        [layer = request.layer, uuid = request.entityUuid]() { return animationOn(layer, uuid); },
        request.undo,
        request.onMutated,
        request.assetPicker,
        request.revealAsset,
        [layer = request.layer](std::string path) {
            if (!layer || path.empty()) {
                return;
            }
            layer->requestOpenAnimationSet(SpriteAnimationSetEditModel::canonicalAssetPath(path));
        },
        request.bReadOnly);
    return {
        .widget = section,
        .sync   = [section](WidgetTree& tree) { section->sync(tree); },
        .tick   = [section](float deltaSeconds) { section->tickSection(deltaSeconds); },
    };
}

} // namespace

EditorComponentSectionRegistry& EditorComponentSectionRegistry::instance()
{
    static EditorComponentSectionRegistry registry;
    return registry;
}

void EditorComponentSectionRegistry::add(type_index_t type, EMultiInstance multi, Factory factory)
{
    if (find(type)) {
        return;
    }
    _entries.push_back(Entry{type, multi, std::move(factory)});
}

const EditorComponentSectionRegistry::Entry* EditorComponentSectionRegistry::find(type_index_t type) const
{
    for (const Entry& entry : _entries) {
        if (entry.type == type) {
            return &entry;
        }
    }
    return nullptr;
}

EditorComponentSectionRegistry::EChoice EditorComponentSectionRegistry::choose(type_index_t type,
                                                                              size_t instanceCount) const
{
    const Entry* entry = find(type);
    if (!entry) {
        return EChoice::AutoProperty;
    }
    if (instanceCount == 1) {
        return EChoice::Custom;
    }
    return entry->multi == EMultiInstance::Skip ? EChoice::Skip : EChoice::AutoProperty;
}

void registerBuiltinInspectorSections()
{
    auto& registry = EditorComponentSectionRegistry::instance();
    registry.add(type_index_v<LuaScriptComponent>,
                 EditorComponentSectionRegistry::EMultiInstance::Skip,
                 makeLuaSection);
    registry.add(type_index_v<SpriteAnimationComponent>,
                 EditorComponentSectionRegistry::EMultiInstance::AutoProperty,
                 makeSpriteAnimationSection);
}

} // namespace ya
