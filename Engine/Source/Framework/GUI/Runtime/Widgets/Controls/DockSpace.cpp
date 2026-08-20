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

std::pair<bool, std::string> rejectForExtent(bool bCorner, const Rect2D& rect, const glm::vec2& local)
{
    if (bCorner) {
        if (rect.extent.x < kSplitMinExtent * 2.0f && rect.extent.y < kSplitMinExtent * 2.0f) {
            return {true, "corner split requires both width and height >= 240"};
        }
        if (rect.extent.x < kSplitMinExtent * 2.0f) {
            return {true, "corner split requires width >= 240"};
        }
        if (rect.extent.y < kSplitMinExtent * 2.0f) {
            return {true, "corner split requires height >= 240"};
        }
        return {false, {}};
    }
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

void UIDockSpace::rebuildProjection()
{
    if (!getTree()) {
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
    addDetachedChild(materializeNode(*_model.root()));
    markLayoutDirty();
    markPaintDirty();
}

void UIDockSpace::rebuildLeaf(DockNodeId leafId)
{
    const FDockNode* leaf = _model.findNode(leafId);
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
        auto it = _panels.find(panelId);
        if (it != _panels.end()) {
            view->bar->addTab(it->second.name);
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
        const FDockNode* currentLeaf = _model.findNode(leafId);
        FLeafView* currentView = leafViewForLeaf(leafId);
        if (!currentLeaf || !currentView || !currentView->content || index < 0 || index >= static_cast<int>(currentLeaf->panelIds.size())) {
            return;
        }
        const DockPanelId panelId = currentLeaf->panelIds[static_cast<size_t>(index)];
        _model.selectPanel(panelId);
        if (WidgetTree* tree = getTree()) {
            auto contentChildren = currentView->content->getChildrenInPaintOrder();
            for (UIElement* child : contentChildren) {
                if (child && child->participatesInLayout()) {
                    tree->detach(*child);
                    break;
                }
            }
        }
        auto panelIt = _panels.find(panelId);
        if (panelIt != _panels.end()) {
            currentView->content->addDetachedChild(panelIt->second.widget);
        }
    };

    if (selectedIndex >= 0) {
        view->bar->syncSelectedTab(selectedIndex);
        DockPanelId selectedPanel = leaf->panelIds[static_cast<size_t>(selectedIndex)];
        auto it = _panels.find(selectedPanel);
        if (it != _panels.end()) {
            view->content->addDetachedChild(it->second.widget);
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
            if (_model.setSplitRatio(splitId, ratio)) {
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
        const FDockNode* currentLeaf = _model.findNode(leafId);
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
    if (_preview->bCorner && _preview->emptyLeafRect.extent.x > 0.0f) {
        // Ghost the persistent empty leaf the compound split will leave, so
        // the user sees the operation keeps a second droppable zone.
        builder.addRectOutline(_preview->emptyLeafRect, {0.55f, 0.58f, 0.64f, 0.55f}, 1.0f);
    }
}

const std::string& UIDockSpace::getDropPreviewDisabledReason() const
{
    static const std::string empty;
    return _preview ? _preview->disabledReason : empty;
}

void UIDockSpace::addPanel(const std::string& name, std::shared_ptr<UIElement> widget)
{
    const DockPanelId panelId = _nextPanelId++;
    if (!_model.registerPanel({.id = panelId, .stableKey = name, .title = name}) ||
        !_model.addPanel(panelId)) {
        YA_CORE_WARN("UIDockSpace '{}': rejected duplicate or invalid panel '{}'", _name, name);
        return;
    }
    _panels.emplace(panelId, FPanel{panelId, name, std::move(widget)});
    if (getTree() && !getChildren().empty()) {
        rebuildLeaf(_model.root()->id);
    }
}

std::optional<UIDockSpace::FDropPreview> UIDockSpace::resolveDropPreview(const glm::vec2& logicalPoint, DockPanelId panelId) const
{
    if (panelId == kInvalidDockPanelId) {
        return std::nullopt;
    }
    const FDockNode* sourceLeaf = _model.findLeafForPanel(panelId);
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
    if (sourceLeaf->id == targetView->leafId) {
        // Dragging a panel's own tab onto its own leaf is meaningless (it is
        // already docked there). Show no dock affordance at all.
        return FDropPreview{
            targetView->leafId,
            panelId,
            EDockCorner::NorthWest,
            EDockCardinalSide::West,
            Rect2D{},
            Rect2D{},
            false,
            false,
            true,
            "cannot dock a panel onto its own leaf",
        };
    }

    const bool bOverTabBar = targetView->bar && pointInRect(logicalPoint, targetView->bar->_layoutRect);

    const Rect2D rect = targetView->root->_layoutRect;
    if (rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec2 local = (logicalPoint - rect.pos) / rect.extent;
    if (bOverTabBar) {
        return FDropPreview{
            targetView->leafId,
            panelId,
            EDockCorner::NorthWest,
            EDockCardinalSide::West,
            targetView->bar->_layoutRect,
            Rect2D{},
            false,
            true,
            false,
            {}}
        ;
    }
    const bool bCornerNW = local.x <= 0.25f && local.y <= 0.25f;
    const bool bCornerNE = local.x >= 0.75f && local.y <= 0.25f;
    const bool bCornerSW = local.x <= 0.25f && local.y >= 0.75f;
    const bool bCornerSE = local.x >= 0.75f && local.y >= 0.75f;
    const bool bMerge = local.x > 0.25f && local.x < 0.75f && local.y > 0.25f && local.y < 0.75f;
    const bool bCorner = bCornerNW || bCornerNE || bCornerSW || bCornerSE;
    EDockCardinalSide side = EDockCardinalSide::West;
    EDockCorner corner = EDockCorner::NorthWest;
    if (bCornerNW) corner = EDockCorner::NorthWest;
    else if (bCornerNE) corner = EDockCorner::NorthEast;
    else if (bCornerSW) corner = EDockCorner::SouthWest;
    else if (bCornerSE) corner = EDockCorner::SouthEast;
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

    auto [bDisabled, reason] = rejectForExtent(bCorner, rect, local);

    Rect2D previewRect = rect;
    if (bCorner) {
        const float halfX = rect.extent.x * 0.25f;
        const float halfY = rect.extent.y * 0.25f;
        switch (corner) {
        case EDockCorner::NorthWest: previewRect = Rect2D{glm::vec2{rect.pos.x, rect.pos.y}, glm::vec2{halfX, halfY}}; break;
        case EDockCorner::NorthEast: previewRect = Rect2D{glm::vec2{rect.pos.x + rect.extent.x - halfX, rect.pos.y}, glm::vec2{halfX, halfY}}; break;
        case EDockCorner::SouthWest: previewRect = Rect2D{glm::vec2{rect.pos.x, rect.pos.y + rect.extent.y - halfY}, glm::vec2{halfX, halfY}}; break;
        case EDockCorner::SouthEast: previewRect = Rect2D{glm::vec2{rect.pos.x + rect.extent.x - halfX, rect.pos.y + rect.extent.y - halfY}, glm::vec2{halfX, halfY}}; break;
        }
    }
    else if (!bMerge) {
        const float stripX = rect.extent.x * 0.30f;
        const float stripY = rect.extent.y * 0.30f;
        switch (side) {
        case EDockCardinalSide::West: previewRect = Rect2D{glm::vec2{rect.pos.x, rect.pos.y}, glm::vec2{stripX, rect.extent.y}}; break;
        case EDockCardinalSide::East: previewRect = Rect2D{glm::vec2{rect.pos.x + rect.extent.x - stripX, rect.pos.y}, glm::vec2{stripX, rect.extent.y}}; break;
        case EDockCardinalSide::North: previewRect = Rect2D{glm::vec2{rect.pos.x, rect.pos.y}, glm::vec2{rect.extent.x, stripY}}; break;
        case EDockCardinalSide::South: previewRect = Rect2D{glm::vec2{rect.pos.x, rect.pos.y + rect.extent.y - stripY}, glm::vec2{rect.extent.x, stripY}}; break;
        }
    }

    Rect2D emptyLeafRect;
    if (bCorner) {
        // Compound plan leaves a persistent empty leaf next to the new panel:
        // same left/right half as the corner, on the opposite vertical side.
        const bool bLeft = corner == EDockCorner::NorthWest || corner == EDockCorner::SouthWest;
        const bool bTop  = corner == EDockCorner::NorthWest || corner == EDockCorner::NorthEast;
        const float halfX = rect.extent.x * 0.25f;
        const float halfY = rect.extent.y * 0.25f;
        const float x = bLeft ? rect.pos.x : rect.pos.x + rect.extent.x - halfX;
        const float y = bTop ? rect.pos.y + halfY : rect.pos.y;
        emptyLeafRect = Rect2D{glm::vec2{x, y}, glm::vec2{halfX, halfY}};
    }

    return FDropPreview{targetView->leafId, panelId, corner, side, previewRect, emptyLeafRect,
                        bCorner, bMerge, bDisabled, std::move(reason)};
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

    const FDockNode* sourceLeaf = _model.findLeafForPanel(panelId);
    if (!sourceLeaf) {
        return;
    }

    bool bChanged = false;
    if (preview->bMerge) {
        if (sourceLeaf->id != preview->targetLeafId) {
            bChanged = _model.movePanel(panelId, preview->targetLeafId, SIZE_MAX, true);
        }
    }
    else if (preview->bCorner) {
        bChanged = _model.splitLeafCorner(preview->targetLeafId, preview->corner, panelId);
    }
    else {
        bChanged = _model.splitLeaf(preview->targetLeafId, preview->side, panelId);
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
