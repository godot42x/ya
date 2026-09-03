#include "GameEditor/UI/EditorInspectorTab.h"
#include "GameEditor/UI/EditorAutoPropertySection.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "ECS/ECSRegistry.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <format>

namespace ya
{

namespace
{

void renameEntity(EditorLayer* layer, uint64_t uuid, const std::string& name)
{
    if (!layer || uuid == 0) {
        return;
    }
    Scene* scene = layer->getHierarchyScene();
    if (!scene) {
        return;
    }
    Entity* entity = scene->getEntityByUUID(uuid);
    if (!entity) {
        return;
    }
    if (Node* node = scene->getNodeByEntity(entity)) {
        node->setName(name);
    }
}

std::vector<Entity*> inspectorTargets(EditorLayer* layer)
{
    if (!layer) {
        return {};
    }
    std::vector<Entity*> targets = layer->getSelections();
    if (targets.empty()) {
        if (Entity* entity = layer->getSelectedEntity()) {
            targets.push_back(entity);
        }
    }
    std::erase_if(targets, [](Entity* entity) { return !entity || !entity->isValid() || !entity->getScene(); });
    return targets;
}

std::string projectedFingerprint(const std::vector<Entity*>& entities)
{
    if (entities.empty()) {
        return {};
    }
    std::vector<uint64_t> uuids;
    uuids.reserve(entities.size());
    for (Entity* entity : entities) {
        uint64_t uuid = 0;
        if (auto* id = entity->getComponent<IDComponent>()) {
            uuid = id->_id.value;
        }
        uuids.push_back(uuid);
    }
    std::sort(uuids.begin(), uuids.end());
    std::string fingerprint;
    for (uint64_t uuid : uuids) {
        if (!fingerprint.empty()) {
            fingerprint += ',';
        }
        fingerprint += std::to_string(uuid);
    }
    auto& registry = ECSRegistry::get();
    std::vector<std::string> common;
    for (const auto& [name, typeIndex] : registry.getTypeIndexCache()) {
        bool bAll = true;
        for (Entity* entity : entities) {
            if (!registry.getComponent(typeIndex, entity->getScene()->getRegistry(), entity->getHandle())) {
                bAll = false;
                break;
            }
        }
        if (bAll) {
            common.push_back(name.toString());
        }
    }
    std::sort(common.begin(), common.end());
    for (const std::string& name : common) {
        fingerprint += '|';
        fingerprint += name;
    }
    return fingerprint;
}

} // namespace

std::shared_ptr<UIElement> EditorInspectorTab::build(WidgetTree&)
{
    auto nameField = ui::textField("InspectorName").setFontSize(14);
    _nameField = nameField.share();
    _nameField->_onCommit = [this](const std::string& text) {
        if (!_layer) return;
        Entity* entity = _layer->getSelectedEntity();
        if (!entity) return;
        Scene* scene = _layer->getHierarchyScene();
        if (!scene) return;
        Node* node = scene->getNodeByEntity(entity);
        if (!node) return;
        const std::string old = node->getName();
        if (old == text) return;
        node->setName(text);
        if (!_undo) return;
        uint64_t uuid = 0;
        if (auto* id = entity->getComponent<IDComponent>()) {
            uuid = id->_id.value;
        }
        if (uuid == 0) return;
        (void)_undo->push({
            .label = "Rename",
            .undo  = [layer = _layer, uuid, old]() { renameEntity(layer, uuid, old); },
            .redo  = [layer = _layer, uuid, text]() { renameEntity(layer, uuid, text); },
        });
    };

    auto empty = ui::text("InspectorEmpty").setText("No selection").setStyleKey("text.muted");
    _emptyText = empty.share();
    auto entityText = ui::text("InspectorEntityId").setText("Entity ID: -").setFontSize(12).setStyleKey("text.muted");
    _entityText = entityText.share();
    auto projected = ui::column("InspectorProjected").setSpacing(8.0f);
    _projectedHost = projected.share();

    auto form = ui::column("InspectorForm")
                    .setPadding({10.0f, 8.0f})
                    .setSpacing(6.0f)
                    .child(ui::text("InspectorTitle").setText("INSPECTOR").setStyleKey("text.eyebrow"))
                    .child(std::move(entityText))
                    .child(ui::text("NameLabel").setText("Name").setFontSize(12))
                    .child(std::move(nameField), FBoxSlotArgs{.preferredSize = {220.0f, 26.0f}})
                    .child(std::move(empty))
                    .child(std::move(projected));
    return ui::panel("InspectorBody")
        .setStyleKey("panel")
        .child(std::move(form), ui::canvasSlot().fill())
        .release();
}

void EditorInspectorTab::rebuildProjected(WidgetTree& tree, const std::vector<Entity*>& entities)
{
    for (const auto& widget : _projectedWidgets) {
        if (widget && widget->isAttached()) {
            tree.detach(*widget);
        }
    }
    _projectedWidgets.clear();
    _projectedSections.clear();
    if (entities.empty() || !_projectedHost) {
        return;
    }

    struct FEntry
    {
        std::string           name;
        type_index_t          type = 0;
        std::vector<void*>    instances;
    };
    std::vector<FEntry> entries;
    auto& registry = ECSRegistry::get();
    for (const auto& [name, typeIndex] : registry.getTypeIndexCache()) {
        FEntry entry;
        entry.name = name.toString();
        entry.type = typeIndex;
        entry.instances.reserve(entities.size());
        bool bAll = true;
        for (Entity* entity : entities) {
            void* ptr = registry.getComponent(typeIndex, entity->getScene()->getRegistry(), entity->getHandle());
            if (!ptr) {
                bAll = false;
                break;
            }
            entry.instances.push_back(ptr);
        }
        if (bAll) {
            entries.push_back(std::move(entry));
        }
    }
    std::sort(entries.begin(), entries.end(), [](const FEntry& a, const FEntry& b) {
        return a.name < b.name;
    });

    std::string identity;
    for (Entity* entity : entities) {
        uint64_t uuid = 0;
        if (auto* id = entity->getComponent<IDComponent>()) {
            uuid = id->_id.value;
        }
        if (!identity.empty()) {
            identity += ',';
        }
        identity += std::to_string(uuid);
    }

    for (FEntry& entry : entries) {
        PropertyGraph graph = PropertyGraph::project(entry.type, std::move(entry.instances));
        if (!graph.hasRetainedEditors()) {
            continue;
        }
        auto title = std::make_shared<UIText>("InspectorComp_" + entry.name);
        title->setText(entry.name);
        title->setFontSize(12);
        title->setStyleKey("text.eyebrow");
        auto section = std::make_shared<EditorAutoPropertySection>(
            "InspectorProps_" + entry.name,
            std::move(graph),
            _undo,
            identity.empty() ? entry.name : identity + ":" + entry.name);
        if (!tree.attach(*_projectedHost, title).valid()) {
            continue;
        }
        if (!tree.attach(*_projectedHost, section).valid()) {
            tree.detach(*title);
            continue;
        }
        _projectedWidgets.push_back(title);
        _projectedWidgets.push_back(section);
        _projectedSections.push_back(section);
    }
}

void EditorInspectorTab::sync(WidgetTree& tree)
{
    if (!_layer) return;
    const std::vector<Entity*> entities = inspectorTargets(_layer);
    Entity* primary = entities.empty() ? nullptr : entities.front();
    const bool selected = !entities.empty();
    if (_emptyText) _emptyText->setVisibility(selected ? EWidgetVisibility::Hidden : EWidgetVisibility::Visible);
    if (_entityText) {
        if (entities.size() > 1) {
            _entityText->setText(std::format("Entities: {}", entities.size()));
        }
        else {
            _entityText->setText(primary ? std::format("Entity ID: {}", primary->getId()) : "Entity ID: -");
        }
    }
    UIElement* focused = tree.getFocused();
    if (_nameField) {
        _nameField->setEnabled(entities.size() == 1);
        if (focused != _nameField.get() && entities.size() == 1 && primary) {
            if (Scene* scene = _layer->getHierarchyScene()) {
                if (Node* node = scene->getNodeByEntity(primary)) _nameField->setText(node->getName());
            }
        }
    }

    const std::string fingerprint = projectedFingerprint(entities);
    if (fingerprint != _projectedFingerprint) {
        _projectedFingerprint = fingerprint;
        rebuildProjected(tree, entities);
    }
    for (const auto& section : _projectedSections) {
        if (section) {
            section->sync(tree);
        }
    }
}

bool EditorInspectorTab::wantsTextInput(WidgetTree& tree) const
{
    UIElement* focused = tree.getFocused();
    if (focused == _nameField.get()) {
        return true;
    }
    for (const auto& section : _projectedSections) {
        if (section && section->wantsTextInput(tree)) {
            return true;
        }
    }
    return false;
}

void EditorInspectorTab::reset()
{
    _nameField.reset();
    _entityText.reset();
    _emptyText.reset();
    _projectedHost.reset();
    _projectedWidgets.clear();
    _projectedSections.clear();
    _projectedFingerprint.clear();
}

} // namespace ya
