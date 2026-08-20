#include "GUI/Widgets/Controls/DockSpace.h"


#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <cmath>

namespace ya
{
namespace
{
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
}

UIDockSpace::UIDockSpace(std::string name)
    : UIElement(std::move(name))
{
    _hitFilter = EWidgetHitFilter::Stop;

    // Three-zone layout: split(left | split(center | right)).
    _groups[0].zoneName = kZoneLeft;
    _groups[1].zoneName = kZoneCenter;
    _groups[2].zoneName = kZoneRight;
}

void UIDockSpace::ensureModelLayout()
{
    if (_zoneLeafIds[0] != kInvalidDockNodeId && _zoneLeafIds[1] != kInvalidDockNodeId && _zoneLeafIds[2] != kInvalidDockNodeId) {
        return;
    }
    if (_model.leafIds().size() >= 3) {
        refreshZoneLeafIds();
        return;
    }
    const DockNodeId rootId = _model.root()->id;
    if (!_model.splitEmptyLeaf(rootId, EDockCardinalSide::East, 0.22f) || _model.leafIds().size() != 2) {
        YA_CORE_ERROR("UIDockSpace '{}': failed to create default dock model", _name);
        return;
    }
    const auto firstLeaves = _model.leafIds();
    if (!_model.splitEmptyLeaf(firstLeaves[1], EDockCardinalSide::West, 0.78f) || _model.leafIds().size() != 3) {
        YA_CORE_ERROR("UIDockSpace '{}': failed to create center dock leaf", _name);
        return;
    }
    const auto leaves = _model.leafIds();
    for (size_t i = 0; i < 3; ++i) {
        _zoneLeafIds[i] = leaves[i];
    }
}

void UIDockSpace::refreshZoneLeafIds()
{
    const auto leaves = _model.leafIds();
    for (size_t i = 0; i < 3; ++i) {
        _zoneLeafIds[i] = i < leaves.size() ? leaves[i] : kInvalidDockNodeId;
    }
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
    refreshZoneLeafIds();
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

    const int zone = node.id == _zoneLeafIds[0] ? 0 : (node.id == _zoneLeafIds[1] ? 1 : (node.id == _zoneLeafIds[2] ? 2 : -1));
    auto leaf = std::make_shared<UIContainer>(zone == 0 ? kZoneLeft : zone == 1 ? kZoneCenter : zone == 2 ? kZoneRight : std::format("DockLeaf{}", node.id));
    leaf->setDirection(EWidgetBoxLayout::Vertical);
    leaf->setSpacing(0.0f);
    auto bar = std::make_shared<UITabBar>(zone >= 0 ? std::format("DockTabBar{}", zone) : std::format("DockTabBar{}", node.id));
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
    ensureModelLayout();

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
    (void)builder;
}

void UIDockSpace::paintChildren(UIFrameBuilder& builder)
{
    UIElement::paintChildren(builder);
    if (!_preview) {
        return;
    }
    const glm::vec4 color = _preview->bMerge
                                ? glm::vec4{0.24f, 0.72f, 0.42f, 0.20f}
                                : glm::vec4{0.24f, 0.46f, 0.82f, 0.20f};
    builder.addSprite(_preview->rect, color, nullptr);
    builder.addRectOutline(_preview->rect, {0.24f, 0.46f, 0.82f, 1.0f}, 2.0f);
}

void UIDockSpace::addPanel(const std::string& name, std::shared_ptr<UIElement> widget, int zone)
{
    if (zone < 0 || zone > 2) {
        zone = 1;
    }
    ensureModelLayout();
    const DockPanelId panelId = _nextPanelId++;
    if (!_model.registerPanel({.id = panelId, .stableKey = name, .title = name}) ||
        !_model.addPanel(panelId, _zoneLeafIds[zone])) {
        YA_CORE_WARN("UIDockSpace '{}': rejected duplicate or invalid panel '{}'", _name, name);
        return;
    }
    _panels.emplace(panelId, FPanel{panelId, name, std::move(widget)});
    if (getTree() && !getChildren().empty()) {
        rebuildLeaf(_zoneLeafIds[zone]);
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

    const Rect2D rect = targetView->root->_layoutRect;
    if (rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec2 local = (logicalPoint - rect.pos) / rect.extent;
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

    return FDropPreview{targetView->leafId, panelId, corner, side, previewRect, bCorner, bMerge};
}

bool UIDockSpace::parsePanelPayload(const std::string& payload, DockPanelId& panelId) const
{
    panelId = parsePanelId(payload);
    return panelId != kInvalidDockPanelId;
}

bool UIDockSpace::canAcceptDrop(const std::string& payload, const glm::vec2& logicalPoint)
{
    DockPanelId panelId = kInvalidDockPanelId;
    return parsePanelPayload(payload, panelId) && resolveDropPreview(logicalPoint, panelId).has_value();
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

    const FDockNode* sourceLeaf = _model.findLeafForPanel(panelId);
    if (!sourceLeaf) {
        return;
    }

    bool bChanged = false;
    if (preview->bMerge) {
        if (sourceLeaf->id != preview->targetLeafId) {
            bChanged = _model.movePanel(panelId, preview->targetLeafId, SIZE_MAX, false);
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
