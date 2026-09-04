#include "GUI/Widgets/Controls/DockSpace.h"


#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Layout/UILayout.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/Event.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace ya
{
namespace
{
using glm::vec2;

constexpr float kSplitMinExtent = 120.0f;
constexpr float kChooserBlock = 28.0f;
constexpr float kChooserGap = 8.0f;
constexpr float kHideTabBarSize = 12.0f;

struct FDockHideTabBarAffordance final : UIElement
{
    FDockHideTabBarAffordance(std::function<void()> onToggle)
        : UIElement("DockHideTabBar")
        , _onToggle(std::move(onToggle))
    {
        _hitFilter = EWidgetHitFilter::Stop;
        _zOrder    = 8;
    }

    [[nodiscard]] bool hitTestSelf(const glm::vec2& logicalPoint) const override
    {
        return isHitTestableSelf() && hitTestLayoutRect(logicalPoint);
    }

    [[nodiscard]] bool isHoverable() const override { return true; }

    void paintSelf(UIFrameBuilder& builder) override
    {
        const glm::vec4 color = _bHovered ? glm::vec4{0.78f, 0.82f, 0.90f, 1.0f}
                                          : glm::vec4{0.52f, 0.56f, 0.64f, 0.95f};
        const glm::vec2 p = _layoutRect.pos;
        const float     s = std::min(_layoutRect.extent.x, _layoutRect.extent.y);
        builder.addLine(p, {p.x + s, p.y}, color, 1.5f);
        builder.addLine(p, {p.x, p.y + s}, color, 1.5f);
        builder.addLine({p.x + s, p.y}, {p.x, p.y + s}, color, 1.5f);
    }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        const bool inside = hitTestLayoutRect(ctx.logicalPoint);
        if (event.getEventType() == EEvent::MouseMoved) {
            _bHovered = inside;
            return inside;
        }
        if (event.getEventType() == EEvent::MouseButtonPressed && inside) {
            if (_onToggle) {
                _onToggle();
            }
            return true;
        }
        return false;
    }

    void resetHoverState() override { _bHovered = false; }
    void clearTransientInputState() override { _bHovered = false; }

  private:
    std::function<void()> _onToggle;
    bool                  _bHovered = false;
};

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

template <typename TBehavior>
TBehavior* findBehavior(UIElement& owner)
{
    for (const UIBehaviorRef& behavior : owner.getBehaviors()) {
        if (auto* typed = dynamic_cast<TBehavior*>(behavior.get())) {
            return typed;
        }
    }
    return nullptr;
}

} // namespace

struct FDockSpacePanelDragBehavior final : public UIBehavior
{
    void beginPanelDrag(UIDockSpace& owner, DockPanelId panelId, std::string label)
    {
        WidgetTree* tree = owner.getTree();
        if (!tree) {
            return;
        }

        DragSessionObserver observer;
        observer.onMove = [&owner, panelId](const std::string&, const glm::vec2& logicalPoint, std::string_view)
        {
            owner._preview = owner.resolveDropPreview(logicalPoint, panelId);
            owner.syncPreviewOverlay();
            owner.markPaintDirty();
        };
        observer.onTargetChanged = [&owner](std::string_view, std::string_view)
        {
            owner.clearPreview();
        };
        observer.onFinished = [&owner, panelId](EDragFinishResult result, const glm::vec2& logicalPoint, std::string_view)
        {
            owner.clearPreview();
            if (result == EDragFinishResult::NoTarget && owner._ws && owner._ws->bAllowTearOff && owner._ws->bAllowFloating) {
                const glm::vec2 size{320.0f, 240.0f};
                owner._ws->tearOffPanel(panelId, logicalPoint, size);
                owner.rebuildProjection();
                owner._ws->fireFloatingUpdated();
                owner._ws->fireDockUpdated();
            }
        };
        tree->beginDrag(&owner, std::string(UIDockSpace::kDockPanelPayload) + std::to_string(panelId), std::move(label), std::move(observer));
    }
};

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
                dock->_ws->fireDockUpdated();
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
    installLayout(std::make_unique<UISingleChildLayout>());
    _hitFilter = EWidgetHitFilter::Stop;
    addBehavior(std::make_shared<FDockSpacePanelDragBehavior>());
    addBehavior(std::make_shared<FDockSpaceDropTargetBehavior>());
}

UIDockSpace::~UIDockSpace()
{
    // Detach from the workspace so a workspace that outlives this widget (the
    // floating host keeps it alive) never serves a dangling UIDockSpace* via
    // dockSpace(). The drag-drop completion path resolves UIDockSpace through
    // exactly this pointer, so a stale value is a use-after-free.
    if (_ws && _ws->dockSpace() == this) {
        _ws->setDockSpace(nullptr);
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
        }
        if (!_previewOverlay->isAttached()) {
            FCanvasSlotArgs fillArgs;
            fillArgs.anchorMin = {0.0f, 0.0f};
            fillArgs.anchorMax = {1.0f, 1.0f};
            const WidgetAttachment attachment =
                tree->attachToLayer(WidgetTree::ELayer::DragIme, _previewOverlay, fillArgs);
            YA_CORE_ASSERT(attachment.valid(), "UIDockSpace: failed to attach preview overlay");
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
    const FDockSpaceStyle& style = resolvedStyle();

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
    // Rebind: the old workspace must not keep pointing back at us (a floating
    // host may keep it alive long after this widget is replaced).
    if (_ws && _ws != ws && _ws->dockSpace() == this) {
        _ws->setDockSpace(nullptr);
    }
    _ws = std::move(ws);
    if (_ws) {
        _ws->setDockSpace(this);
        // Weak self: the workspace may fire dock-updated after this widget is
        // destroyed (it is kept alive by the floating host), so the callback
        // must never dereference a stale 'this'.
        std::weak_ptr<UIDockSpace> weakSelf =
            std::static_pointer_cast<UIDockSpace>(shared_from_this());
        _ws->setOnDockUpdated([weakSelf]()
        {
            if (auto self = weakSelf.lock()) {
                if (self->getTree()) {
                    self->rebuildProjection();
                }
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
        _ws->fireDockUpdated();
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
            currentView->content->addDetachedChild(fp->widget, [](UIElement&, UISlot& edge)
            {
                if (auto* slot = edge.as<UIBoxSlot>()) {
                    FBoxSlotArgs args;
                    args.sizeRule = EUIBoxSlotSizeRule::Fill;
                    slot->apply(args);
                }
            });
        }
    };

    const auto attachPanelContent = [view](const UIElementRef& panel)
    {
        if (!view || !view->content || !panel) {
            return;
        }
        // DockContent is a path-A box host. The selected panel's extent is
        // therefore expressed on the parent-owned box slot, never through
        // child-authored canvas anchors. This keeps dock roots portable
        // regardless of how the panel was authored.
        view->content->addDetachedChild(panel, [](UIElement&, UISlot& edge)
        {
            if (auto* slot = edge.as<UIBoxSlot>()) {
                FBoxSlotArgs args;
                args.sizeRule = EUIBoxSlotSizeRule::Fill;
                slot->apply(args);
            }
        });
    };

    if (selectedIndex >= 0) {
        view->bar->syncSelectedTab(selectedIndex);
        DockPanelId selectedPanel = leaf->panelIds[static_cast<size_t>(selectedIndex)];
        if (const UIDockWorkspace::FPanel* fp = _ws->findPanel(selectedPanel)) {
            attachPanelContent(fp->widget);
        }
    }
    view->bar->setVisibility(leaf->bHideTabBar ? EWidgetVisibility::Collapsed : EWidgetVisibility::Visible);
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
                _ws->fireDockUpdated();
            }
        });
        if (node.child[0]) split->addDetachedChild(materializeNode(*node.child[0]));
        if (node.child[1]) split->addDetachedChild(materializeNode(*node.child[1]));
        return split;
    }

    auto leaf = std::make_shared<UIOverlay>(std::format("DockLeaf{}", node.id));
    auto chrome = std::make_shared<UIContainer>(std::format("DockLeafChrome{}", node.id));
    chrome->setDirection(EWidgetBoxLayout::Vertical);
    chrome->setSpacing(0.0f);
    chrome->setClipChildren(true);
    auto bar = std::make_shared<UITabBar>(std::format("DockTabBar{}", node.id));
    bar->_bDraggableTabs = true;
    bar->_styleKey = "tab.dock";
    bar->setClipChildren(true);
    bar->setPadding({2.0f, 1.0f});
    bar->setSpacing(1.0f);
    bar->_emptyPlaceholder = std::format("{} (drop tabs here)", leaf->_name);
    bar->_onTabDragBegin = [this, leafId = node.id](int index, const std::string& label)
    {
        const FDockNode* currentLeaf = _ws->dockModel().findNode(leafId);
        if (!currentLeaf || index < 0 || index >= static_cast<int>(currentLeaf->panelIds.size())) {
            return;
        }
        const DockPanelId panelId = currentLeaf->panelIds[static_cast<size_t>(index)];
        if (auto* behavior = findBehavior<FDockSpacePanelDragBehavior>(*this)) {
            behavior->beginPanelDrag(*this, panelId, label);
        }
    };
    chrome->addDetachedChild(bar);

    auto body = std::make_shared<UIPanel>(std::format("DockBody{}", node.id));
    body->_styleKey = "panel.surface";
    chrome->addDetachedChild(body);

    auto content = std::make_shared<UIContainer>(std::format("DockContent{}", node.id));
    content->setPadding({0.0f, 0.0f});
    chrome->setStretchLastChild(true);
    body->addDetachedChild(content);
    // Stretch intent lives on the parent->child edge: the body is a canvas host,
    // so the fill is expressed through its slot rather than on the container.
    if (UISlot* edge = body->getSlotForChild(*content); edge && edge->as<UICanvasSlot>()) {
        auto* slot = edge->as<UICanvasSlot>();
        FCanvasSlotArgs fillArgs;
        fillArgs.anchorMin = {0.0f, 0.0f};
        fillArgs.anchorMax = {1.0f, 1.0f};
        slot->apply(fillArgs);
    }
    content->setStretchLastChild(true);

    leaf->addDetachedChild(chrome, [](UIElement&, UISlot& slot)
    {
        if (auto* overlay = slot.as<UIOverlaySlot>()) {
            overlay->apply(FOverlaySlotArgs{
                .hAlign = EUIOverlayAlignment::Fill,
                .vAlign = EUIOverlayAlignment::Fill,
            });
        }
    });
    auto hideBar = std::make_shared<FDockHideTabBarAffordance>([this, leafId = node.id]()
    {
        const FDockNode* current = _ws->dockModel().findNode(leafId);
        if (!current || current->kind != EDockNodeKind::Leaf) {
            return;
        }
        if (_ws->dockModel().setHideTabBar(leafId, !current->bHideTabBar)) {
            _ws->fireDockUpdated();
        }
    });
    leaf->addDetachedChild(hideBar, [](UIElement&, UISlot& slot)
    {
        if (auto* overlay = slot.as<UIOverlaySlot>()) {
            overlay->apply(FOverlaySlotArgs{
                .hAlign        = EUIOverlayAlignment::Start,
                .vAlign        = EUIOverlayAlignment::Start,
                .preferredSize = {kHideTabBarSize, kHideTabBarSize},
            });
        }
    });

    _leafViews[node.id] = {node.id, leaf.get(), bar.get(), content.get()};
    rebuildLeaf(node.id);
    return leaf;
}

void UIDockSpace::layout(const Rect2D& parentRect)
{
    layoutAssigned(parentRect);
}

void UIDockSpace::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);

    if (getChildren().empty() && getTree()) {
        rebuildProjection();
    }

    if (UILayout* layout = getLayout()) {
        layout->arrange(*this, _layoutRect);
    }
}

void UIDockSpace::paintSelf(UIFrameBuilder& builder)
{
    // Theme resolution (style-system Phase 3): the dock canvas base from
    // FDockSpaceStyle (darker than any panel so the tabs/content read as
    // stacked surfaces instead of floating rectangles). Absent key/theme →
    // default-constructed style is the fallback (no magic literals here).
    const FDockSpaceStyle& style = resolvedStyle();
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
    // floating window. The actual tear-off happens in the dock-panel drag
    // session's finish observer, not in a widget-level drop override.
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
