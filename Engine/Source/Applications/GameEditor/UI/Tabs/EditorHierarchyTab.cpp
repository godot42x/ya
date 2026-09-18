#include "GameEditor/UI/Tabs/EditorHierarchyTab.h"
#include "GameEditor/UI/Ops/EditorHierarchyOps.h"
#include "Render/Adapters/Companion/CompanionManager.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneWidgetEntry.h"

#include <format>
#include <string_view>

namespace ya
{

namespace
{

bool parseWidgetEntryKey(const std::string& id, std::string& outEntryId)
{
    constexpr std::string_view kPrefix = "ui:";
    if (id.size() <= kPrefix.size() || id.compare(0, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    outEntryId = id.substr(kPrefix.size());
    return true;
}

UITreeView::FNode buildHierarchyNode(Node* node)
{
    UITreeView::FNode out;
    if (!node) {
        return out;
    }
    uint64_t uuid = 0;
    if (Entity* entity = node->getEntity()) {
        if (auto* id = entity->getComponent<IDComponent>()) {
            uuid = id->_id.value;
        }
    }
    out.id    = editorHierarchyEntityIdKey(uuid);
    out.label = node->getName();
    // Generated companions the author does not own (camera body, light icon)
    // show up so the object's contents are visible, but read as disabled: they
    // are rebuilt from the host, so nothing here is theirs to edit.
    if (Entity* entity = node->getEntity()) {
        out.bEnabled = CompanionManager::isAuthorEditable(*entity);
    }
    out.children.reserve(node->getChildCount());
    for (Node* child : node->getChildren()) {
        out.children.push_back(buildHierarchyNode(child));
    }
    return out;
}

} // namespace

EditorHierarchyTab::EditorHierarchyTab(EditorLayer& layer, SelectionModel& selection, ActionMap& actions)
    : UICompoundWidget("HierarchyBody", "panel.canvas")
    , _layer(&layer)
    , _selection(&selection)
    , _actions(&actions)
{
}

EditorHierarchyTab::~EditorHierarchyTab()
{
    unbindLayerDelegates();
}

void EditorHierarchyTab::construct()
{
    _filter = std::make_shared<Reactive<std::string>>("");
    auto filterField = ui::textField("HierarchyFilter")
                           .setStyleKey(editorStyle(StyleKey::TextField))
                           .setOnTextChanged([this](const std::string& text) {
                               if (_filter) {
                                   _filter->set(text);
                               }
                           });
    _filterField = filterField.share();

    _roots    = std::make_shared<ReactiveList<UITreeView::FNode>>();
    _treeView = ui::treeView("HierarchyTree")
                    .bindData(_roots)
                    .bindFilter(_filter)
                    .bindSelection(_selection ? _selection->primaryRef() : nullptr)
                    .setReorderable(true)
                    .setOnSelectionChanged([this](const std::string& id) {
                        if (!_layer || !_selection) {
                            return;
                        }
                        _selection->select(id);
                        uint64_t uuid = 0;
                        std::string entryId;
                        if (parseEditorHierarchyEntityIdKey(id, uuid)) {
                            if (Scene* scene = _layer->getHierarchyScene()) {
                                _layer->setSelectedEntity(scene->getEntityByUUID(uuid));
                            }
                        }
                        else if (parseWidgetEntryKey(id, entryId)) {
                            _layer->setSelectedWidgetEntryId(entryId);
                        }
                    })
                    .setOnReorderHandler([this](const std::string& fromId,
                                                const std::string& toId,
                                                int dropMode) {
                        if (!_layer) {
                            return;
                        }
                        Scene* scene = _layer->getHierarchyScene();
                        if (!scene) {
                            return;
                        }
                        if (Entity* moved = moveEditorHierarchyEntity(*scene, fromId, toId, dropMode)) {
                            _layer->notifyHierarchyChanged();
                            _layer->setSelectedEntity(moved);
                        }
                    })
                    .setOnContextMenu([this](const std::string&, const glm::vec2& logicalPoint) {
                        openContextMenu(logicalPoint);
                    })
                    .share();

    auto hierarchyScroll = ui::scroll("HierarchyScroll")
                               .child(_treeView, ui::contentSlot().fill());
    addDetachedChild(ui::column("HierarchyBodyInner")
                         .setSpacing(4.0f)
                         .setPadding({4.0f, 4.0f})
                         .setStretchLastChild(true)
                         .child(std::move(filterField), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                         .child(std::move(hierarchyScroll), ui::boxSlot().fill())
                         .release());
}

void EditorHierarchyTab::onAttached()
{
    bindLayerDelegates();
    rebuildTree();
    pullSelectionFromLayer();
}

void EditorHierarchyTab::onDetached()
{
    unbindLayerDelegates();
}

void EditorHierarchyTab::bindLayerDelegates()
{
    unbindLayerDelegates();
    if (!_layer) {
        return;
    }
    _selectionHandle = _layer->onSelectionChanged.addLambda(this, [this]() {
        pullSelectionFromLayer();
    });
    _hierarchyHandle = _layer->onHierarchyChanged.addLambda(this, [this]() {
        rebuildTree();
    });
}

void EditorHierarchyTab::unbindLayerDelegates()
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

void EditorHierarchyTab::rebuildTree()
{
    if (!_roots || !_layer) {
        return;
    }
    Scene* scene = _layer->getHierarchyScene();
    std::vector<UITreeView::FNode> roots;
    if (scene) {
        if (Node* root = scene->getRootNode()) {
            for (Node* child : root->getChildren()) {
                roots.push_back(buildHierarchyNode(child));
            }
        }
        if (!scene->getWidgetEntries().empty()) {
            UITreeView::FNode uiRoot;
            uiRoot.id    = "ui-root";
            uiRoot.label = "Game UI";
            for (const auto& entry : scene->getWidgetEntries()) {
                uiRoot.children.push_back(UITreeView::FNode{
                    .id    = std::format("ui:{}", entry.entryId),
                    .label = entry.entryId,
                });
            }
            roots.push_back(std::move(uiRoot));
        }
    }
    _roots->replace(std::move(roots));
}

void EditorHierarchyTab::pullSelectionFromLayer()
{
    if (!_layer || !_selection) {
        return;
    }

    std::vector<std::string> ids;
    if (!_layer->getSelectedWidgetEntryId().empty()) {
        ids.push_back(std::format("ui:{}", _layer->getSelectedWidgetEntryId()));
    }
    else {
        ids.reserve(_layer->getSelections().size());
        for (Entity* entity : _layer->getSelections()) {
            if (!entity || !entity->isValid()) {
                continue;
            }
            uint64_t uuid = 0;
            if (auto* id = entity->getComponent<IDComponent>()) {
                uuid = id->_id.value;
            }
            if (uuid != 0) {
                ids.push_back(editorHierarchyEntityIdKey(uuid));
            }
        }
    }
    std::string primary = ids.empty() ? std::string{} : ids.front();
    _selection->replace(std::move(ids), std::move(primary));
}

void EditorHierarchyTab::openContextMenu(const glm::vec2& logicalPoint)
{
    WidgetTree* tree = getTree();
    if (!tree || !_actions) {
        return;
    }
    auto menu = UIMenu::create({
        UIMenu::FItem::fromAction(*_actions, "selection.createEmpty"),
        UIMenu::FItem::separator(),
        UIMenu::FItem::fromAction(*_actions, "selection.duplicate"),
        UIMenu::FItem::fromAction(*_actions, "selection.delete"),
    });
    menu->openAt(*tree, logicalPoint);
}

} // namespace ya
