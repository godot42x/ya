#include "GUI/Widgets/Controls/DockSpace.h"


#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "Render/Resources/FontManager.h"
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
constexpr float kChooserBlock = 28.0f;
constexpr float kChooserGap = 8.0f;

struct FChooserRects
{
    Rect2D center{};
    Rect2D left{};
    Rect2D right{};
    Rect2D top{};
    Rect2D bottom{};
};

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

FChooserRects makeChooserRects(const Rect2D& rect)
{
    const glm::vec2 center = rect.pos + rect.extent * 0.5f;
    const float block = kChooserBlock;
    const float gap = kChooserGap;
    return {
        Rect2D{glm::vec2{center.x - block * 0.5f, center.y - block * 0.5f}, glm::vec2{block, block}},
        Rect2D{glm::vec2{center.x - block * 1.5f - gap, center.y - block * 0.5f}, glm::vec2{block, block}},
        Rect2D{glm::vec2{center.x + block * 0.5f + gap, center.y - block * 0.5f}, glm::vec2{block, block}},
        Rect2D{glm::vec2{center.x - block * 0.5f, center.y - block * 1.5f - gap}, glm::vec2{block, block}},
        Rect2D{glm::vec2{center.x - block * 0.5f, center.y + block * 0.5f + gap}, glm::vec2{block, block}},
    };
}

struct FDropChooserOverlay final : UIElement
{
    explicit FDropChooserOverlay(UIDockSpace* owner)
        : UIElement("DockChooserOverlay")
        , _owner(owner)
    {
        _hitFilter = EWidgetHitFilter::Stop;
        setVisibility(EWidgetVisibility::SelfHitTestInvisible);
        _bSelfClip = false;
    }

    [[nodiscard]] bool hitTestSelf(const glm::vec2&) const override { return false; }
    void paintSelf(UIFrameBuilder& builder) override
    {
        if (_owner) {
            _owner->paintDropPreviewOverlay(builder);
        }
    }

    UIDockSpace* _owner = nullptr;
};
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
    }
    syncPreviewOverlay();
    markPaintDirty();
}

void UIDockSpace::syncPreviewOverlay()
{
    WidgetTree* tree = getTree();
    if (_preview && tree) {
        if (!_previewOverlay) {
            _previewOverlay = std::make_shared<FDropChooserOverlay>(this);
        }
        if (!_previewOverlay->isAttached()) {
            tree->attachToLayer(WidgetTree::ELayer::DragIme, _previewOverlay);
        }
        return;
    }
    if (_previewOverlay && tree && _previewOverlay->isAttached()) {
        tree->detach(*_previewOverlay);
    }
    _previewOverlay.reset();
}

void UIDockSpace::paintDropPreviewOverlay(UIFrameBuilder& builder) const
{
    if (!_preview || _preview->bDisabled) {
        return;
    }
    FDockSpaceStyle style;
    if (!_styleKey.empty()) {
        if (const FDockSpaceStyle* themed = resolveThemeStyle<FDockSpaceStyle>(*this, _styleKey)) {
            style = *themed;
        }
    }

    const FLeafView* targetView = leafViewForLeaf(_preview->targetLeafId);
    if (!targetView || !targetView->root || !targetView->bar) {
        return;
    }

    const Rect2D barRect = targetView->bar->_layoutRect;
    const Rect2D leafRect = targetView->root->_layoutRect;
    const FChooserRects chooser = _preview->bHeaderZone ? makeChooserRects(barRect) : makeChooserRects(leafRect);

    const auto drawChoice = [&](const Rect2D& rect, bool bActive)
    {
        const FBrush fillBrush = bActive ? style.dropPreviewMergeColor : style.dropPreviewColor;
        builder.addBrush(rect, fillBrush);
        builder.addRectOutline(rect, style.dropPreviewOutlineColor, bActive ? 2.0f : 1.0f);
    };

    builder.addRectOutline(_preview->bHeaderZone ? barRect : leafRect, style.dropPreviewOutlineColor, 1.0f);
    if (_preview->bHeaderZone) {
        drawChoice(chooser.center, true);
        return;
    }

    drawChoice(chooser.center, _preview->bMerge && !_preview->bDisabled);
    drawChoice(chooser.left, _preview->side == EDockCardinalSide::West && !_preview->bMerge && !_preview->bDisabled);
    drawChoice(chooser.right, _preview->side == EDockCardinalSide::East && !_preview->bMerge && !_preview->bDisabled);
    drawChoice(chooser.top, _preview->side == EDockCardinalSide::North && !_preview->bMerge && !_preview->bDisabled);
    drawChoice(chooser.bottom, _preview->side == EDockCardinalSide::South && !_preview->bMerge && !_preview->bDisabled);
}

void UIDockSpace::setWorkspace(std::shared_ptr<UIDockWorkspace> ws)
{
    _ws = std::move(ws);
    if (_ws) {
        _ws->setOnDockUpdated([this]()
        {
            if (getTree()) {
                rebuildProjection();
            }
        });
    }
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
    leaf->setClipChildren(true);
    auto bar = std::make_shared<UITabBar>(std::format("DockTabBar{}", node.id));
    bar->_bDraggableTabs = true;
    bar->_styleKey = "tab.dock";
    bar->setClipChildren(true);
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
                syncPreviewOverlay();
                markPaintDirty();
            };
            observer.onTargetChanged = [this](std::string_view, std::string_view)
            {
                clearPreview();
            };
            observer.onFinished = [this, panelId](EDragFinishResult result, const glm::vec2& logicalPoint, std::string_view)
            {
                clearPreview();
                if (result == EDragFinishResult::NoTarget && _ws && _ws->bAllowTearOff && _ws->bAllowFloating) {
                    // Tear-off: pull the panel out of the dock tree into a
                    // floating window at a clear default position (not the
                    // arbitrary release point, which may sit in chrome). Detach
                    // the panel from this leaf first (reproject), then let the
                    // host mount the floating window.
                    _ws->tearOffPanel(panelId, {180.0f, 140.0f}, {320.0f, 240.0f});
                    rebuildProjection();
                    _ws->fireFloatingUpdated();
                }
            };
            tree->beginDrag(this, std::string(kDockPanelPayload) + std::to_string(panelId), label, std::move(observer));
        }
    };
    leaf->addDetachedChild(bar);

    auto body = std::make_shared<UIPanel>(std::format("DockBody{}", node.id));
    body->_styleKey = "panel.surface";
    leaf->addDetachedChild(body);

    auto content = std::make_shared<UIContainer>(std::format("DockContent{}", node.id));
    content->_anchorMin = {0.0f, 0.0f};
    content->_anchorMax = {1.0f, 1.0f};
    content->setPosition({0.0f, 0.0f});
    content->setSize({0.0f, 0.0f});
    content->setPadding({12.0f, 12.0f});
    leaf->setStretchLastChild(true);
    body->addDetachedChild(content);
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
    // Theme resolution (style-system Phase 3): the dock canvas base from
    // FDockSpaceStyle (darker than any panel so the tabs/content read as
    // stacked surfaces instead of floating rectangles). Absent key/theme →
    // default-constructed style is the fallback (no magic literals here).
    FDockSpaceStyle style;
    if (!_styleKey.empty()) {
        if (const FDockSpaceStyle* themed = resolveThemeStyle<FDockSpaceStyle>(*this, _styleKey)) {
            style = *themed;
        }
    }
    builder.addBrush(_layoutRect, style.canvasColor);
}

void UIDockSpace::paintChildren(UIFrameBuilder& builder)
{
    UIElement::paintChildren(builder);
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
    const bool bFloating = _ws->isPanelFloating(panelId);
    if (!sourceLeaf && !bFloating) {
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
    const bool bOverTabBar = targetView->bar && pointInRect(logicalPoint, targetView->bar->_layoutRect);
    const Rect2D barRect = targetView->bar->_layoutRect;
    const Rect2D leafRect = targetView->root->_layoutRect;

    if (bOverTabBar) {
        const Rect2D centerRect = makeChooserRects(barRect).center;
        return FDropPreview{targetView->leafId, panelId, EDockCardinalSide::West, centerRect,
                            "Dock Center", true, true, false, {}};
    }

    if (!pointInRect(logicalPoint, leafRect)) {
        return std::nullopt;
    }

    const FChooserRects chooser = makeChooserRects(leafRect);
    const bool inCenter = pointInRect(logicalPoint, chooser.center);
    const bool inLeft = pointInRect(logicalPoint, chooser.left);
    const bool inRight = pointInRect(logicalPoint, chooser.right);
    const bool inTop = pointInRect(logicalPoint, chooser.top);
    const bool inBottom = pointInRect(logicalPoint, chooser.bottom);

    if (!inCenter && !inLeft && !inRight && !inTop && !inBottom) {
        return std::nullopt;
    }
    if (sourceLeaf && sourceLeaf->id == targetView->leafId && bOverTabBar) {
        return std::nullopt;
    }
    if (inCenter) {
        return FDropPreview{targetView->leafId, panelId, EDockCardinalSide::West, chooser.center,
                            "Dock Center", true, false, false, {}};
    }
    if (inLeft) {
        return FDropPreview{targetView->leafId, panelId, EDockCardinalSide::West, chooser.left,
                            "Left", false, false, false, {}};
    }
    if (inRight) {
        return FDropPreview{targetView->leafId, panelId, EDockCardinalSide::East, chooser.right,
                            "Right", false, false, false, {}};
    }
    if (inTop) {
        return FDropPreview{targetView->leafId, panelId, EDockCardinalSide::North, chooser.top,
                            "Top", false, false, false, {}};
    }
    return FDropPreview{targetView->leafId, panelId, EDockCardinalSide::South, chooser.bottom,
                        "Bottom", false, false, false, {}};
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
    const bool bWasFloating = _ws->isPanelFloating(panelId);
    if (!sourceLeaf && !bWasFloating) {
        return;
    }

    bool bChanged = false;
    if (preview->bMerge) {
        if (sourceLeaf && sourceLeaf->id != preview->targetLeafId) {
            bChanged = _ws->dockModel().movePanel(panelId, preview->targetLeafId, SIZE_MAX, true);
        }
        else if (!sourceLeaf) {
            // Re-dock a floating panel by merging it into the target leaf.
            bChanged = _ws->dockModel().addPanel(panelId, preview->targetLeafId);
        }
    }
    else {
        bChanged = _ws->dockModel().splitLeaf(preview->targetLeafId, preview->side, panelId);
    }

    if (bChanged) {
        if (bWasFloating) {
            _ws->endFloatingForPanel(panelId);
        }
        rebuildProjection();
    }
}

void UIDockSpace::setDropHighlight(bool bHighlight)
{
    if (!bHighlight) {
        clearPreview();
    }
}

void UIDockSpace::updateDropHover(const std::string& payload, const glm::vec2& logicalPoint)
{
    // Point-sensitive drop preview: canAcceptDrop only answers yes/no, so the
    // tree feeds the CURRENT pointer here on every move of an active drag.
    // Resolve the merge/split preview at the pointer and mark paint-dirty so
    // the highlight follows the drag (dock regression: the preview never
    // rendered because it was only computed into a local in canAcceptDrop).
    DockPanelId panelId = kInvalidDockPanelId;
    auto preview = parsePanelPayload(payload, panelId) ? resolveDropPreview(logicalPoint, panelId)
                                                       : std::nullopt;
    _preview = std::move(preview);
    syncPreviewOverlay();
    markPaintDirty();
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
