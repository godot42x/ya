#include "GameEditor/UI/Tabs/EditorHierarchyTab.h"
#include "GameEditor/UI/Ops/EditorHierarchyOps.h"
#include "GameEditor/UI/Ops/EditorCreateMenu.h"
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
#include "Core/Os/Os.h"
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
                        uint64_t    uuid = 0;
                        std::string entryId;
                        if (parseEditorHierarchyEntityIdKey(id, uuid)) {
                            Scene* scene = _layer->getHierarchyScene();
                            Entity* entity = scene ? scene->getEntityByUUID(uuid) : nullptr;
                            if (entity) {
                                // Ctrl/Cmd adds or removes, Shift extends over the
                                // visible rows; plain replaces. (Widget entries
                                // below stay single-select.)
                                const uint32_t mod    = Os::queryKeyModState();
                                const bool     bMulti = (mod & EKeyMod::Ctrl) != 0 || (mod & EKeyMod::Gui) != 0;
                                const bool     bRange = (mod & EKeyMod::Shift) != 0;
                                applyEntitySelectionGesture(entity, bMulti, bRange);
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
                            _layer->markSceneDirty();
                            _layer->setSelectedEntity(moved);
                        }
                    })
                    .setOnContextMenu([this](const std::string& targetId, const glm::vec2& logicalPoint) {
                        openContextMenu(targetId, logicalPoint);
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
    if (_treeView) {
        // Paint the whole selection, not just the primary row.
        _treeView->setSelectedIds(std::unordered_set<std::string>(ids.begin(), ids.end()));
    }
    _selection->replace(std::move(ids), std::move(primary));
}

void EditorHierarchyTab::collectVisibleEntityUuids(const std::vector<UITreeView::FNode>& nodes,
                                                   std::vector<uint64_t>&                   out) const
{
    for (const UITreeView::FNode& node : nodes) {
        uint64_t uuid = 0;
        if (parseEditorHierarchyEntityIdKey(node.id, uuid)) {
            out.push_back(uuid);
        }
        // Collapsed subtrees are not on screen, so a Shift range must not
        // reach into them.
        if (!node.children.empty() && (!_treeView || _treeView->isExpanded(node.id))) {
            collectVisibleEntityUuids(node.children, out);
        }
    }
}

void EditorHierarchyTab::collectVisibleEntityUuids(std::vector<uint64_t>& out) const
{
    out.clear();
    if (!_roots) {
        return;
    }
    const size_t count = _roots->size();
    std::vector<UITreeView::FNode> roots;
    roots.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        roots.push_back(_roots->get(i));
    }
    collectVisibleEntityUuids(roots, out);
}

void EditorHierarchyTab::applyEntitySelectionGesture(Entity* entity, bool bMulti, bool bRange)
{
    Scene* scene = _layer ? _layer->getHierarchyScene() : nullptr;
    if (!scene || !entity) {
        return;
    }

    std::vector<Entity*> selection = _layer->getSelections();

    if (bRange && _rangeAnchorUuid != 0) {
        std::vector<uint64_t> visible;
        collectVisibleEntityUuids(visible);
        const auto anchorIt = std::find(visible.begin(), visible.end(), _rangeAnchorUuid);
        const auto clickIt  = std::find(visible.begin(), visible.end(), entity->getComponent<IDComponent>()
                                                                                 ? entity->getComponent<IDComponent>()->_id.value
                                                                                 : 0);
        if (anchorIt != visible.end() && clickIt != visible.end()) {
            auto [rangeBegin, rangeEnd] = std::minmax(anchorIt, clickIt);
            std::vector<Entity*> range;
            range.reserve(static_cast<size_t>(std::distance(rangeBegin, rangeEnd)) + 1);
            for (auto it = rangeBegin; it != std::next(rangeEnd); ++it) {
                if (Entity* inRange = scene->getEntityByUUID(*it)) {
                    range.push_back(inRange);
                }
            }
            _layer->setSelections(range, entity);
            return;
        }
    }

    if (bMulti) {
        const auto it = std::find(selection.begin(), selection.end(), entity);
        if (it != selection.end()) {
            selection.erase(it);
            _layer->setSelections(selection, selection.empty() ? nullptr : selection.front());
        }
        else {
            selection.push_back(entity);
            _layer->setSelections(selection, entity);
        }
        _rangeAnchorUuid = entity->getComponent<IDComponent>() ? entity->getComponent<IDComponent>()->_id.value : 0;
        return;
    }

    _layer->setSelectedEntity(entity);
    _rangeAnchorUuid = entity->getComponent<IDComponent>() ? entity->getComponent<IDComponent>()->_id.value : 0;
}

void EditorHierarchyTab::groupSelectionUnderNewFolder()
{
    Scene* scene = _layer ? _layer->getHierarchyScene() : nullptr;
    if (!scene) {
        return;
    }
    std::vector<Entity*> selection = _layer->getSelections();
    if (selection.empty()) {
        return;
    }

    // The folder is an empty node under the primary's parent: the same shape
    // the create menu makes, so it serializes and expands like any node.
    Node* primaryNode = scene->getNodeByEntity(selection.front());
    Node* parent      = primaryNode ? primaryNode->getParent() : scene->getRootNode();
    Node* group       = scene->createNode3D("Group", parent);
    if (!group) {
        YA_CORE_WARN("EditorHierarchyTab: could not create the group node");
        return;
    }

    const std::string groupId = editorHierarchyEntityIdKey(group->getEntity()->getComponent<IDComponent>()->_id.value);
    for (Entity* entity : selection) {
        if (!entity || !entity->isValid() || scene->getNodeByEntity(entity) == group) {
            continue;
        }
        (void)moveEditorHierarchyEntity(*scene, editorHierarchyEntityIdKey(entity->getComponent<IDComponent>()->_id.value), groupId, /*dropMode=*/1);
    }
    _layer->notifyHierarchyChanged();
    _layer->markSceneDirty();
    _layer->setSelectedEntity(group->getEntity());
}

void EditorHierarchyTab::openContextMenu(const std::string& targetId, const glm::vec2& logicalPoint)
{
    WidgetTree* tree = getTree();
    if (!tree || !_actions || !_layer) {
        return;
    }
    EditorLayer& layer = *_layer;

    auto gameUIItems = [&layer]() {
        std::vector<UIMenu::FItem> items;
        items.push_back({
            .label  = "New Game UI",
            .action = [&layer]() { layer.createAndMountGameUI(); },
        });
        items.push_back({
            .label    = "Mount Open Game UI",
            .action   = [&layer]() { layer.mountOpenGameUI(); },
            .bEnabled = layer.canMountOpenGameUI(),
        });
        return items;
    };

    std::vector<UIMenu::FItem> items;
    std::string                entryId;
    uint64_t                   uuid = 0;
    if (parseWidgetEntryKey(targetId, entryId)) {
        items.push_back({
            .label  = "Open in UI Designer",
            .action = [&layer, entryId]() { layer.openGameUIEntry(entryId); },
        });
        items.push_back(UIMenu::FItem::separator());
        items.push_back({
            .label  = "Unmount from Scene",
            .action = [&layer, entryId]() { layer.unmountGameUIEntry(entryId); },
        });
    }
    else if (targetId == "ui-root") {
        items = gameUIItems();
    }
    else if (parseEditorHierarchyEntityIdKey(targetId, uuid)) {
        Node* parent = nullptr;
        if (Scene* scene = layer.getHierarchyScene()) {
            if (Entity* entity = scene->getEntityByUUID(uuid)) {
                parent = scene->getNodeByEntity(entity);
            }
        }
        items = makeEditorCreateMenuItems(layer, parent);
        items.push_back(UIMenu::FItem::separator());
        items.push_back(UIMenu::FItem::fromAction(*_actions, "selection.duplicate"));
        items.push_back(UIMenu::FItem::fromAction(*_actions, "selection.delete"));
        items.push_back(UIMenu::FItem::separator());
        items.push_back({
            .label    = "Group Selection",
            .action   = [this]() { groupSelectionUnderNewFolder(); },
            .bEnabled = !layer.getSelections().empty(),
        });
    }
    else {
        items = makeEditorCreateMenuItems(layer);
        items.push_back(UIMenu::FItem::separator());
        for (UIMenu::FItem& item : gameUIItems()) {
            items.push_back(std::move(item));
        }
    }

    auto menu = UIMenu::create(std::move(items));
    menu->openAt(*tree, logicalPoint);
}

} // namespace ya
