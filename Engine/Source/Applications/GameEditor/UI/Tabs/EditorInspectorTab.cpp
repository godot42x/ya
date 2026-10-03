#include "GameEditor/UI/Tabs/EditorInspectorTab.h"
#include "GameEditor/UI/Sections/EditorAutoPropertySection.h"
#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"
#include "GameEditor/UI/Dialogs/EditorFilePicker.h"
#include "GameEditor/UI/Ops/EditorComponentOps.h"
#include "GameEditor/UI/Ops/EditorHierarchyOps.h"
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
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/EditorUIDesignerSession.h"
#include "GameEditor/UI/Shell/EditorListRows.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
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
    return [layer](type_index_t refType, std::string currentPath, std::function<void(std::string)> onPicked) {
        if (!layer) {
            return;
        }
        if (layer->_assetPickerHandler) {
            layer->_assetPickerHandler(refType, std::move(currentPath), std::move(onPicked));
            return;
        }
        const std::optional<AssetTypeDesc> desc = AssetTypeRegistry::get().findByRefType(refType);
        if (!desc) {
            return;
        }
        layer->_filePicker.openAssetPicker(desc->displayName, desc->extensions, currentPath, std::move(onPicked));
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

EditorScriptPicker makeScriptPicker(EditorLayer* layer)
{
    return [layer](std::string currentPath, std::function<void(std::string)> onPicked) {
        if (!layer) {
            return;
        }
        if (layer->_filePickerHandler) {
            layer->_filePickerHandler(makeScriptFilePickerRequest(std::move(currentPath), std::move(onPicked)));
            return;
        }
        layer->_filePicker.openScriptPicker(currentPath, std::move(onPicked));
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
    if (_addComponentMenu) {
        _addComponentMenu->_onDismiss = nullptr;
        _addComponentMenu->close();
        _addComponentMenu.reset();
    }
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
        _layer->markSceneDirty();
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
    auto addComponent = labeledButton("InspectorAddComponent", "Add Component");
    addComponent.setOnClick([this]() { openAddComponentMenu(); });
    _addComponentButton = addComponent.share();

    auto entityForm = ui::column("InspectorEntityForm")
                          .setSpacing(editor_density::kRowSpacing)
                          .setClipChildren(true)
                          .child(_addComponentButton,
                                 ui::boxSlot().preferredSize({0.0f, editor_density::kToolbarHeight}))
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
    _openDesignerButton->onClicked.addLambda([this]() {
        if (!_layer) {
            return;
        }
        SceneWidgetEntry* entry = _layer->getSelectedWidgetEntry();
        if (!entry || entry->documentPath.empty()) {
            return;
        }
        (void)_layer->openDocumentEditor(EEditorDocumentKind::UI, entry->documentPath);
    });

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
                                                     .setText("Document")
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
    if (_addComponentMenu) {
        _addComponentMenu->_onDismiss = nullptr;
        _addComponentMenu->close();
        _addComponentMenu.reset();
    }
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

void EditorInspectorTab::tick(float deltaSeconds)
{
    WidgetTree* tree = getTree();
    if (!tree) {
        return;
    }
    for (EditorInspectorSectionHost& section : _customSections) {
        if (section.tick) {
            section.tick(deltaSeconds);
        }
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
    if (_addComponentMenu) {
        _addComponentMenu->_onDismiss = nullptr;
        _addComponentMenu->close();
        _addComponentMenu.reset();
    }
    for (const auto& widget : _projectedWidgets) {
        if (widget && widget->isAttached()) {
            tree.detach(*widget);
        }
    }
    _projectedWidgets.clear();
    _projectedSections.clear();
    _customSections.clear();
    if (entities.empty() || !_projectedHost) {
        if (_addComponentButton) {
            _addComponentButton->setVisibility(EWidgetVisibility::Collapsed);
        }
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

    bool bReadOnlySelection = false;
    for (Entity* entity : entities) {
        if (entity && !CompanionManager::isAuthorEditable(*entity)) {
            bReadOnlySelection = true;
            break;
        }
    }
    if (_addComponentButton) {
        const bool bCanAdd = !bReadOnlySelection &&
                             std::any_of(entities.begin(), entities.end(), [](Entity* entity) {
                                 return entity && canMutateAuthoringComponents(*entity);
                             });
        _addComponentButton->setVisibility(bCanAdd ? EWidgetVisibility::Visible
                                                   : EWidgetVisibility::Collapsed);
        _addComponentButton->setEnabled(bCanAdd);
    }

    auto attachExpander = [this, &tree](const std::string& name,
                                        std::shared_ptr<UIElement> body,
                                        type_index_t type,
                                        const std::vector<Entity*>& targets) -> bool {
        bool expanded = true;
        if (const auto it = _componentExpanded.find(name); it != _componentExpanded.end()) {
            expanded = it->second;
        }
        else {
            _componentExpanded.emplace(name, true);
        }

        auto column = ui::column("InspectorCompBody_" + name)
                          .setSpacing(editor_density::kRowSpacing)
                          .child(body);

        auto header = ui::collapsingHeader("InspectorComp_" + name)
                          .setTitle(name)
                          .setDisclosureKind(EDisclosureKind::Chevron)
                          .setExpanded(expanded)
                          .setPadding({editor_density::kPanelPadding, editor_density::kPanelPadding})
                          .setSpacing(editor_density::kRowSpacing);
        if (canRemoveAuthoringComponent(targets, type)) {
            auto remove = labeledButton("InspectorRemove_" + name, "Remove");
            // Sits on the framed expander bar: needs its own fill or it reads
            // as part of the bar (both are `raised` by default).
            (void)remove.setStyleKey(editorStyle("header_button"));
            (void)remove.setOnClick([this, type]() {
                const std::vector<Entity*> current = editorSelectionEntities(*_layer, _selection);
                if (!removeAuthoringComponent(current, type)) {
                    return;
                }
                noteSceneMutated();
                if (_layer) {
                    _layer->notifyHierarchyChanged();
                }
            });
            header.headerChild(std::move(remove),
                               ui::overlaySlot().hAlign(EUIOverlayAlignment::End)
                                                .vAlign(EUIOverlayAlignment::Center)
                                                .inset(FMargin{0.0f, 0.0f, 6.0f, 0.0f}));
        }

        auto expander = std::move(header).child(std::move(column)).share();
        expander->_onExpandedChanged = [this, name](bool value) {
            _componentExpanded[name] = value;
        };
        if (!tree.attach(*_projectedHost, expander).valid()) {
            return false;
        }
        _projectedWidgets.push_back(std::move(expander));
        return true;
    };

    registerBuiltinInspectorSections();
    for (FEntry& entry : entries) {
        const EditorComponentSectionRegistry::EChoice choice =
            EditorComponentSectionRegistry::instance().choose(entry.type, entities.size());
        if (choice == EditorComponentSectionRegistry::EChoice::Skip) {
            continue;
        }
        if (choice == EditorComponentSectionRegistry::EChoice::Custom) {
            uint64_t uuid = 0;
            if (auto* id = entities.front()->getComponent<IDComponent>()) {
                uuid = id->_id.value;
            }
            const EditorComponentSectionRegistry::Entry* sectionEntry =
                EditorComponentSectionRegistry::instance().find(entry.type);
            EditorInspectorSectionHost host =
                sectionEntry->make(EditorInspectorSectionRequest{
                    .name         = "InspectorCustom_" + entry.name,
                    .layer        = _layer,
                    .entityUuid   = uuid,
                    .undo         = _undo,
                    .bReadOnly    = bReadOnlySelection,
                    .onMutated    = [this]() { noteSceneMutated(); },
                    .assetPicker  = makeAssetPicker(_layer),
                    .revealAsset  = makeRevealAsset(_layer),
                    .scriptPicker = makeScriptPicker(_layer),
                });
            if (host.widget && attachExpander(entry.name, host.widget, entry.type, entities)) {
                _customSections.push_back(std::move(host));
            }
            continue;
        }

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
        section->setOnMutated([this]() { noteSceneMutated(); });
        if (attachExpander(entry.name, section, entry.type, entities)) {
            _projectedSections.push_back(std::move(section));
        }
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

    // The decision sits with the other instance helpers (and is unit tested
    // there); this tab only shows or hides the line.
    const std::string notice = editorInstanceNotice(*scene, primary);
    if (notice.empty()) {
        _instanceHost->setVisibility(EWidgetVisibility::Collapsed);
        return;
    }

    _instanceBodyText->setText(notice);
    _instanceHost->setVisibility(EWidgetVisibility::Visible);
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
            _widgetEntryTypeText->setText(widgetEntry->documentPath.empty()
                                              ? std::string("<invalid: no document>")
                                              : widgetEntry->documentPath);
        }
        if (_openDesignerButton) {
            _openDesignerButton->setEnabled(!widgetEntry->documentPath.empty());
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
    for (EditorInspectorSectionHost& section : _customSections) {
        if (section.sync) {
            section.sync(tree);
        }
    }
}

void EditorInspectorTab::noteSceneMutated()
{
    if (_layer) {
        _layer->markSceneDirty();
    }
}

void EditorInspectorTab::openAddComponentMenu()
{
    WidgetTree* tree = getTree();
    if (!tree || !_layer || !_addComponentButton) {
        return;
    }

    const std::vector<Entity*> entities = editorSelectionEntities(*_layer, _selection);
    std::vector<UIMenu::FItem> items;
    for (const auto& [name, type] : authoringComponentTypes()) {
        const bool bCanAdd = canAddAuthoringComponent(entities, type);
        items.push_back({
            .label    = name,
            .action   = [this, type]() {
                if (!_layer) {
                    return;
                }
                const std::vector<Entity*> current = editorSelectionEntities(*_layer, _selection);
                if (!addAuthoringComponent(current, type)) {
                    return;
                }
                noteSceneMutated();
                _layer->notifyHierarchyChanged();
            },
            .bEnabled = bCanAdd,
        });
    }
    if (items.empty()) {
        return;
    }

    if (_addComponentMenu) {
        _addComponentMenu->_onDismiss = nullptr;
        _addComponentMenu->close();
        _addComponentMenu.reset();
    }
    auto menu = UIMenu::create(std::move(items));
    _addComponentMenu = menu;
    menu->_onDismiss = [this]() { _addComponentMenu.reset(); };
    menu->openAt(*tree, _addComponentButton->getLayoutRect());
}

} // namespace ya
