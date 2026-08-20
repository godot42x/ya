#include "GUI/Widgets/Controls/DockSpace.h"


#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ya
{
namespace
{
constexpr float kSplitMinExtent = 120.0f;

bool pointInRect(const glm::vec2& point, const Rect2D& rect)
{
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.extent.y;
}

DockPanelId parsePanelId(const std::string& payload)
{
    const std::string prefix = UIDockSpace::kDockPanelPayload;
    if (payload.rfind(prefix, 0) != 0) {
        return kInvalidDockPanelId;
    }
    return static_cast<DockPanelId>(std::strtoull(payload.c_str() + prefix.size(), nullptr, 10));
}

std::pair<bool, std::string> rejectForExtent(const Rect2D& rect, const glm::vec2& local)
{
    if (rect.extent.x < kSplitMinExtent * 2.0f && rect.extent.y < kSplitMinExtent * 2.0f) {
        return {true, "cardinal split requires width or height >= 240"};
    }
    const bool bHorizontal = std::abs(local.x - 0.5f) > std::abs(local.y - 0.5f);
    if (bHorizontal) {
        if (rect.extent.x < kSplitMinExtent * 2.0f) {
            return {true, "cardinal split requires width >= 240"};
        }
    }
    else if (rect.extent.y < kSplitMinExtent * 2.0f) {
        return {true, "cardinal split requires height >= 240"};
    }
    return {false, {}};
}
}

UIDockSpace::UIDockSpace(std::string name)
    : UIElement(std::move(name))
{
    _hitFilter = EWidgetHitFilter::Stop;
}

UIDockSpace::FLeafView* UIDockSpace::leafViewForLeaf(DockNodeId leafId)
{
    auto it = _leafViews.find(leafId);
    return it == _leafViews.end() ? nullptr : &it->second;
}

const UIDockSpace::FLeafView* UIDockSpace::leafViewForLeaf(DockNodeId leafId) const
{
    auto it = _leafViews.find(leafId);
    return it == _leafViews.end() ? nullptr : &it->second;
}

void UIDockSpace::clearPreview()
{
    if (_preview) {
        _preview.reset();
        markPaintDirty();
    }
}

void UIDockSpace::setWorkspace(std::shared_ptr<UIDockWorkspace> ws)
{
    _ws = std::move(ws);
    if (getTree() && !getChildren().empty()) {
        rebuildProjection();
    }
}

void UIDockSpace::rebuildProjection()
{
    if (!getTree() || !_ws) {
        return;
    }
    clearPreview();
    auto children = getChildrenInPaintOrder();
    for (UIElement* child : children) {
        if (child && child->participatesInLayout()) {
            getTree()->detach(*child);
        }
    }
    _leafViews.clear();
    addDetachedChild(materializeNode(*_ws->dockModel().root()));
    markLayoutDirty();
    markPaintDirty();
}

void UIDockSpace::rebuildLeaf(DockNodeId leafId)
{
    if (!_ws) {
        return;
    }
    const FDockNode* leaf = _ws->dockModel().findNode(leafId);
    FLeafView* view = leafViewForLeaf(leafId);
    if (!leaf || !view || !view->bar || !view->content) {
        return;
    }

    if (WidgetTree* tree = getTree()) {
        auto contentChildren = view->content->getChildrenInPaintOrder();
        for (UIElement* child : contentChildren) {
            if (child && child->participatesInLayout()) {
                tree->detach(*child);
                break;
            }
        }
    }

    const int tabCount = static_cast<int>(view->bar->getChildren().size());
    for (int i = tabCount - 1; i >= 0; --i) {
        view->bar->removeTab(i);
    }

    for (DockPanelId panelId : leaf->panelIds) {
        if (const UIDockWorkspace::FPanel* fp = _ws->findPanel(panelId)) {
            view->bar->addTab(fp->name);
        }
    }

    int selectedIndex = -1;
    if (leaf->selectedPanel != kInvalidDockPanelId) {
        auto it = std::find(leaf->panelIds.begin(), leaf->panelIds.end(), leaf->selectedPanel);
        if (it != leaf->panelIds.end()) {
            selectedIndex = static_cast<int>(std::distance(leaf->panelIds.begin(), it));
        }
    }
    if (selectedIndex < 0 && !leaf->panelIds.empty()) {
        selectedIndex = 0;
    }

    view->bar->_onTabSelected = [this, leafId](int index)
    {
        const FDockNode* currentLeaf = _ws->dockModel().findNode(leafId);
        FLeafView* currentView = leafViewForLeaf(leafId);
        if (!currentLeaf || !currentView || !currentView->content || index < 0 || index >= static_cast<int>(currentLeaf->panelIds.size())) {
            return;
        }
        const DockPanelId panelId = currentLeaf->panelIds[static_cast<size_t>(index)];
        _ws->dockModel().selectPanel(panelId);
        if (WidgetTree* tree = getTree()) {
            auto contentChildren = currentView->content->getChildrenInPaintOrder();
            for (UIElement* child : contentChildren) {
                if (child && child->participatesInLayout()) {
                    tree->detach(*child);
                    break;
                }
            }
        }
        if (const UIDockWorkspace::FPanel* fp = _ws->findPanel(panelId)) {
            currentView->content->addDetachedChild(fp->widget);
        }
    };

    if (selectedIndex >= 0) {
        view->bar->syncSelectedTab(selectedIndex);
        DockPanelId selectedPanel = leaf->panelIds[static_cast<size_t>(selectedIndex)];
        if (const UIDockWorkspace::FPanel* fp = _ws->findPanel(selectedPanel)) {
            view->content->addDetachedChild(fp->widget);
        }
    }
    markLayoutDirty();
}

std::shared_ptr<UIElement> UIDockSpace::materializeNode(const FDockNode& node)
{
    if (node.kind == EDockNodeKind::Split) {
        auto split = std::make_shared<UISplitPane>(std::format("DockSplit{}", node.id));
        split->setOrientation(node.orientation == EDockSplitOrientation::Vertical
                                  ? ESplitOrientation::Vertical : ESplitOrientation::Horizontal);
        split->setSplitRatio(node.ratio);
        split->setMinFirstExtent(node.minExtent[0]);
        split->setMinSecondExtent(node.minExtent[1]);
        const DockNodeId splitId = node.id;
        split->setSplitRatioChangedCallback([this, splitId](float ratio)
        {
            if (_ws->dockModel().setSplitRatio(splitId, ratio)) {
                markLayoutDirty();
                markPaintDirty();
            }
        });
        if (node.child[0]) split->addDetachedChild(materializeNode(*node.child[0]));
        if (node.child[1]) split->addDetachedChild(materializeNode(*node.child[1]));
        return split;
    }

    auto leaf = std::make_shared<UIContainer>(std::format("DockLeaf{}", node.id));
    leaf->setDirection(EWidgetBoxLayout::Vertical);
    leaf->setSpacing(0.0f);
    auto bar = std::make_shared<UITabBar>(std::format("DockTabBar{}", node.id));
    bar->_bDraggableTabs = true;
    bar->_emptyPlaceholder = std::format("{} (drop tabs here)", leaf->_name);
    bar->_onTabDragBegin = [this, leafId = node.id](int index, const std::string& label)
    {
        const FDockNode* currentLeaf = _ws->dockModel().findNode(leafId);
        if (!currentLeaf || index < 0 || index >= static_cast<int>(currentLeaf->panelIds.size())) {
            return;
        }
        const DockPanelId panelId = currentLeaf->panelIds[static_cast<size_t>(index)];
        if (WidgetTree* tree = getTree()) {
            DragSessionObserver observer;
            observer.onMove = [this, panelId](const std::string&, const glm::vec2& logicalPoint, std::string_view)
            {
                _preview = resolveDropPreview(logicalPoint, panelId);
                markPaintDirty();
            };
            observer.onTargetChanged = [this](std::string_view, std::string_view)
            {
                clearPreview();
            };
            observer.onFinished = [this](EDragFinishResult, const glm::vec2&, std::string_view)
            {
                clearPreview();
            };
            tree->beginDrag(this, std::string(kDockPanelPayload) + std::to_string(panelId), label, std::move(observer));
        }
    };
    leaf->addDetachedChild(bar);

    auto content = std::make_shared<UIContainer>(std::format("DockContent{}", node.id));
    leaf->setStretchLastChild(true);
    leaf->addDetachedChild(content);
    content->setStretchLastChild(true);

    _leafViews[node.id] = {node.id, leaf.get(), bar.get(), content.get()};
    rebuildLeaf(node.id);
    return leaf;
}

void UIDockSpace::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIDockSpace::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);

    if (getChildren().empty() && getTree()) {
        rebuildProjection();
    }

    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            child->layoutAssigned(rect);
            break;
        }
    }
}

void UIDockSpace::paintSelf(UIFrameBuilder& builder)
{
    // Dock canvas base: darker than any panel so the tabs/content read as
    // stacked surfaces instead of floating rectangles.
    builder.addSprite(_layoutRect, {0.075f, 0.082f, 0.10f, 1.0f}, nullptr);
}

void UIDockSpace::paintChildren(UIFrameBuilder& builder)
{
    UIElement::paintChildren(builder);
    if (!_preview || _preview->bDisabled) {
        return;
    }
    const glm::vec4 color = _preview->bMerge
                                ? glm::vec4{0.26f, 0.76f, 0.46f, 0.28f}
                                : glm::vec4{0.28f, 0.52f, 0.90f, 0.28f};
    builder.addSprite(_preview->rect, color, nullptr);
    builder.addRectOutline(_preview->rect, {0.34f, 0.60f, 0.96f, 1.0f}, 1.5f);
}

const std::string& UIDockSpace::getDropPreviewDisabledReason() const
{
    static const std::string empty;
    return _preview ? _preview->disabledReason : empty;
}

void UIDockSpace::addPanel(const std::string& name, std::shared_ptr<UIElement> widget)
{
    if (!_ws) {
        return;
    }
    const DockPanelId panelId = _ws->addPanel(name, std::move(widget));
    if (panelId == kInvalidDockPanelId) {
        YA_CORE_WARN("UIDockSpace '{}': rejected duplicate or invalid panel '{}'", _name, name);
        return;
    }
    if (getTree() && !getChildren().empty()) {
        rebuildLeaf(_ws->dockModel().root()->id);
    }
}

std::optional<UIDockSpace::FDropPreview> UIDockSpace::resolveDropPreview(const glm::vec2& logicalPoint, DockPanelId panelId) const
{
    if (!_ws || panelId == kInvalidDockPanelId) {
        return std::nullopt;
    }
    const FDockNode* sourceLeaf = _ws->dockModel().findLeafForPanel(panelId);
    if (!sourceLeaf) {
        return std::nullopt;
    }
    const FLeafView* targetView = nullptr;
    for (const auto& [leafId, view] : _leafViews) {
        (void)leafId;
        if (view.root && pointInRect(logicalPoint, view.root->_layoutRect)) {
            targetView = &view;
            break;
        }
    }
    if (!targetView || !targetView->root) {
        return std::nullopt;
    }
    const bool bSameLeaf = sourceLeaf->id == targetView->leafId;

    const bool bOverTabBar = targetView->bar && pointInRect(logicalPoint, targetView->bar->_layoutRect);

    const Rect2D rect = targetView->root->_layoutRect;
    if (rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec2 local = (logicalPoint - rect.pos) / rect.extent;
    if (bOverTabBar) {
        if (bSameLeaf) {
            return FDropPreview{targetView->leafId, panelId, EDockCardinalSide::West, Rect2D{},
                                false, true, "already docked in this leaf"};
        }
        return FDropPreview{
            targetView->leafId,
            panelId,
            EDockCardinalSide::West,
            targetView->bar->_layoutRect,
            true,
            false,
            {}}
        ;
    }
    const bool bMerge = local.x > 0.25f && local.x < 0.75f && local.y > 0.25f && local.y < 0.75f;
    EDockCardinalSide side = EDockCardinalSide::West;
    if (!bMerge) {
        const float dx = local.x - 0.5f;
        const float dy = local.y - 0.5f;
        if (std::abs(dx) > std::abs(dy)) {
            side = dx < 0.0f ? EDockCardinalSide::West : EDockCardinalSide::East;
        }
        else {
            side = dy < 0.0f ? EDockCardinalSide::North : EDockCardinalSide::South;
        }
    }

    auto [bDisabled, reason] = rejectForExtent(rect, local);
    if (bSameLeaf) {
        if (bMerge) {
            bDisabled = true;
            reason = "cannot merge a panel into its own leaf";
        }
        else if (sourceLeaf->panelIds.size() <= 1) {
            bDisabled = true;
            reason = "cannot split a single-panel leaf onto itself";
        }
    }

    Rect2D previewRect = rect;
    if (!bMerge) {
        const float stripX = rect.extent.x * 0.30f;
        const float stripY = rect.extent.y * 0.30f;
        switch (side) {
        case EDockCardinalSide::West: previewRect = Rect2D{glm::vec2{rect.pos.x, rect.pos.y}, glm::vec2{stripX, rect.extent.y}}; break;
        case EDockCardinalSide::East: previewRect = Rect2D{glm::vec2{rect.pos.x + rect.extent.x - stripX, rect.pos.y}, glm::vec2{stripX, rect.extent.y}}; break;
        case EDockCardinalSide::North: previewRect = Rect2D{glm::vec2{rect.pos.x, rect.pos.y}, glm::vec2{rect.extent.x, stripY}}; break;
        case EDockCardinalSide::South: previewRect = Rect2D{glm::vec2{rect.pos.x, rect.pos.y + rect.extent.y - stripY}, glm::vec2{rect.extent.x, stripY}}; break;
        }
    }

    return FDropPreview{targetView->leafId, panelId, side, previewRect,
                        bMerge, bDisabled, std::move(reason)};
}

bool UIDockSpace::parsePanelPayload(const std::string& payload, DockPanelId& panelId) const
{
    panelId = parsePanelId(payload);
    return panelId != kInvalidDockPanelId;
}

bool UIDockSpace::canAcceptDrop(const std::string& payload, const glm::vec2& logicalPoint)
{
    DockPanelId panelId = kInvalidDockPanelId;
    auto preview = parsePanelPayload(payload, panelId) ? resolveDropPreview(logicalPoint, panelId) : std::nullopt;
    return preview.has_value() && !preview->bDisabled;
}

void UIDockSpace::onDrop(const std::string& payload, const glm::vec2& logicalPoint)
{
    DockPanelId panelId = kInvalidDockPanelId;
    if (!parsePanelPayload(payload, panelId)) {
        clearPreview();
        return;
    }
    auto preview = resolveDropPreview(logicalPoint, panelId);
    clearPreview();
    if (!preview) {
        return;
    }
    if (preview->bDisabled) {
        return;
    }

    const FDockNode* sourceLeaf = _ws->dockModel().findLeafForPanel(panelId);
    if (!sourceLeaf) {
        return;
    }

    bool bChanged = false;
    if (preview->bMerge) {
        if (sourceLeaf->id != preview->targetLeafId) {
            bChanged = _ws->dockModel().movePanel(panelId, preview->targetLeafId, SIZE_MAX, true);
        }
    }
    else {
        bChanged = _ws->dockModel().splitLeaf(preview->targetLeafId, preview->side, panelId);
    }

    if (bChanged) {
        rebuildProjection();
    }
}

void UIDockSpace::setDropHighlight(bool bHighlight)
{
    if (!bHighlight) {
        clearPreview();
    }
}

void UIDockSpace::clearTransientInputState()
{
    clearPreview();
}

bool UIDockSpace::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    (void)event;
    (void)ctx;
    return false;
}

} // namespace ya
