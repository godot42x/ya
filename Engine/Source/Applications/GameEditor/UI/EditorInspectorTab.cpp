#include "GameEditor/UI/EditorInspectorTab.h"
#include "GameEditor/UI/EditorAutoPropertySection.h"
#include "GameEditor/UI/EditorAssetPicker.h"
#include "GameEditor/UI/EditorHierarchyOps.h"
#include "Render/Adapters/Companion/CompanionManager.h"

#include "ECS/Component.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Entity.h"
#include "ECS/ECSRegistry.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Expander.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Panels/UIDesignerPanel.h"
#include "GameEditor/UI/EditorTheme.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneWidgetEntry.h"

#include <algorithm>
#include <format>
#include <string_view>

namespace ya
{

namespace
{

int inspectorComponentRank(std::string_view name)
{
    if (name == "TransformComponent") {
        return 0;
    }
    if (name == "DirectionComponent") {
        return 1;
    }
    return 100;
}

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
        layer->notifyHierarchyChanged();
    }
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

EditorAssetPickerCallback makeAssetPicker(EditorLayer* layer)
{
    return [layer](EEditorAssetPickerKind kind, std::string currentPath, std::function<void(std::string)> onPicked) {
        if (!layer) {
            return;
        }
        if (layer->_assetPickerHandler) {
            layer->_assetPickerHandler(kind, std::move(currentPath), std::move(onPicked));
            return;
        }
        switch (kind) {
        case EEditorAssetPickerKind::Texture:
            layer->_filePicker.openTexturePicker(currentPath, std::move(onPicked));
            break;
        case EEditorAssetPickerKind::Model:
        case EEditorAssetPickerKind::Mesh:
            layer->_filePicker.openModelPicker(currentPath, std::move(onPicked));
            break;
        }
    };
}

EditorRevealAssetCallback makeRevealAsset(EditorLayer* layer)
{
    return [layer](std::string vfsPath) {
        if (layer) {
            layer->revealInContentBrowser(std::move(vfsPath));
        }
    };
}

} // namespace

EditorInspectorTab::EditorInspectorTab(EditorLayer& layer, SelectionModel& selection, UndoStack* undo)
    : UICompoundWidget("InspectorBody", "panel")
    , _layer(&layer)
    , _selection(&selection)
    , _undo(undo)
{
    enableTick();
}

EditorInspectorTab::~EditorInspectorTab()
{
    unbindLayerDelegates();
}

void EditorInspectorTab::construct()
{
    auto nameField = ui::textField("InspectorName").setStyleKey(editorStyle(StyleKey::TextField));
    _nameField = nameField.share();
    _nameField->_onCommit = [this](const std::string& text) {
        if (!_layer) return;
        Entity* entity = nullptr;
        const std::vector<Entity*> targets = editorSelectionEntities(*_layer, _selection);
        if (targets.size() == 1) {
            entity = targets.front();
        }
        if (!entity) return;
        Scene* scene = _layer->getHierarchyScene();
        if (!scene) return;
        Node* node = scene->getNodeByEntity(entity);
        if (!node) return;
        const std::string old = node->getName();
        if (old == text) return;
        node->setName(text);
        _layer->notifyHierarchyChanged();
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

    auto empty = ui::text("InspectorEmpty")
                     .setText("Select an entity in the Hierarchy")
                     .setStyleKey("text.muted")
                     .setWrap(true);
    _emptyText = empty.share();
    auto entityText = ui::text("InspectorEntityId")
                          .setText("—")
                          .setStyleKey("text.small")
                          .setVAlign(EWidgetAlignV::Center);
    _entityText = entityText.share();
    auto projected = ui::column("InspectorProjected").setSpacing(editor_density::kSectionSpacing);
    _projectedHost = projected.share();

    const FBoxSlotArgs labelSlot{.preferredSize = {editor_density::kLabelColumn, editor_density::kRowHeight}};
    auto entityForm = ui::column("InspectorEntityForm")
                          .setSpacing(editor_density::kRowSpacing)
                          .setClipChildren(true)
                          .child(ui::row("InspectorIdRow")
                                     .setSpacing(editor_density::kControlSpacing)
                                     .child(ui::text("InspectorIdLabel")
                                                .setText("ID")
                                                .setStyleKey("text.muted")
                                                .setVAlign(EWidgetAlignV::Center),
                                            labelSlot)
                                     .child(std::move(entityText),
                                            ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight})))
                          .child(ui::row("InspectorNameRow")
                                     .setSpacing(editor_density::kControlSpacing)
                                     .child(ui::text("NameLabel")
                                                .setText("Name")
                                                .setStyleKey("text.muted")
                                                .setVAlign(EWidgetAlignV::Center),
                                            labelSlot)
                                     .child(std::move(nameField),
                                            ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight})))
                          .child(std::move(projected));
    _entityFormHost = entityForm.share();

    auto widgetEntryId = ui::text("InspectorWidgetEntryId").setStyleKey("text.muted").setVAlign(EWidgetAlignV::Center);
    _widgetEntryIdText = widgetEntryId.share();
    auto widgetEntryType = ui::text("InspectorWidgetEntryType").setStyleKey("text.small").setVAlign(EWidgetAlignV::Center);
    _widgetEntryTypeText = widgetEntryType.share();
    auto openDesigner = ui::button("InspectorOpenDesigner")
                            .child(ui::text("InspectorOpenDesignerLabel").setText("Open in UI Designer"));
    _openDesignerButton = openDesigner.share();
    _openDesignerButton->_onClick = [this]() {
        if (!_layer) {
            return;
        }
        SceneWidgetEntry* entry = _layer->getSelectedWidgetEntry();
        Scene* scene = _layer->getViewportInteractionScene();
        if (!entry || !scene || !entry->inlineDocument) {
            return;
        }
        _layer->getUIDesignerPanel().openSceneEntry(*scene, *entry);
    };

    auto widgetEntryForm = ui::column("InspectorWidgetEntryForm")
                               .setSpacing(editor_density::kRowSpacing)
                               .child(ui::text("InspectorWidgetEntryTitle")
                                          .setText("Game UI Entry")
                                          .setStyleKey("text.eyebrow"))
                               .child(ui::row("InspectorWidgetEntryIdRow")
                                          .setSpacing(editor_density::kControlSpacing)
                                          .child(ui::text("InspectorWidgetEntryIdLabel")
                                                     .setText("Entry")
                                                     .setStyleKey("text.muted")
                                                     .setVAlign(EWidgetAlignV::Center),
                                                 labelSlot)
                                          .child(std::move(widgetEntryId),
                                                 ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight})))
                               .child(ui::row("InspectorWidgetEntryTypeRow")
                                          .setSpacing(editor_density::kControlSpacing)
                                          .child(ui::text("InspectorWidgetEntryTypeLabel")
                                                     .setText("Type")
                                                     .setStyleKey("text.muted")
                                                     .setVAlign(EWidgetAlignV::Center),
                                                 labelSlot)
                                          .child(std::move(widgetEntryType),
                                                 ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight})))
                               .child(std::move(openDesigner),
                                      FBoxSlotArgs{.preferredSize = {0.0f, editor_density::kToolbarHeight}})
                               .setVisibility(EWidgetVisibility::Collapsed);
    _widgetEntryHost = widgetEntryForm.share();

    // Model instance notice. The instance root and its generated meshes look
    // alike in the viewport, so the Inspector states which one is selected and
    // what that means for edits — the alternative is a silent no-op after the
    // next rebuild.
    auto instanceBody = ui::text("InspectorInstanceBody")
                            .setStyleKey("text.small")
                            .setWrap(true);
    _instanceBodyText = instanceBody.share();
    auto instanceForm = ui::column("InspectorInstanceForm")
                            .setSpacing(editor_density::kRowSpacing)
                            .child(ui::text("InspectorInstanceTitle")
                                       .setText("Model Instance")
                                       .setStyleKey("text.eyebrow"))
                            .child(std::move(instanceBody))
                            .setVisibility(EWidgetVisibility::Collapsed);
    _instanceHost = instanceForm.share();

    auto form = ui::column("InspectorForm")
                    .setPadding({editor_density::kPanelPadding, editor_density::kPanelPadding})
                    .setSpacing(editor_density::kSectionSpacing)
                    .child(std::move(empty))
                    .child(std::move(entityForm))
                    .child(std::move(instanceForm))
                    .child(std::move(widgetEntryForm));
    addDetachedChild(ui::scroll("InspectorScroll")
                         .setAxis(EScrollAxis::Vertical)
                         .child(std::move(form), ui::contentSlot().fill())
                         .release());
}

void EditorInspectorTab::onAttached()
{
    bindLayerDelegates();
    refresh();
}

void EditorInspectorTab::onDetached()
{
    unbindLayerDelegates();
}

void EditorInspectorTab::bindLayerDelegates()
{
    unbindLayerDelegates();
    _selectionHandle = _layer->onSelectionChanged.addLambda(this, [this]() {
        refresh();
    });
    _hierarchyHandle = _layer->onHierarchyChanged.addLambda(this, [this]() {
        refresh();
    });
}

void EditorInspectorTab::unbindLayerDelegates()
{
    if (!_layer) {
        _selectionHandle = INVALID_HANDLE;
        _hierarchyHandle = INVALID_HANDLE;
        return;
    }
    if (_selectionHandle != INVALID_HANDLE) {
        _layer->onSelectionChanged.remove(_selectionHandle);
        _selectionHandle = INVALID_HANDLE;
    }
    if (_hierarchyHandle != INVALID_HANDLE) {
        _layer->onHierarchyChanged.remove(_hierarchyHandle);
        _hierarchyHandle = INVALID_HANDLE;
    }
}

void EditorInspectorTab::tick(float)
{
    WidgetTree* tree = getTree();
    if (!tree) {
        return;
    }
    syncProjectedValues(*tree);
}

void EditorInspectorTab::refresh()
{
    WidgetTree* tree = getTree();
    if (!tree) {
        return;
    }
    refreshFromTree(*tree);
    syncProjectedValues(*tree);
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
        const int rankA = inspectorComponentRank(a.name);
        const int rankB = inspectorComponentRank(b.name);
        if (rankA != rankB) {
            return rankA < rankB;
        }
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

    // A generated companion is not authored scene content: show its fields so
    // the user can see what draws, but never let them pretend to own it.
    bool bReadOnlySelection = false;
    for (Entity* entity : entities) {
        if (entity && !CompanionManager::isAuthorEditable(*entity)) {
            bReadOnlySelection = true;
            break;
        }
    }

    for (FEntry& entry : entries) {
        PropertyGraph graph = PropertyGraph::project(entry.type, std::move(entry.instances));
        if (!graph.hasRetainedEditors()) {
            continue;
        }
        std::vector<PropertyHandle::FInstanceBinding> rootBindings;
        rootBindings.reserve(entities.size());
        for (Entity* entity : entities) {
            Scene* scene = entity ? entity->getScene() : nullptr;
            const entt::entity handle = entity ? entity->getHandle() : entt::null;
            const type_index_t componentType = entry.type;
            uint64_t uuid = 0;
            if (entity) {
                if (auto* id = entity->getComponent<IDComponent>()) {
                    uuid = id->_id.value;
                }
            }
            rootBindings.push_back({
                .identity = _layer->getCurrentScenePath() + "#" + std::to_string(uuid) + ":" + entry.name,
                .resolver = [scene, handle, componentType]() -> void* {
                    if (!scene || handle == entt::null) {
                        return nullptr;
                    }
                    return ECSRegistry::get().getComponent(componentType, scene->getRegistry(), handle);
                },
            });
        }
        for (PropertyNode& node : graph.getNodesMutable()) {
            node.binding.setInstanceBindings(rootBindings);
        }
        // Generated companions are rebuilt from their host, so their fields are
        // shown but never writable: an edit would be silently reverted.
        if (bReadOnlySelection) {
            graph.markAllReadOnly();
        }
        auto section = std::make_shared<EditorAutoPropertySection>(
            "InspectorProps_" + entry.name,
            std::move(graph),
            _undo,
            identity.empty() ? entry.name : identity + ":" + entry.name,
            makeAssetPicker(_layer),
            makeRevealAsset(_layer));
        bool expanded = true;
        if (const auto it = _componentExpanded.find(entry.name); it != _componentExpanded.end()) {
            expanded = it->second;
        }
        else {
            _componentExpanded.emplace(entry.name, true);
        }
        auto expander = ui::collapsingHeader("InspectorComp_" + entry.name)
                            .setTitle(entry.name)
                            .setExpanded(expanded)
                            .setPadding({editor_density::kPanelPadding, editor_density::kPanelPadding})
                            .setSpacing(editor_density::kRowSpacing)
                            .child(section)
                            .share();
        expander->_onExpandedChanged = [this, name = entry.name](bool value) {
            _componentExpanded[name] = value;
        };
        if (!tree.attach(*_projectedHost, expander).valid()) {
            continue;
        }
        _projectedWidgets.push_back(std::move(expander));
        _projectedSections.push_back(section);
    }
}

void EditorInspectorTab::updateInstanceNotice(const std::vector<Entity*>& entities)
{
    if (!_instanceHost || !_instanceBodyText) {
        return;
    }

    Scene* scene = _layer ? _layer->getHierarchyScene() : nullptr;
    if (!scene || entities.size() != 1) {
        _instanceHost->setVisibility(EWidgetVisibility::Collapsed);
        return;
    }

    Entity* primary = entities.front();
    if (!primary || !primary->isValid()) {
        _instanceHost->setVisibility(EWidgetVisibility::Collapsed);
        return;
    }

    if (editorIsInstanceChild(primary)) {
        Entity* root = editorResolveInstanceRoot(*scene, primary);
        _instanceBodyText->setText(std::format(
            "Generated mesh of model instance '{}'. Material, parameter and transform edits last for this "
            "session only — rebuilding the instance restores the imported values. Delete, duplicate and "
            "reorder are disabled; select the instance root to act on the whole model.",
            root ? root->getName() : std::string_view("<unknown>")));
        _instanceHost->setVisibility(EWidgetVisibility::Visible);
        return;
    }

    if (const auto* model = primary->getComponent<ModelComponent>()) {
        _instanceBodyText->setText(std::format(
            "{} generated mesh(es) from '{}', sharing {} runtime material(s). Select a mesh in the Hierarchy "
            "— or Alt+click it in the viewport — to edit that mesh's material.",
            editorCountInstanceChildren(*scene, primary),
            model->_modelRef.getPath(),
            model->_cachedMaterials.size()));
        _instanceHost->setVisibility(EWidgetVisibility::Visible);
        return;
    }

    _instanceHost->setVisibility(EWidgetVisibility::Collapsed);
}

void EditorInspectorTab::refreshFromTree(WidgetTree& tree)
{
    if (!_layer) return;

    SceneWidgetEntry* widgetEntry = _layer->getSelectedWidgetEntry();
    const bool widgetMode = widgetEntry != nullptr;
    if (_widgetEntryHost) {
        _widgetEntryHost->setVisibility(widgetMode ? EWidgetVisibility::Visible : EWidgetVisibility::Collapsed);
    }
    if (widgetMode) {
        if (_entityFormHost) {
            _entityFormHost->setVisibility(EWidgetVisibility::Collapsed);
        }
        if (_instanceHost) {
            _instanceHost->setVisibility(EWidgetVisibility::Collapsed);
        }
        if (_emptyText) {
            _emptyText->setVisibility(EWidgetVisibility::Collapsed);
        }
        if (_widgetEntryIdText) {
            _widgetEntryIdText->setText(widgetEntry->entryId);
        }
        if (_widgetEntryTypeText) {
            if (widgetEntry->inlineDocument) {
                _widgetEntryTypeText->setText(widgetEntry->inlineDocument->typeId);
            }
            else {
                _widgetEntryTypeText->setText("<invalid: no document>");
            }
        }
        if (_openDesignerButton) {
            _openDesignerButton->setEnabled(widgetEntry->inlineDocument != nullptr);
        }
        if (!_projectedFingerprint.empty()) {
            _projectedFingerprint.clear();
            rebuildProjected(tree, {});
        }
        return;
    }

    const std::vector<Entity*> entities = editorSelectionEntities(*_layer, _selection);
    Entity* primary = entities.empty() ? nullptr : entities.front();
    const bool selected = !entities.empty();
    if (_emptyText) {
        _emptyText->setVisibility(selected ? EWidgetVisibility::Collapsed : EWidgetVisibility::Visible);
    }
    if (_entityFormHost) {
        _entityFormHost->setVisibility(selected ? EWidgetVisibility::Visible : EWidgetVisibility::Collapsed);
    }
    updateInstanceNotice(entities);
    if (_entityText) {
        if (entities.size() > 1) {
            _entityText->setText(std::format("{} selected", entities.size()));
        }
        else if (primary) {
            _entityText->setText(std::format("{}", primary->getId()));
        }
        else {
            _entityText->setText("—");
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
}

void EditorInspectorTab::syncProjectedValues(WidgetTree& tree)
{
    for (const auto& section : _projectedSections) {
        if (section) {
            section->sync(tree);
        }
    }
}

} // namespace ya
