#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"

#include "GUI/Widgets/Controls/DockSpace/DockHideTabBarAffordance.h"
#include "GUI/Widgets/Controls/DockSpace/DockTabStack.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Layout/UILayout.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/Event.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

namespace ya
{
namespace
{
using glm::vec2;

bool stackHidesInnerTabWell(const FDockNode& leaf)
{
    return leaf.bHideTabBar || leaf.leafRole == EDockLeafRole::Page;
}

constexpr float kSplitMinExtent = 120.0f;
constexpr float kChooserBlock = 28.0f;
constexpr float kChooserGap = 8.0f;

void unlinkWidgetFromVisualParent(UIElement& widget, WidgetTree* tree)
{
    if (WidgetTree* attached = widget.getTree()) {
        attached->detach(widget);
        return;
    }
    if (tree) {
        tree->detach(widget);
    }
}

void fillDockPanelBoxSlot(UIElement&, UISlot& edge)
{
    FBoxSlotArgs args;
    args.sizeRule = EUIBoxSlotSizeRule::Fill;
    edge.applyArgs(args);
}

void graftPanelIntoHost(UIElement& host, const UIElementRef& panel, WidgetTree* fallbackTree)
{
    if (!panel || panel.get() == &host) {
        return;
    }
    if (panel->getParent() == &host) {
        host.initializeChildSlot(*panel, fillDockPanelBoxSlot);
        return;
    }

    WidgetTree* hostTree = host.getTree() ? host.getTree() : fallbackTree;
    if (hostTree && hostTree->contains(host) && panel->isAttached() && panel->getTree() == hostTree) {
        hostTree->reparent(host, panel);
        host.initializeChildSlot(*panel, fillDockPanelBoxSlot);
        return;
    }

    unlinkWidgetFromVisualParent(*panel, hostTree);
    if (hostTree && hostTree->contains(host)) {
        hostTree->attach(host, panel, fillDockPanelBoxSlot);
        return;
    }
    host.addDetachedChild(panel, fillDockPanelBoxSlot);
}

/// A leaf shows exactly one panel. Box DockContent defaults to horizontal Fill,
/// so leftover siblings share the node 50/50 instead of hiding.
void showPanelInContent(UIElement& host, const UIElementRef& panel, WidgetTree* tree)
{
    const std::vector<UIElementRef> mounted = host.getChildren();
    for (const UIElementRef& child : mounted) {
        if (child && child != panel) {
            unlinkWidgetFromVisualParent(*child, tree);
        }
    }
    graftPanelIntoHost(host, panel, tree);
}

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

DockPanelId panelIdFrom(const UIDragDropOperation& operation)
{
    const auto* dockOp = operation.as<FDockPanelDragDropOp>();
    return dockOp ? dockOp->panelId : kInvalidDockPanelId;
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

UIDockSpace* enclosingDockSpace(UIElement* widget)
{
    for (UIElement* node = widget; node != nullptr; node = node->getParent()) {
        if (auto* dock = dynamic_cast<UIDockSpace*>(node)) {
            return dock;
        }
    }
    return nullptr;
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

FDockPanelDragDropOp::~FDockPanelDragDropOp() = default;

/// Panel drag session for UIDockSpace; a friend so it can drive the owner's
/// drag state.
struct FDockSpacePanelDrag
{
    static void beginPanelDrag(UIDockSpace& owner, DockPanelId panelId, std::string label)
    {
        WidgetTree* tree = owner.getTree();
        if (!tree) {
            return;
        }

        DragSessionObserver observer;
        observer.onMove = [&owner, panelId](const UIDragDropOperation&, const glm::vec2& logicalPoint, std::string_view)
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
            if (result == EDragFinishResult::Dropped || result == EDragFinishResult::Cancelled) {
                return;
            }
            if (logicalPoint.x < -10000.0f || logicalPoint.y < -10000.0f) {
                return;
            }
            if (result == EDragFinishResult::NoTarget && owner._context && owner._context->bAllowTearOff && owner._context->bAllowFloating) {
                const glm::vec2 size{320.0f, 240.0f};
                bool bHandled = false;
                if (owner._context->realizeNoTargetTearOff) {
                    bHandled = owner._context->realizeNoTargetTearOff(panelId, logicalPoint, size);
                }
                if (!bHandled) {
                    owner._context->tearOffPanel(panelId, logicalPoint, size);
                }
                owner.syncProjection(EDockProjectionSync::Structure);
                owner._context->fireFloatingUpdated();
                owner._context->notifyDockLayoutListeners();
            }
        };
        tree->beginDrag(&owner, FDockPanelDragDropOp::make(panelId, std::move(label), owner._context.get()), std::move(observer));
    }
};

UIDockSpace::UIDockSpace(std::string name)
    : UIElement(std::move(name), "dock")
{
    installLayout(std::make_unique<UISingleChildLayout>());
    _hitFilter = EWidgetHitFilter::Stop;
    installDockDropTarget(*this);
}

UIDockSpace::~UIDockSpace()
{
    // Detach from the context so a context that outlives this widget (the
    // floating host keeps it alive) never serves a dangling UIDockSpace* via
    // dockSpace(). The drag-drop completion path resolves UIDockSpace through
    // exactly this pointer, so a stale value is a use-after-free.
    if (_context && _context->dockSpace() == this) {
        _context->setDockSpace(nullptr);
    }
}

UIDockSpace::FDockStackView* UIDockSpace::stackViewFor(DockNodeId leafId)
{
    auto it = _stackViews.find(leafId);
    return it == _stackViews.end() ? nullptr : &it->second;
}

const UIDockSpace::FDockStackView* UIDockSpace::stackViewFor(DockNodeId leafId) const
{
    auto it = _stackViews.find(leafId);
    return it == _stackViews.end() ? nullptr : &it->second;
}

size_t UIDockSpace::tabInsertIndexAt(DockNodeId leafId, const glm::vec2& logicalPoint) const
{
    const FDockStackView* view = stackViewFor(leafId);
    if (!view || !view->well) {
        return SIZE_MAX;
    }
    size_t index = 0;
    for (const UIElementRef& child : view->well->getChildren()) {
        if (!child) {
            continue;
        }
        const Rect2D& rect = child->_layoutRect;
        if (logicalPoint.x < rect.pos.x + rect.extent.x * 0.5f) {
            return index;
        }
        ++index;
    }
    return index;
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
    // drop zone.
    if (_preview->target.kind == EDockDropTargetKind::FloatingTabWell) {
        builder.addBrush(_preview->rect, style.dropPreviewMergeColor);
        builder.addRectOutline(_preview->rect, style.dropPreviewOutlineColor, 2.0f);
        return;
    }

    const FDockStackView* targetView = stackViewFor(_preview->target.stackId);
    if (!targetView || !targetView->root || !targetView->well) {
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

    // Always draw every chooser block. Hover/active only brightens the
    // selected target — otherwise blocks are invisible until the pointer
    // is already on top of them.
    drawChoice(chooser.center, false);
    drawChoice(chooser.left, false);
    drawChoice(chooser.right, false);
    drawChoice(chooser.top, false);
    drawChoice(chooser.bottom, false);

    if (_preview->target.isPreviewOnly()) {
        return;
    }

    if (_preview->target.kind == EDockDropTargetKind::TabWell) {
        builder.addBrush(_preview->rect, style.dropPreviewMergeColor);
        builder.addRectOutline(_preview->rect, style.dropPreviewOutlineColor, 2.0f);
        return;
    }
    if (_preview->target.kind == EDockDropTargetKind::TabStackCenter) {
        drawChoice(chooser.center, !_preview->bDisabled);
        return;
    }
    if (_preview->target.splitSide == EDockCardinalSide::West) {
        drawChoice(chooser.left, !_preview->bDisabled);
    }
    else if (_preview->target.splitSide == EDockCardinalSide::East) {
        drawChoice(chooser.right, !_preview->bDisabled);
    }
    else if (_preview->target.splitSide == EDockCardinalSide::North) {
        drawChoice(chooser.top, !_preview->bDisabled);
    }
    else if (_preview->target.splitSide == EDockCardinalSide::South) {
        drawChoice(chooser.bottom, !_preview->bDisabled);
    }
}

void UIDockSpace::setContext(std::shared_ptr<FDockContext> context)
{
    // Rebind: the old context must not keep pointing back at us (a floating
    // host may keep it alive long after this widget is replaced).
    if (_context && _context != context && _context->dockSpace() == this) {
        _context->setDockSpace(nullptr);
    }
    _context = std::move(context);
    if (_context) {
        _context->setDockSpace(this);
        // Weak self: the context may fire dock-updated after this widget is
        // destroyed (it is kept alive by the floating host), so the callback
        // must never dereference a stale 'this'.
        std::weak_ptr<UIDockSpace> weakSelf = std::static_pointer_cast<UIDockSpace>(shared_from_this());
        _context->setOnDockUpdated([weakSelf]()
        {
            if (auto self = weakSelf.lock()) {
                if (self->getTree() && !self->_bRebuildingProjection) {
                    self->syncProjection(EDockProjectionSync::Structure);
                }
            }
        });
    }
    if (getTree()) {
        syncProjection(EDockProjectionSync::Structure);
    }
}

void UIDockSpace::syncProjection(EDockProjectionSync sync, DockNodeId stackId)
{
    // Single projection entry. rebuildProjection / rebuildStack are the
    // Structure / Stack implementations; callers (drop, tear-off, addPanel,
    // first layout) must not pick them directly.
    switch (sync) {
    case EDockProjectionSync::Structure: {
        rebuildProjection();
        break;
    }
    case EDockProjectionSync::Stack: {
        rebuildStack(stackId);
        break;
    }
    case EDockProjectionSync::Chrome: {
        syncTabBarVisibility();
        break;
    }
    }
}

void UIDockSpace::rebuildProjection()
{
    if (!getTree() || !_context || _bRebuildingProjection) {
        return;
    }
    if (UIElement* captured = getTree()->getPointerCapture()) {
        bool bCaptureInDock = false;
        for (UIElement* node = captured; node != nullptr; node = node->getParent()) {
            if (node == this) {
                bCaptureInDock = true;
                break;
            }
        }
        YA_CORE_ASSERT(!bCaptureInDock,
                       "UIDockSpace '{}': Structure rematerialize while pointer capture is held "
                       "by '{}'; press/release would straddle the rebuild and eat the next click",
                       _name,
                       captured->_name);
    }
    _bRebuildingProjection = true;
    clearPreview();
    releaseMountedPanels();
    const std::vector<UIElementRef> children = getChildren();
    for (const UIElementRef& child : children) {
        if (child && child->isAttached() && child->getTree() == getTree()) {
            getTree()->detach(*child);
        }
    }
    _stackViews.clear();
    _splitViews.clear();
    addDetachedChild(materializeNode(*_context->dockModel().getRootNode()));
    markLayoutDirty();
    markPaintDirty();
    _bRebuildingProjection = false;
}

void UIDockSpace::releaseMountedPanels()
{
    for (auto& [id, view] : _stackViews) {
        (void)id;
        if (!view.content) {
            continue;
        }
        const std::vector<UIElementRef> mounted = view.content->getChildren();
        for (const UIElementRef& child : mounted) {
            if (child) {
                unlinkWidgetFromVisualParent(*child, getTree());
            }
        }
    }
    if (!_context) {
        return;
    }
    // Panels coming from a just-closed floating window are not in `_stackViews`.
    // Unlink them here so rematerialize can graft without addDetachedChild
    // hitting a stale parent (drag keepAlive still holds the floating chrome).
    WidgetTree* tree = getTree();
    for (DockNodeId leafId : _context->dockModel().leafIds()) {
        const FDockNode* leaf = _context->dockModel().findNode(leafId);
        if (!leaf) {
            continue;
        }
        for (DockPanelId panelId : leaf->panelIds) {
            const FDockContext::FPanel* fp = _context->findPanel(panelId);
            if (fp && fp->widget) {
                unlinkWidgetFromVisualParent(*fp->widget, tree);
            }
        }
    }
}

void UIDockSpace::applyStackTabBarVisibility(DockNodeId leafId)
{
    const FDockNode* leaf = _context ? _context->dockModel().findNode(leafId) : nullptr;
    FDockStackView* view = stackViewFor(leafId);
    if (!leaf || !view || !view->well) {
        return;
    }
    const bool bPageChrome = leaf->leafRole == EDockLeafRole::Page;
    const bool bHideWell   = stackHidesInnerTabWell(*leaf);
    view->well->setVisibility(bHideWell ? EWidgetVisibility::Collapsed
                                        : EWidgetVisibility::Visible);
    if (view->hideAffordance) {
        const bool bShowReveal = !bPageChrome && leaf->bHideTabBar;
        view->hideAffordance->setVisibility(bShowReveal ? EWidgetVisibility::Visible
                                                        : EWidgetVisibility::Collapsed);
    }
    markLayoutDirty();
    markPaintDirty();
}

void UIDockSpace::syncTabBarVisibility()
{
    for (const auto& [leafId, view] : _stackViews) {
        (void)view;
        applyStackTabBarVisibility(leafId);
    }
}

void UIDockSpace::openStackTabBarMenu(DockNodeId leafId, const glm::vec2& pos)
{
    WidgetTree* tree = getTree();
    const FDockNode* leaf = _context ? _context->dockModel().findNode(leafId) : nullptr;
    if (!tree || !leaf || leaf->kind != EDockNodeKind::Stack ||
        leaf->leafRole == EDockLeafRole::Page) {
        return;
    }
    if (leaf->bHideTabBar || leaf->panelIds.size() != 1) {
        return;
    }
    auto menu = UIMenu::create({
        UIMenu::FItem{
            .label = "Hide Tab Bar",
            .action = [this, leafId]()
            {
                const FDockNode* current = _context->dockModel().findNode(leafId);
                if (!current || current->kind != EDockNodeKind::Stack ||
                    current->leafRole == EDockLeafRole::Page ||
                    current->panelIds.size() != 1) {
                    return;
                }
                if (_context->dockModel().setHideTabBar(leafId, true)) {
                    applyStackTabBarVisibility(leafId);
                    _context->notifyDockLayoutListeners();
                }
            },
        },
    });
    menu->openAt(*tree, pos);
}

void UIDockSpace::activateDraggedPanel(DockPanelId panelId)
{
    if (!_context) {
        return;
    }
    const FDockNode* leaf = _context->dockModel().findLeafForPanel(panelId);
    if (!leaf) {
        return;
    }
    FDockStackView* view = stackViewFor(leaf->id);
    if (!view || !view->well) {
        return;
    }
    auto it = std::find(leaf->panelIds.begin(), leaf->panelIds.end(), panelId);
    if (it == leaf->panelIds.end()) {
        return;
    }
    view->well->selectTab(static_cast<int>(std::distance(leaf->panelIds.begin(), it)));
}

void UIDockSpace::graftPanelIntoContent(UIContainer& content, const UIElementRef& panel)
{
    showPanelInContent(content, panel, getTree());
}

void UIDockSpace::bindStackHandlers(FDockStackView& view, DockNodeId stackId)
{
    if (!view.well) {
        return;
    }
    view.well->_onStripDoubleClick = [this]()
    {
        if (_context && _context->onTabBarDoubleClick) {
            _context->onTabBarDoubleClick();
        }
    };
    view.well->_onTabDragBegin = [this, stackId](int index, const std::string& label)
    {
        const FDockNode* currentLeaf = _context->dockModel().findNode(stackId);
        if (!currentLeaf || index < 0 || index >= static_cast<int>(currentLeaf->panelIds.size())) {
            return;
        }
        const DockPanelId panelId = currentLeaf->panelIds[static_cast<size_t>(index)];
        FDockSpacePanelDrag::beginPanelDrag(*this, panelId, label);
    };
    view.well->_onTabReordered = [this, stackId](int from, int to)
    {
        const FDockNode* leaf = _context ? _context->dockModel().findNode(stackId) : nullptr;
        if (!leaf || from < 0 || to < 0 ||
            from >= static_cast<int>(leaf->panelIds.size()) ||
            to >= static_cast<int>(leaf->panelIds.size())) {
            return;
        }
        const DockPanelId panelId = leaf->panelIds[static_cast<size_t>(from)];
        const size_t insert = static_cast<size_t>(to > from ? to + 1 : to);
        if (_context->dockModel().movePanel(panelId, stackId, insert, false)) {
            _context->notifyDockLayoutListeners();
        }
    };
    view.well->_onTabSelected = [this, stackId](int index)
    {
        const FDockNode* currentLeaf = _context->dockModel().findNode(stackId);
        FDockStackView* currentView = stackViewFor(stackId);
        if (!currentLeaf || !currentView || !currentView->content || index < 0 ||
            index >= static_cast<int>(currentLeaf->panelIds.size())) {
            return;
        }
        const DockPanelId panelId = currentLeaf->panelIds[static_cast<size_t>(index)];
        _context->dockModel().selectPanel(panelId);
        _context->rememberFocusedLeaf(stackId);
        if (const FDockContext::FPanel* fp = _context->findPanel(panelId)) {
            graftPanelIntoContent(*currentView->content, fp->widget);
        }
        _context->notifyDockLayoutListeners();
    };
    view.well->_onTabContextMenu = [this, stackId](int, const glm::vec2& logicalPoint)
    {
        openStackTabBarMenu(stackId, logicalPoint);
    };
}

void UIDockSpace::rebuildStack(DockNodeId leafId)
{
    if (!_context) {
        return;
    }
    const FDockNode* leaf = _context->dockModel().findNode(leafId);
    FDockStackView* view = stackViewFor(leafId);
    if (!leaf || !view || !view->well || !view->content) {
        return;
    }

    const std::vector<UIElementRef> contentChildren = view->content->getChildren();
    for (const UIElementRef& child : contentChildren) {
        if (child) {
            unlinkWidgetFromVisualParent(*child, getTree());
        }
    }

    const int tabCount = static_cast<int>(view->well->getChildren().size());
    for (int i = tabCount - 1; i >= 0; --i) {
        view->well->removeTab(i);
    }

    for (DockPanelId panelId : leaf->panelIds) {
        if (const FDockContext::FPanel* fp = _context->findPanel(panelId)) {
            UITabButton* tab = view->well->addTab(fp->name);
            if (const FDockPanelRecord* rec = _context->dockModel().findPanel(panelId)) {
                tab->_bClosable = rec->closable;
                if (rec->closable) {
                    tab->_onClose = [this, panelId]()
                    {
                        if (_context) {
                            _context->closePanel(panelId);
                        }
                    };
                }
            }
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

    if (selectedIndex >= 0) {
        view->well->syncSelectedTab(selectedIndex);
        DockPanelId selectedPanel = leaf->panelIds[static_cast<size_t>(selectedIndex)];
        if (const FDockContext::FPanel* fp = _context->findPanel(selectedPanel)) {
            graftPanelIntoContent(*view->content, fp->widget);
        }
    }
    applyStackTabBarVisibility(leafId);
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
        _splitViews[node.id] = split.get();
        const DockNodeId splitId = node.id;
        split->setSplitRatioChangedCallback([this, splitId](float ratio)
        {
            if (_context->dockModel().setSplitRatio(splitId, ratio)) {
                markLayoutDirty();
                markPaintDirty();
                _context->notifyDockLayoutListeners();
            }
        });
        if (node.child[0]) split->addDetachedChild(materializeNode(*node.child[0]));
        if (node.child[1]) split->addDetachedChild(materializeNode(*node.child[1]));
        return split;
    }

    auto stack = std::make_shared<UIDockTabStack>(std::format("DockStack{}", node.id), this, node.id);
    auto chrome = std::make_shared<UIContainer>(std::format("DockStackChrome{}", node.id));
    chrome->setDirection(EWidgetBoxLayout::Vertical);
    chrome->setSpacing(0.0f);
    chrome->setClipChildren(true);
    auto bar = std::make_shared<UIDockTabWell>(std::format("DockTabBar{}", node.id), this, node.id);
    bar->_bDraggableTabs = true;
    bar->_styleKey = "tab.dock";
    bar->setClipChildren(true);
    bar->setPadding({2.0f, 1.0f});
    bar->setSpacing(1.0f);
    chrome->addDetachedChild(bar);

    auto body = std::make_shared<UIBorder>(std::format("DockBody{}", node.id));
    body->_styleKey = "panel.surface";
    chrome->addDetachedChild(body);

    auto content = std::make_shared<UIContainer>(std::format("DockContent{}", node.id));
    content->setPadding({kDockContentInset, kDockContentInset});
    chrome->setStretchLastChild(true);
    body->addDetachedChild(content);
    content->setStretchLastChild(true);

    stack->addDetachedChild(chrome, [](UIElement&, UISlot& slot)
    {
        slot.applyArgs(FOverlaySlotArgs{
            .hAlign = EUIOverlayAlignment::Fill,
            .vAlign = EUIOverlayAlignment::Fill,
        });
    });
    auto hideBar = std::make_shared<FDockHideTabBarAffordance>(
        "DockHideTabBar",
        [this, leafId = node.id]()
        {
            const FDockNode* current = _context->dockModel().findNode(leafId);
            if (!current || current->kind != EDockNodeKind::Stack ||
                current->leafRole == EDockLeafRole::Page || !current->bHideTabBar) {
                return;
            }
            if (_context->dockModel().setHideTabBar(leafId, false)) {
                applyStackTabBarVisibility(leafId);
                _context->notifyDockLayoutListeners();
            }
        });
    stack->addDetachedChild(hideBar, [](UIElement&, UISlot& slot)
    {
        slot.applyArgs(FOverlaySlotArgs{
            .hAlign        = EUIOverlayAlignment::Start,
            .vAlign        = EUIOverlayAlignment::Start,
            .preferredSize = {kDockHideTabBarSize, kDockHideTabBarSize},
        });
    });
    _stackViews[node.id] = {
        .stackId        = node.id,
        .root           = stack.get(),
        .well           = bar.get(),
        .content        = content.get(),
        .hideAffordance = hideBar.get(),
    };
    bindStackHandlers(_stackViews[node.id], node.id);
    syncProjection(EDockProjectionSync::Stack, node.id);
    return stack;
}

void UIDockSpace::applySplitMinsFromModel()
{
    if (!_context) {
        return;
    }
    for (auto& [id, split] : _splitViews) {
        if (!split) {
            continue;
        }
        const FDockNode* node = _context->dockModel().findNode(id);
        if (!node || node->kind != EDockNodeKind::Split) {
            continue;
        }
        // Mins are host policy and can change without rematerialize
        // (spawn-complete). Ratio is the user's drag; writing it here
        // fights clampRatio, jumps the divider, and can pin capture.
        split->setMinFirstExtent(node->minExtent[0]);
        split->setMinSecondExtent(node->minExtent[1]);
    }
}

void UIDockSpace::applyAssignedLayout(const Rect2D& rect)
{
    // First layout of an empty Area materializes the *current* model, so
    // attach → mutate model → layout still works. After that, Structure
    // syncs come from syncProjection (drop / tear-off / fireDockUpdated).
    if (getChildren().empty() && getTree()) {
        syncProjection(EDockProjectionSync::Structure);
    }
    applySplitMinsFromModel();
    UIElement::applyAssignedLayout(rect);
}

bool UIDockSpace::assignedLayoutInputsUnchanged() const
{
    return !getChildren().empty() || getTree() == nullptr;
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
    if (!_context) {
        return;
    }
    const DockPanelId panelId = _context->addPanel(name, std::move(widget));
    if (panelId == kInvalidDockPanelId) {
        YA_CORE_WARN("UIDockSpace '{}': rejected duplicate or invalid panel '{}'", _name, name);
    }
}

std::optional<UIDockSpace::FDropPreview> UIDockSpace::resolveFloatingWell(const glm::vec2& logicalPoint,
                                                                          DockPanelId      panelId,
                                                                          bool             bImport) const
{
    if (!_context) {
        return std::nullopt;
    }
    const bool bFloating = !bImport && _context->isPanelFloating(panelId);
    const FDockContext::FFloatingWindow* sourceFloating =
        bFloating ? _context->findFloatingByPanel(panelId) : nullptr;
    for (const FDockContext::FFloatingWindow& fw : _context->floatingWindows()) {
        if (sourceFloating && sourceFloating->id == fw.id) {
            continue;
        }
        Rect2D fwRect;
        fwRect.pos    = fw.pos;
        fwRect.extent = fw.size;
        if (pointInRect(logicalPoint, fwRect)) {
            return FDropPreview{
                .target  = FDockDropTarget::floatingWell(fw.id),
                .panelId = panelId,
                .rect    = fwRect,
                .prompt  = "Add Tab",
            };
        }
    }
    return std::nullopt;
}

const UIDockSpace::FDockStackView* UIDockSpace::focusStackAt(const glm::vec2& logicalPoint,
                                                       const FDockNode* sourceLeaf) const
{
    for (const auto& [leafId, view] : _stackViews) {
        (void)leafId;
        if (view.root && pointInRect(logicalPoint, view.root->_layoutRect)) {
            return &view;
        }
    }
    if (sourceLeaf) {
        return stackViewFor(sourceLeaf->id);
    }
    return nullptr;
}

std::optional<UIDockSpace::FDropPreview> UIDockSpace::resolveTabWell(const FDockStackView& focus,
                                                                     const glm::vec2& logicalPoint,
                                                                     DockPanelId      panelId) const
{
    if (!focus.well || !pointInRect(logicalPoint, focus.well->_layoutRect)) {
        return std::nullopt;
    }
    return FDropPreview{
        .target  = FDockDropTarget::well(focus.stackId, tabInsertIndexAt(focus.stackId, logicalPoint)),
        .panelId = panelId,
        .rect    = focus.well->_layoutRect,
        .prompt  = "Add Tab",
    };
}

std::optional<UIDockSpace::FDropPreview> UIDockSpace::resolveTabStack(const FDockStackView& focus,
                                                                      const glm::vec2& logicalPoint,
                                                                      DockPanelId      panelId,
                                                                      const FDockNode* sourceLeaf) const
{
    if (!focus.root) {
        return std::nullopt;
    }
    (void)sourceLeaf;
    const Rect2D leafRect  = focus.root->_layoutRect;
    const bool   bOverLeaf = pointInRect(logicalPoint, leafRect);

    if (!bOverLeaf) {
        return FDropPreview{
            .target  = FDockDropTarget::noTarget(focus.stackId),
            .panelId = panelId,
            .rect    = leafRect,
        };
    }

    const FChooserRects chooser  = makeChooserRects(leafRect);
    const bool          inCenter = pointInRect(logicalPoint, chooser.center);
    const bool          inLeft   = pointInRect(logicalPoint, chooser.left);
    const bool          inRight  = pointInRect(logicalPoint, chooser.right);
    const bool          inTop    = pointInRect(logicalPoint, chooser.top);
    const bool          inBottom = pointInRect(logicalPoint, chooser.bottom);

    if (!inCenter && !inLeft && !inRight && !inTop && !inBottom) {
        return FDropPreview{
            .target  = FDockDropTarget::stackChooser(focus.stackId),
            .panelId = panelId,
            .rect    = leafRect,
        };
    }
    if (inCenter) {
        return FDropPreview{
            .target  = FDockDropTarget::stackCenter(focus.stackId),
            .panelId = panelId,
            .rect    = chooser.center,
            .prompt  = "Dock Center",
        };
    }

    EDockCardinalSide side  = EDockCardinalSide::South;
    Rect2D            rect  = chooser.bottom;
    const char*       label = "Bottom";
    if (inLeft) {
        side  = EDockCardinalSide::West;
        rect  = chooser.left;
        label = "Left";
    }
    else if (inRight) {
        side  = EDockCardinalSide::East;
        rect  = chooser.right;
        label = "Right";
    }
    else if (inTop) {
        side  = EDockCardinalSide::North;
        rect  = chooser.top;
        label = "Top";
    }
    return FDropPreview{
        .target  = FDockDropTarget::stackSplit(focus.stackId, side),
        .panelId = panelId,
        .rect    = rect,
        .prompt  = label,
    };
}

std::optional<UIDockSpace::FDropPreview> UIDockSpace::resolveDropPreview(const glm::vec2& logicalPoint,
                                                                         DockPanelId      panelId,
                                                                         bool             bImport) const
{
    if (!_context || panelId == kInvalidDockPanelId) {
        return std::nullopt;
    }
    const FDockNode* sourceLeaf = bImport ? nullptr : _context->dockModel().findLeafForPanel(panelId);
    const bool       bFloating  = !bImport && _context->isPanelFloating(panelId);
    if (!bImport && !sourceLeaf && !bFloating) {
        return std::nullopt;
    }
    if (auto floating = resolveFloatingWell(logicalPoint, panelId, bImport)) {
        return floating;
    }
    // A nested UIDockSpace (Level Viewport / Hierarchy) is a descendant of the
    // window-root page leaf. That leaf's rect covers the whole upper window, so
    // without this check the outer Area paints a page chooser on top of the
    // nested stacks. Innermost dock owns the point.
    if (const WidgetTree* tree = getTree()) {
        if (UIElement* hit = tree->pickAt(logicalPoint)) {
            if (UIDockSpace* inner = enclosingDockSpace(hit); inner && inner != this) {
                return std::nullopt;
            }
        }
    }
    const FDockStackView* focus = focusStackAt(logicalPoint, sourceLeaf);
    if (!focus || !focus->root) {
        return std::nullopt;
    }
    if (auto well = resolveTabWell(*focus, logicalPoint, panelId)) {
        return well;
    }
    return resolveTabStack(*focus, logicalPoint, panelId, sourceLeaf);
}

std::optional<UIDockSpace::FDropPreview> UIDockSpace::dropPreviewFor(const UIDragDropOperation& operation,
                                                                     const glm::vec2& logicalPoint) const
{
    const auto* dockOp = operation.as<FDockPanelDragDropOp>();
    if (!dockOp || dockOp->panelId == kInvalidDockPanelId) {
        return std::nullopt;
    }
    const bool bImport = dockOp->sourceContext != nullptr && dockOp->sourceContext != _context.get();
    if (bImport && _context && dockOp->sourceContext) {
        const FDockPanelRecord* record = dockOp->sourceContext->dockModel().findPanel(dockOp->panelId);
        const FDockContext::FPanel* panel = dockOp->sourceContext->findPanel(dockOp->panelId);
        if (record && panel &&
            !_context->acceptsImportedPanel(record->stableKey, panel->ownerEditorId, panel->documentKey)) {
            return std::nullopt;
        }
    }
    auto preview = resolveDropPreview(logicalPoint, dockOp->panelId, bImport);
    if (!preview || preview->target.isPreviewOnly() || preview->target.stackId == kInvalidDockNodeId ||
        !_context) {
        return preview;
    }
    const FDockContext* source = bImport ? dockOp->sourceContext : _context.get();
    const FDockPanelRecord* record = source ? source->dockModel().findPanel(dockOp->panelId) : nullptr;
    const FDockContext::FPanel* panel = source ? source->findPanel(dockOp->panelId) : nullptr;
    if (record && panel &&
        !_context->acceptsLeafDrop(record->stableKey,
                                   panel->ownerEditorId,
                                   panel->documentKey,
                                   preview->target.stackId,
                                   preview->target.isMerge())) {
        preview->bDisabled = true;
        preview->disabledReason = "Cannot dock here";
    }
    return preview;
}

void UIDockSpace::hoverDrop(const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    _preview = dropPreviewFor(operation, logicalPoint);
    syncPreviewOverlay();
    markPaintDirty();
}

void UIDockSpace::applyDrop(const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    if (!_context) {
        return;
    }
    const auto* dockOp = operation.as<FDockPanelDragDropOp>();
    if (!dockOp || dockOp->panelId == kInvalidDockPanelId) {
        clearPreview();
        return;
    }
    const bool bImport =
        dockOp->sourceContext != nullptr && dockOp->sourceContext != _context.get();
    auto preview = dropPreviewFor(operation, logicalPoint);
    clearPreview();
    if (!preview || preview->bDisabled || !preview->target.commitsDrop()) {
        return;
    }

    DockPanelId panelId = dockOp->panelId;
    if (bImport) {
        const FDockPanelRecord* record = dockOp->sourceContext->dockModel().findPanel(panelId);
        const FDockContext::FPanel* panel = dockOp->sourceContext->findPanel(panelId);
        if (!record || !panel ||
            !_context->acceptsImportedPanel(record->stableKey,
                                            panel->ownerEditorId,
                                            panel->documentKey)) {
            return;
        }
        std::optional<FDockContext::FDockExtractedPanel> extracted =
            dockOp->sourceContext->extractPanel(panelId);
        if (!extracted) {
            return;
        }
        panelId = _context->adoptPanel(std::move(*extracted));
        if (panelId == kInvalidDockPanelId) {
            return;
        }
    }

    const EDockDropCommit commit = _context->commitDrop(panelId, preview->target);
    if (commit == EDockDropCommit::Selected) {
        activateDraggedPanel(panelId);
    }
    else if (commit == EDockDropCommit::Applied) {
        syncProjection(EDockProjectionSync::Structure);
        _context->notifyDockLayoutListeners();
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
