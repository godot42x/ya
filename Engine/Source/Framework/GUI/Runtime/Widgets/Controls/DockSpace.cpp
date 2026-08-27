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
using glm::vec2;

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
    if (!payload.starts_with(prefix)) {
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
    const float     block  = kChooserBlock;
    const float     gap    = kChooserGap;
    return {
        .center = Rect2D{
            {vec2{center.x - block * 0.5f, center.y - block * 0.5f}},
            vec2{block, block},
        },
        .left = Rect2D{
            {glm::vec2{center.x - block * 1.5f - gap, center.y - block * 0.5f}},
            glm::vec2{block, block},
        },
        .right = Rect2D{
            {glm::vec2{center.x + block * 0.5f + gap, center.y - block * 0.5f}},
            glm::vec2{block, block},
        },
        .top = Rect2D{
            {glm::vec2{center.x - block * 0.5f, center.y - block * 1.5f - gap}},
            glm::vec2{block, block},
        },
        .bottom = Rect2D{
            {glm::vec2{center.x - block * 0.5f, center.y + block * 0.5f + gap}},
            glm::vec2{block, block},
        },
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

} // namespace

struct FDockSpaceDropTargetBehavior final : public UIDropTargetBehavior
{
    FDockSpaceDropTargetBehavior()
    {
        acceptPayload = [](UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
        {
            auto* dock = dynamic_cast<UIDockSpace*>(&owner);
            if (!dock) {
                return false;
            }
            DockPanelId panelId = kInvalidDockPanelId;
            auto preview = dock->parsePanelPayload(payload, panelId) ? dock->resolveDropPreview(logicalPoint, panelId) : std::nullopt;
            return preview.has_value() && !preview->bDisabled && !preview->bChooser;
        };
        handleDroppedPayload = [](UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
        {
            auto* dock = dynamic_cast<UIDockSpace*>(&owner);
            if (!dock) {
                return;
            }
            DockPanelId panelId = kInvalidDockPanelId;
            if (!dock->parsePanelPayload(payload, panelId)) {
                dock->clearPreview();
                return;
            }
            auto preview = dock->resolveDropPreview(logicalPoint, panelId);
            dock->clearPreview();
            if (!preview || preview->bDisabled || preview->bChooser) {
                return;
            }

            const FDockNode* sourceLeaf = dock->_ws->dockModel().findLeafForPanel(panelId);
            const bool bWasFloating = dock->_ws->isPanelFloating(panelId);
            if (!sourceLeaf && !bWasFloating) {
                return;
            }

            bool bChanged = false;
            if (preview->targetFloatingId != kInvalidFloatingWindowId) {
                bChanged = dock->_ws->addPanelToFloating(preview->targetFloatingId, panelId);
            }
            else if (preview->bMerge) {
                if (sourceLeaf && sourceLeaf->id != preview->targetLeafId) {
                    bChanged = dock->_ws->dockModel().movePanel(panelId, preview->targetLeafId, SIZE_MAX, true);
                }
                else if (!sourceLeaf) {
                    bChanged = dock->_ws->dockModel().addPanel(panelId, preview->targetLeafId);
                }
            }
            else {
                bChanged = dock->_ws->dockModel().splitLeaf(preview->targetLeafId, preview->side, panelId);
            }

            if (bChanged) {
                if (bWasFloating && preview->targetFloatingId == kInvalidFloatingWindowId) {
                    dock->_ws->endFloatingForPanel(panelId);
                }
                dock->rebuildProjection();
            }
        };
        setHighlightState = [](UIElement& owner, bool bHighlight)
        {
            if (!bHighlight) {
                if (auto* dock = dynamic_cast<UIDockSpace*>(&owner)) {
                    dock->clearPreview();
                }
            }
        };
        updateHoverState = [](UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
        {
            auto* dock = dynamic_cast<UIDockSpace*>(&owner);
            if (!dock) {
                return;
            }
            DockPanelId panelId = kInvalidDockPanelId;
            auto        preview = dock->parsePanelPayload(payload, panelId)
                                    ? dock->resolveDropPreview(logicalPoint, panelId)
                                    : std::nullopt;
            dock->_preview = std::move(preview);
            dock->syncPreviewOverlay();
            dock->markPaintDirty();
        };
    }

    void onDetached(UIElement& owner) override
    {
        if (auto* dock = dynamic_cast<UIDockSpace*>(&owner)) {
            dock->clearPreview();
        }
        UIDropTargetBehavior::onDetached(owner);
    }
};

UIDockSpace::UIDockSpace(std::string name)
    : UIElement(std::move(name), "dock")
{
    _hitFilter = EWidgetHitFilter::Stop;
    addBehavior(std::make_shared<FDockSpaceDropTargetBehavior>());
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

void UIDockSpace::clearDropPreview()
{
    if (!_preview) {
        return;
    }
    _preview.reset();
    syncPreviewOverlay();
    markPaintDirty();
}

void UIDockSpace::setDropPreview(const FDropPreview& preview)
{
    _preview = preview;
    syncPreviewOverlay();
    markPaintDirty();
}

void UIDockSpace::syncPreviewOverlay()
{
    WidgetTree* tree = getTree();
    if (_preview && tree) {
        if (!_previewOverlay) {
            _previewOverlay = std::make_shared<FDropChooserOverlay>(this);
            _previewOverlay->_anchorMin = {0.0f, 0.0f};
            _previewOverlay->_anchorMax = {1.0f, 1.0f};
            _previewOverlay->setPosition({0.0f, 0.0f});
            _previewOverlay->setSize({0.0f, 0.0f});
        }
        if (!_previewOverlay->isAttached()) {
            tree->attachToLayer(WidgetTree::ELayer::DragIme, _previewOverlay);
        }
        _previewOverlay->markPaintDirty();
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
    const FDockSpaceStyle style = resolveWidgetStyle<FDockSpaceStyle>(*this, _authoredStyle);

    // Floating-window target: highlight the whole window as a "merge as tab"
    // drop zone (targetLeafId is kInvalidDockNodeId for these previews).
    if (_preview->targetFloatingId != kInvalidFloatingWindowId) {
        builder.addBrush(_preview->rect, style.dropPreviewMergeColor);
        builder.addRectOutline(_preview->rect, style.dropPreviewOutlineColor, 2.0f);
        return;
    }

    const FLeafView* targetView = leafViewForLeaf(_preview->targetLeafId);
    if (!targetView || !targetView->root || !targetView->bar) {
        return;
    }

    const Rect2D leafRect = targetView->root->_layoutRect;
    const FChooserRects chooser = makeChooserRects(leafRect);

    const auto drawChoice = [&](const Rect2D& rect, bool bActive)
    {
        const FBrush fillBrush = bActive ? style.dropPreviewMergeColor : style.dropPreviewColor;
        builder.addBrush(rect, fillBrush);
        builder.addRectOutline(rect, style.dropPreviewOutlineColor, bActive ? 2.0f : 1.0f);
    };

    builder.addRectOutline(leafRect, style.dropPreviewOutlineColor, 1.0f);

    // Chooser mode: pointer is over the leaf but not over a specific block yet.
    // Render all blocks dimmed so the user can see the drop targets.
    if (_preview->bChooser) {
        drawChoice(chooser.center, false);
        drawChoice(chooser.left, false);
        drawChoice(chooser.right, false);
        drawChoice(chooser.top, false);
        drawChoice(chooser.bottom, false);
        return;
    }

    // Active drop mode: only the selected block is highlighted, so the
    // preview of where the panel will land is unambiguous.
    if (_preview->bMerge) {
        // ImGui-style tab merge: highlight the tab bar strip, not the body
        // center block, so the user sees the tab becomes the drop target.
        if (_preview->bTabBar) {
            builder.addBrush(_preview->rect, style.dropPreviewMergeColor);
            builder.addRectOutline(_preview->rect, style.dropPreviewOutlineColor, 2.0f);
            return;
        }
        drawChoice(chooser.center, !_preview->bDisabled);
        return;
    }
    if (_preview->side == EDockCardinalSide::West) {
        drawChoice(chooser.left, !_preview->bDisabled);
    }
    else if (_preview->side == EDockCardinalSide::East) {
        drawChoice(chooser.right, !_preview->bDisabled);
    }
    else if (_preview->side == EDockCardinalSide::North) {
        drawChoice(chooser.top, !_preview->bDisabled);
    }
    else if (_preview->side == EDockCardinalSide::South) {
        drawChoice(chooser.bottom, !_preview->bDisabled);
    }
}

void UIDockSpace::setWorkspace(std::shared_ptr<UIDockWorkspace> ws)
{
    _ws = std::move(ws);
    if (_ws) {
        _ws->setDockSpace(this);
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
    addDetachedChild(materializeNode(*_ws->dockModel().getRootNode()));
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
                    // floating window positioned at the actual drop point so the
                    // window appears where the user released the tab. Detach the
                    // panel from this leaf first (reproject), then let the host
                    // mount the floating window.
                    const glm::vec2 size{320.0f, 240.0f};
                    _ws->tearOffPanel(panelId, logicalPoint, size);
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
    const FDockSpaceStyle style = resolveWidgetStyle<FDockSpaceStyle>(*this, _authoredStyle);
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
        rebuildLeaf(_ws->dockModel().getRootNode()->id);
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
    // Floating windows are drop targets too: hovering over another floating
    // window merges the dragged panel in as a new tab. It takes precedence over
    // the dock leaves beneath, since floating windows render above the content.
    // The panel's own floating window is excluded (you cannot merge into itself).
    const UIDockWorkspace::FFloatingWindow* sourceFloating = bFloating ? _ws->findFloatingByPanel(panelId) : nullptr;
    for (const UIDockWorkspace::FFloatingWindow& fw : _ws->floatingWindows()) {
        if (sourceFloating && sourceFloating->id == fw.id) {
            continue;
        }
        Rect2D fwRect;
        fwRect.pos = fw.pos;
        fwRect.extent = fw.size;
        if (pointInRect(logicalPoint, fwRect)) {
            return FDropPreview{
                .targetLeafId     = kInvalidDockNodeId,
                .targetFloatingId = fw.id,
                .panelId          = panelId,
                .side             = EDockCardinalSide::West,
                .rect             = fwRect,
                .prompt           = "Add Tab",
                .bMerge           = true,
                .bChooser         = false,
                .bDisabled        = false,
                .disabledReason   = {},
            };
        }
    }
    // Focus leaf: the leaf under the pointer, or — when the pointer is over
    // empty space / the tab bar — the dragged panel's own leaf. Keeping the
    // source leaf as focus means the chooser stays visible for the entire drag
    // instead of vanishing the moment the cursor leaves a leaf's content rect.
    const FLeafView* focus = nullptr;
    for (const auto& [leafId, view] : _leafViews) {
        (void)leafId;
        if (view.root && pointInRect(logicalPoint, view.root->_layoutRect)) {
            focus = &view;
            break;
        }
    }
    if (!focus && sourceLeaf) {
        focus = leafViewForLeaf(sourceLeaf->id);
    }
    if (!focus || !focus->root) {
        return std::nullopt;
    }
    const Rect2D leafRect = focus->root->_layoutRect;

    // ImGui-style "drop on a tab": hovering a leaf's tab bar merges the dragged
    // panel into that tab group. This is a real dock (bMerge, not bChooser), so
    // the tab bar highlights and a drop there adds the panel as a new tab.
    if (focus->bar && pointInRect(logicalPoint, focus->bar->_layoutRect)) {
        return FDropPreview{
            .targetLeafId     = focus->leafId,
            .targetFloatingId = kInvalidFloatingWindowId,
            .panelId          = panelId,
            .side             = EDockCardinalSide::West,
            .rect             = focus->bar->_layoutRect,
            .prompt           = "Add Tab",
            .bMerge           = true,
            .bTabBar          = true,
            .bChooser         = false,
            .bDisabled        = false,
            .disabledReason   = {},
        };
    }

    // Pointer outside this leaf's content (over empty space): render the
    // chooser dimmed, but never dock here — a drop in these regions becomes a
    // floating window. See canAcceptDrop() / onFinished().
    if (!pointInRect(logicalPoint, leafRect)) {
        return FDropPreview{
            .targetLeafId   = focus->leafId,
            .panelId        = panelId,
            .side           = EDockCardinalSide::West,
            .rect           = leafRect,
            .prompt         = {},
            .bMerge         = false,
            .bChooser       = true,
            .bDisabled      = false,
            .disabledReason = {},
        };
    }

    const FChooserRects chooser = makeChooserRects(leafRect);
    const bool inCenter = pointInRect(logicalPoint, chooser.center);
    const bool inLeft = pointInRect(logicalPoint, chooser.left);
    const bool inRight = pointInRect(logicalPoint, chooser.right);
    const bool inTop = pointInRect(logicalPoint, chooser.top);
    const bool inBottom = pointInRect(logicalPoint, chooser.bottom);

    // Chooser mode: pointer is over the leaf content but not over a specific
    // block yet. Render all blocks dimmed so the drop targets are visible.
    if (!inCenter && !inLeft && !inRight && !inTop && !inBottom) {
        return FDropPreview{
            .targetLeafId   = focus->leafId,
            .panelId        = panelId,
            .side           = EDockCardinalSide::West,
            .rect           = leafRect,
            .prompt         = {},
            .bMerge         = false,
            .bChooser       = true,
            .bDisabled      = false,
            .disabledReason = {},
        };
    }
    if (inCenter) {
        return FDropPreview{
            .targetLeafId   = focus->leafId,
            .panelId        = panelId,
            .side           = EDockCardinalSide::West,
            .rect           = chooser.center,
            .prompt         = "Dock Center",
            .bMerge         = true,
            .bDisabled      = false,
            .disabledReason = {},
        };
    }
    if (inLeft) {
        return FDropPreview{
            .targetLeafId   = focus->leafId,
            .panelId        = panelId,
            .side           = EDockCardinalSide::West,
            .rect           = chooser.left,
            .prompt         = "Left",
            .bMerge         = false,
            .bDisabled      = false,
            .disabledReason = {},
        };
    }
    if (inRight) {
        return FDropPreview{
            .targetLeafId   = focus->leafId,
            .panelId        = panelId,
            .side           = EDockCardinalSide::East,
            .rect           = chooser.right,
            .prompt         = "Right",
            .bMerge         = false,
            .bDisabled      = false,
            .disabledReason = {},
        };
    }
    if (inTop) {
        return FDropPreview{
            .targetLeafId   = focus->leafId,
            .panelId        = panelId,
            .side           = EDockCardinalSide::North,
            .rect           = chooser.top,
            .prompt         = "Top",
            .bMerge         = false,
            .bDisabled      = false,
            .disabledReason = {},
        };
    }
    return FDropPreview{
        .targetLeafId   = focus->leafId,
        .panelId        = panelId,
        .side           = EDockCardinalSide::South,
        .rect           = chooser.bottom,
        .prompt         = "Bottom",
        .bMerge         = false,
        .bDisabled      = false,
        .disabledReason = {},
    };
}

bool UIDockSpace::parsePanelPayload(const std::string& payload, DockPanelId& panelId) const
{
    panelId = parsePanelId(payload);
    return panelId != kInvalidDockPanelId;
}

std::optional<UIDockSpace::FDropPreview> UIDockSpace::dropPreviewFor(const std::string& payload,
                                                                     const glm::vec2& logicalPoint) const
{
    DockPanelId panelId = kInvalidDockPanelId;
    if (!parsePanelPayload(payload, panelId)) {
        return std::nullopt;
    }
    return resolveDropPreview(logicalPoint, panelId);
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
