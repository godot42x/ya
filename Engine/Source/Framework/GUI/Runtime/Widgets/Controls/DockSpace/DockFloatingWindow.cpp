#include "GUI/Widgets/Controls/DockSpace/DockFloatingWindow.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/Event.h"

#include <algorithm>
#include <functional>

namespace ya
{

namespace
{
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

bool pointInRect(const glm::vec2& point, const Rect2D& rect)
{
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.extent.y;
}

constexpr float kResizeThickness = 6.0f;
constexpr float kCornerGripSize  = 14.0f;
constexpr float kHideTabBarSize  = 12.0f;

struct FDockHideTabBarAffordance final : UIElement
{
    FDockHideTabBarAffordance(std::function<void()> onToggle, std::function<bool()> isFolded)
        : UIElement("FloatingHideTabBar")
        , _onToggle(std::move(onToggle))
        , _isFolded(std::move(isFolded))
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
        const float     inset = std::max(1.0f, s * 0.18f);
        if (_isFolded && _isFolded()) {
            const glm::vec2 a = {p.x + inset, p.y + inset};
            const glm::vec2 b = {p.x + inset, p.y + s - inset};
            const glm::vec2 c = {p.x + s - inset, p.y + s * 0.5f};
            builder.addLine(a, b, color, 1.5f);
            builder.addLine(b, c, color, 1.5f);
            builder.addLine(c, a, color, 1.5f);
            return;
        }
        const glm::vec2 a = {p.x + inset, p.y + inset};
        const glm::vec2 b = {p.x + s - inset, p.y + inset};
        const glm::vec2 c = {p.x + s * 0.5f, p.y + s - inset};
        builder.addLine(a, b, color, 1.5f);
        builder.addLine(b, c, color, 1.5f);
        builder.addLine(c, a, color, 1.5f);
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
    std::function<bool()> _isFolded;
    bool                  _bHovered = false;
};

} // namespace

struct FDockFloatingWindowDropTargetBehavior final : public UIDropTargetBehavior
{
    FDockFloatingWindowDropTargetBehavior()
    {
        canAccept = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
        {
            auto* window = dynamic_cast<UIDockFloatingWindow*>(&owner);
            UIDockSpace* space = window && window->_context ? window->_context->dockSpace() : nullptr;
            if (!space) {
                return false;
            }
            const auto preview = space->dropPreviewFor(operation, logicalPoint);
            return preview.has_value() && preview->bMerge &&
                   preview->targetFloatingId == window->_floatingId && !preview->bDisabled;
        };
        handleDrop = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
        {
            auto* window = dynamic_cast<UIDockFloatingWindow*>(&owner);
            if (!window) {
                return;
            }
            if (UIDockSpace* space = window->_context ? window->_context->dockSpace() : nullptr) {
                space->onDrop(operation, logicalPoint);
            }
        };
    }
};

struct FDockFloatingWindowPanelDragBehavior final : public UIBehavior
{
    void beginPanelDrag(UIDockFloatingWindow& owner, DockPanelId panelId, std::string label)
    {
        WidgetTree* tree = owner.getTree();
        if (!tree) {
            return;
        }

        owner._bDockDragging = true;
        owner._bTitlePressed = false;
        owner._bTitleMoving  = false;
        DragSessionObserver observer;
        auto lastPreview = std::make_shared<std::optional<UIDockSpace::FDropPreview>>();
        observer.onMove = [&owner, lastPreview](const UIDragDropOperation& operation, const glm::vec2& logicalPoint, std::string_view)
        {
            if (owner._lastDragPoint) {
                Rect2D moved = owner.getWindowRect();
                moved.pos += logicalPoint - *owner._lastDragPoint;
                owner.setWindowRect(moved);
                if (owner._context) {
                    owner._context->setFloatingWindowRect(owner._floatingId, moved.pos, moved.extent);
                }
            }
            owner._lastDragPoint = logicalPoint;
            if (WidgetTree* tree = owner.getTree()) {
                tree->invalidateLayout();
            }
            UIDockSpace* space = owner._context ? owner._context->dockSpace() : nullptr;
            if (!space) {
                return;
            }
            space->updateDropHover(operation, logicalPoint);
            if (space->hasDropPreview()) {
                *lastPreview = space->dropPreview();
            }
            else if (*lastPreview) {
                space->setDropPreview(**lastPreview);
            }
        };
        observer.onTargetChanged = [](std::string_view, std::string_view) {};
        observer.onFinished = [&owner](EDragFinishResult, const glm::vec2&, std::string_view)
        {
            owner._lastDragPoint.reset();
            owner._bDockDragging = false;
            if (UIDockSpace* space = owner._context ? owner._context->dockSpace() : nullptr) {
                space->clearDropPreview();
            }
            owner.refreshFromContext();
        };
        tree->beginDrag(&owner, FDockPanelDragDropOp::make(panelId, std::move(label)), std::move(observer), false, true);
    }
};

namespace
{

struct FResizeHandle final : UIElement
{
    FResizeHandle(UIDockFloatingWindow* owner, UIDockFloatingWindow::EResizeEdge edge)
        : UIElement("FloatingResizeHandle")
        , _owner(owner)
        , _edge(edge)
    {
        _hitFilter = EWidgetHitFilter::Stop;
    }

    [[nodiscard]] bool hitTestSelf(const glm::vec2& logicalPoint) const override
    {
        return isHitTestableSelf() && hitTestLayoutRect(logicalPoint);
    }

    [[nodiscard]] bool isHoverable() const override
    {
        return true;
    }

    [[nodiscard]] ECursorType getCursor() const override
    {
        switch (_edge) {
        case UIDockFloatingWindow::EResizeEdge::Left:
        case UIDockFloatingWindow::EResizeEdge::Right:
        case UIDockFloatingWindow::EResizeEdge::BottomRight:
            return ECursorType::ResizeEastWest;
        case UIDockFloatingWindow::EResizeEdge::Top:
        case UIDockFloatingWindow::EResizeEdge::Bottom:
            return ECursorType::ResizeNorthSouth;
        }
        return ECursorType::Arrow;
    }

    void paintSelf(UIFrameBuilder& builder) override
    {
        // Theme values come from the owner's computed-style cache. The handle
        // is not a UIStyledWidget, so it still registers its own theme edges —
        // otherwise a theme switch would leave the handle's paint items cached
        // while the window body updated.
        glm::vec4 edgeColor = FFloatingWindowStyle{}.edgeAffordance;
        if (_owner) {
            const FFloatingWindowStyle& ownerStyle =
                _owner->resolvedStyle(ReactiveBase::EDirtyLevel::Paint, false);
            if (stylePatchUsesThemeBase<FFloatingWindowStyle>(_owner->_styleKey, _owner->_authoredStyle)) {
                (void)resolveThemeStyle<FFloatingWindowStyle>(*this, _owner->_styleKey);
            }
            edgeColor = ownerStyle.edgeAffordance;
        }
        switch (_edge) {
        case UIDockFloatingWindow::EResizeEdge::Left:
            builder.addSprite({_layoutRect.pos, {1.0f, _layoutRect.extent.y}}, edgeColor, nullptr);
            break;
        case UIDockFloatingWindow::EResizeEdge::Right:
            builder.addSprite({glm::vec2{_layoutRect.pos.x + _layoutRect.extent.x - 1.0f,
                                         _layoutRect.pos.y},
                               glm::vec2{1.0f, _layoutRect.extent.y}},
                              edgeColor, nullptr);
            break;
        case UIDockFloatingWindow::EResizeEdge::Top:
            builder.addSprite({_layoutRect.pos, {_layoutRect.extent.x, 1.0f}}, edgeColor, nullptr);
            break;
        case UIDockFloatingWindow::EResizeEdge::Bottom:
            builder.addSprite({glm::vec2{_layoutRect.pos.x,
                                         _layoutRect.pos.y + _layoutRect.extent.y - 1.0f},
                               glm::vec2{_layoutRect.extent.x, 1.0f}},
                              edgeColor, nullptr);
            break;
        case UIDockFloatingWindow::EResizeEdge::BottomRight: {
            const glm::vec2 p = _layoutRect.pos;
            const glm::vec2 e = _layoutRect.extent;
            builder.addSprite({glm::vec2{p.x + e.x - 8.0f, p.y + e.y - 2.0f}, glm::vec2{6.0f, 1.0f}},
                              edgeColor, nullptr);
            builder.addSprite({glm::vec2{p.x + e.x - 6.0f, p.y + e.y - 4.0f}, glm::vec2{4.0f, 1.0f}},
                              edgeColor, nullptr);
            builder.addSprite({glm::vec2{p.x + e.x - 4.0f, p.y + e.y - 6.0f}, glm::vec2{2.0f, 1.0f}},
                              edgeColor, nullptr);
            break;
        }
        }
    }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        const EEvent::T eventType = event.getEventType();
        if (eventType == EEvent::MouseButtonPressed) {
            _bResizing = true;
            _resizeStartPoint = ctx.logicalPoint;
            _resizeStartRect  = _owner->getWindowRect();
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        }
        if (_bResizing && ctx.bViaCapture) {
            if (eventType == EEvent::MouseMoved) {
                _owner->setWindowRect(_resizeStartRect);
                _owner->applyResizeFromEdge(_edge, ctx.logicalPoint - _resizeStartPoint);
                if (WidgetTree* tree = getTree()) {
                    tree->invalidateLayout();
                }
                return true;
            }
            if (eventType == EEvent::MouseButtonReleased) {
                _bResizing = false;
                _owner->commitGeometryToContext(true);
                if (WidgetTree* tree = getTree()) {
                    tree->releasePointerCapture(this);
                    tree->invalidateLayout();
                }
                return true;
            }
        }
        return false;
    }

    void clearTransientInputState() override
    {
        _bResizing = false;
    }

  private:
    UIDockFloatingWindow* _owner = nullptr;
    UIDockFloatingWindow::EResizeEdge _edge = UIDockFloatingWindow::EResizeEdge::BottomRight;
    bool _bResizing = false;
    glm::vec2 _resizeStartPoint{0.0f, 0.0f};
    Rect2D _resizeStartRect{glm::vec2{0.0f, 0.0f}, glm::vec2{0.0f, 0.0f}};
};
} // namespace

UIDockFloatingWindow::UIDockFloatingWindow(std::string name, FDockFloatingWindowId floatingId,
                                           std::shared_ptr<FDockContext> context)
    : UIElement(std::move(name), "floating")
    , _floatingId(floatingId)
    , _context(std::move(context))
{
    installLayout(std::make_unique<UIOverlayLayout>());
    _hitFilter = EWidgetHitFilter::Stop;
    addBehavior(std::make_shared<FDockFloatingWindowPanelDragBehavior>());
    addBehavior(std::make_shared<FDockFloatingWindowDropTargetBehavior>());

    auto chrome = std::make_shared<UIContainer>(std::format("{}_Chrome", _name));
    chrome->setDirection(EWidgetBoxLayout::Vertical);
    chrome->setSpacing(0.0f);
    chrome->setClipChildren(true);
    chrome->setStretchLastChild(true);
    _chrome = chrome;

    auto header = std::make_shared<UIContainer>(std::format("{}_Header", _name));
    header->setDirection(EWidgetBoxLayout::Horizontal);
    header->setSpacing(0.0f);
    _header = header;

    _tabBar = std::make_shared<UITabBar>(std::format("{}_TabBar", _name));
    _tabBar->_bDraggableTabs = true;
    _tabBar->setPadding({2.0f, 1.0f});
    _tabBar->setSpacing(1.0f);
    _tabBar->_onTabDragBegin = [this](int index, const std::string&)
    {
        if (index < 0) {
            return;
        }
        if (const auto* rec = _context->findFloatingById(_floatingId)) {
            if (static_cast<size_t>(index) < rec->panelIds.size()) {
                _panelId = rec->panelIds[static_cast<size_t>(index)];
                _title = _context->findPanel(_panelId) ? _context->findPanel(_panelId)->name : std::string{};
            }
        }
        if (auto* behavior = findBehavior<FDockFloatingWindowPanelDragBehavior>(*this)) {
            behavior->beginPanelDrag(*this, _panelId, _title);
        }
    };
    header->addDetachedChild(_tabBar);

    auto close = std::make_shared<UIButton>(std::format("{}_Close", _name));
    close->setContentPadding({4.0f, 2.0f});
    auto closeText = std::make_shared<UIText>(std::format("{}_CloseLabel", _name));
    closeText->setText("x");
    closeText->_fontSize = 12;
    close->addDetachedChild(closeText);
    close->_onClick = [this]()
    {
        if (_context && _context->dockPanelHome(_panelId) && _context->floatingHost()) {
            // Host observes floating drift via the context; the window is
            // removed by the host once it re-syncs its window set.
        }
    };
    header->addDetachedChild(close);

    _content = std::make_shared<UIContainer>(std::format("{}_Content", _name));
    _content->setClipChildren(true);
    _content->setStretchLastChild(true);
    chrome->addDetachedChild(header);
    chrome->addDetachedChild(_content);
    addDetachedChild(chrome);

    auto hideBar = std::make_shared<FDockHideTabBarAffordance>(
        [this]()
        {
            if (!_context) {
                return;
            }
            const auto* rec = _context->findFloatingById(_floatingId);
            if (!rec) {
                return;
            }
            _context->setFloatingHideTabBar(_floatingId, !rec->bHideTabBar);
            _context->fireFloatingUpdated();
            refreshFromContext();
        },
        [this]()
        {
            const auto* rec = _context ? _context->findFloatingById(_floatingId) : nullptr;
            return rec && rec->bHideTabBar;
        });
    addDetachedChild(hideBar, [](UIElement&, UISlot& slot)
    {
        if (auto* overlay = slot.as<UIOverlaySlot>()) {
            overlay->apply(FOverlaySlotArgs{
                .hAlign        = EUIOverlayAlignment::Start,
                .vAlign        = EUIOverlayAlignment::Start,
                .preferredSize = {kHideTabBarSize, kHideTabBarSize},
            });
        }
    });

    refreshFromContext();

    const auto addHandle = [this](EResizeEdge edge, EUIOverlayAlignment hAlign,
                                  EUIOverlayAlignment vAlign, glm::vec2 desired)
    {
        auto handle = std::make_shared<FResizeHandle>(this, edge);
        addDetachedChild(handle, [hAlign, vAlign, desired](UIElement&, UISlot& slot)
        {
            if (auto* overlay = slot.as<UIOverlaySlot>()) {
                overlay->apply(FOverlaySlotArgs{
                    .hAlign        = hAlign,
                    .vAlign        = vAlign,
                    .preferredSize = desired,
                });
            }
        });
        _resizeHandles.push_back(std::move(handle));
    };
    addHandle(EResizeEdge::Left, EUIOverlayAlignment::Start, EUIOverlayAlignment::Fill,
              {kResizeThickness, kResizeThickness});
    addHandle(EResizeEdge::Right, EUIOverlayAlignment::End, EUIOverlayAlignment::Fill,
              {kResizeThickness, kResizeThickness});
    addHandle(EResizeEdge::Top, EUIOverlayAlignment::Fill, EUIOverlayAlignment::Start,
              {kResizeThickness, kResizeThickness});
    addHandle(EResizeEdge::Bottom, EUIOverlayAlignment::Fill, EUIOverlayAlignment::End,
              {kResizeThickness, kResizeThickness});
    addHandle(EResizeEdge::BottomRight, EUIOverlayAlignment::End, EUIOverlayAlignment::End,
              {kCornerGripSize, kCornerGripSize});

    _windowRect = {glm::vec2{120.0f, 120.0f}, glm::vec2{360.0f, 260.0f}};
    setWindowRect(_windowRect);
}

void UIDockFloatingWindow::setWindowRect(const Rect2D& rect)
{
    _windowRect = rect;
    const FChildSlotInitializer applyRect = [rect](UIElement&, UISlot& edge) {
        if (auto* slot = edge.as<UICanvasSlot>()) {
            FCanvasSlotArgs args;
            args.offset    = rect.pos;
            args.fixedSize = rect.extent;
            slot->apply(args);
        }
    };
    if (UIElement* parent = getParent()) {
        parent->initializeChildSlot(*this, applyRect);
    }
}

void UIDockFloatingWindow::onAttached()
{
    setWindowRect(_windowRect);
}

void UIDockFloatingWindow::refreshFromContext()
{
    if (!_context) {
        return;
    }
    const auto* rec = _context->findFloatingById(_floatingId);
    if (!rec) {
        return;
    }
    // Rebuild the tab strip to match the floating record's panel set.
    const int tabCount = static_cast<int>(_tabBar->getChildren().size());
    for (int i = tabCount - 1; i >= 0; --i) {
        _tabBar->removeTab(i);
    }
    for (const DockPanelId pid : rec->panelIds) {
        const std::string title = _context->findPanel(pid) ? _context->findPanel(pid)->name : std::string{};
        _tabBar->addTab(title.empty() ? "?" : title);
    }
    _panelId = rec->activePanelId;
    if (_context->findPanel(_panelId)) {
        _title = _context->findPanel(_panelId)->name;
    }
    int activeIndex = 0;
    for (size_t i = 0; i < rec->panelIds.size(); ++i) {
        if (rec->panelIds[i] == _panelId) {
            activeIndex = static_cast<int>(i);
            break;
        }
    }
    _tabBar->syncSelectedTab(activeIndex);
    if (_header) {
        _header->setVisibility(rec->bHideTabBar ? EWidgetVisibility::Collapsed
                                                : EWidgetVisibility::Visible);
    }
    _tabBar->_onTabSelected = [this](int index)
    {
        if (const auto* r = _context->findFloatingById(_floatingId)) {
            if (static_cast<size_t>(index) < r->panelIds.size()) {
                _panelId = r->panelIds[static_cast<size_t>(index)];
                _title = _context->findPanel(_panelId) ? _context->findPanel(_panelId)->name : std::string{};
                _context->setFloatingWindowActivePanel(_floatingId, _panelId);
                _context->fireFloatingUpdated();
            }
        }
        rebuildContent();
    };
    rebuildContent();
}

void UIDockFloatingWindow::rebuildContent()
{
    if (!_content) {
        return;
    }
    WidgetTree* tree = getTree();
    const std::vector<UIElementRef> oldChildren = _content->getChildren();
    for (const UIElementRef& child : oldChildren) {
        if (child && tree) {
            tree->detach(*child);
        }
    }
    if (_context && _panelId != kInvalidDockPanelId) {
        if (const auto* panel = _context->findPanel(_panelId); panel && panel->widget) {
            if (tree) {
                tree->detach(*panel->widget);
            }
            if (tree && tree->contains(*_content)) {
                if (panel->widget->isAttached()) {
                    tree->reparent(*_content, panel->widget);
                }
                else {
                    tree->attach(*_content, panel->widget);
                }
            }
            else if (!panel->widget->getParent()) {
                _content->addDetachedChild(panel->widget);
            }
        }
    }
    if (tree) {
        tree->invalidateLayout();
    }
    markPaintDirty();
}

void UIDockFloatingWindow::layout(const Rect2D& parentRect)
{
    (void)parentRect;
    layoutAssigned(_windowRect);
}

void UIDockFloatingWindow::layoutAssigned(const Rect2D& rect)
{
    _windowRect = rect;
    UIElement::layoutAssigned(rect);
}

void UIDockFloatingWindow::paintSelf(UIFrameBuilder& builder)
{
    // Theme resolution (style-system Phase 3): body / border / inner chrome
    // from FFloatingWindowStyle when the key resolves; otherwise the
    // default-constructed style is the fallback (no magic literals). Pure
    // visual, Paint level (minSize is consumed by the resize interaction
    // path, not layout).
    const FFloatingWindowStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect, style.bodyFill);
    builder.addRectOutline(_layoutRect, style.borderColor, 1.0f);
    builder.addRectOutline(
        Rect2D{_layoutRect.pos + glm::vec2{1.0f, 1.0f}, _layoutRect.extent - glm::vec2{2.0f, 2.0f}},
        style.innerFill.tintColor, 1.0f);
}

void UIDockFloatingWindow::beginWindowMove()
{
    if (_onActivated) {
        _onActivated();
    }
    if (WidgetTree* tree = getTree()) {
        _bTitlePressed = false;
        _bTitleMoving = true;
        _lastDragPoint = tree->getPointerState().logicalPoint;
        tree->setPointerCapture(this);
    }
}

void UIDockFloatingWindow::updateWindowMove(const glm::vec2& logicalPoint)
{
    if (!_lastDragPoint) {
        _lastDragPoint = logicalPoint;
        return;
    }
    const glm::vec2 delta = logicalPoint - *_lastDragPoint;
    _lastDragPoint = logicalPoint;
    Rect2D moved = _windowRect;
    moved.pos += delta;
    setWindowRect(moved);
    commitGeometryToContext(false);
    if (WidgetTree* tree = getTree()) {
        tree->invalidateLayout();
    }
}

bool UIDockFloatingWindow::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    // While an active dock-panel drag session owns this window, the header's own
    // window-move path is bypassed: the session's onMove is the sole mover and
    // chooser driver.
    if (!_bDockDragging) {
        // Title-strip drag moves the floating window only. Docking is driven by
        // docked tab drag sessions elsewhere; the floating title bar is just a
        // window mover, so it must not emit dock payloads or dock chooser previews.
        if (eventType == EEvent::MouseButtonPressed && _header &&
            pointInRect(ctx.logicalPoint, _header->_layoutRect)) {
            _bTitlePressed   = true;
            _bTitleMoving    = false;
            _titlePressPoint = ctx.logicalPoint;
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        }
        if (_bTitlePressed) {
            if (eventType == EEvent::MouseMoved) {
                if (!_bTitleMoving && glm::length(ctx.logicalPoint - _titlePressPoint) > 6.0f) {
                    beginWindowMove();
                    return true;
                }
                if (_bTitleMoving) {
                    updateWindowMove(ctx.logicalPoint);
                }
                return true;
            }
            if (eventType == EEvent::MouseButtonReleased) {
                _bTitlePressed = false;
                _bTitleMoving = false;
                _lastDragPoint.reset();
                commitGeometryToContext(true);
                if (WidgetTree* tree = getTree()) {
                    tree->releasePointerCapture(this);
                }
                return true;
            }
        }
        if (_bTitleMoving && eventType == EEvent::MouseMoved) {
            updateWindowMove(ctx.logicalPoint);
            return true;
        }
        if (_bTitleMoving && eventType == EEvent::MouseButtonReleased) {
            _bTitleMoving = false;
            _lastDragPoint.reset();
            commitGeometryToContext(true);
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            return true;
        }
    }

    return UIElement::handleInputEvent(event, ctx);
}

void UIDockFloatingWindow::clearTransientInputState()
{
    _bTitlePressed = false;
    _bTitleMoving = false;
    _bDockDragging = false;
    _lastDragPoint.reset();
    UIElement::clearTransientInputState();
}

/// Resolve the window's min size from its theme style (fallback = the
/// framework-constructed FFloatingWindowStyle default minSize, which matches
/// the historical constants). Interaction paths read this live; the theme
/// edge is registered by paintSelf.
static glm::vec2 floatingMinSize(const UIDockFloatingWindow& window)
{
    return window.resolvedStyle(ReactiveBase::EDirtyLevel::Paint, false).minSize;
}

void UIDockFloatingWindow::resizeTo(const glm::vec2& extent)
{
    const glm::vec2 minSize = floatingMinSize(*this);
    Rect2D next = _windowRect;
    next.extent.x = std::max(minSize.x, extent.x);
    next.extent.y = std::max(minSize.y, extent.y);
    setWindowRect(next);
}

void UIDockFloatingWindow::applyResizeFromEdge(EResizeEdge edge, const glm::vec2& pointerDelta)
{
    const float     right   = _windowRect.pos.x + _windowRect.extent.x;
    const float     bottom  = _windowRect.pos.y + _windowRect.extent.y;
    const glm::vec2 minSize = floatingMinSize(*this);
    Rect2D          next    = _windowRect;

    switch (edge) {
    case EResizeEdge::Left: {
        const float nextLeft = std::min(next.pos.x + pointerDelta.x, right - minSize.x);
        next.pos.x    = nextLeft;
        next.extent.x = right - nextLeft;
        break;
    }
    case EResizeEdge::Right:
        next.extent.x = std::max(minSize.x, next.extent.x + pointerDelta.x);
        break;
    case EResizeEdge::Top: {
        const float nextTop = std::min(next.pos.y + pointerDelta.y, bottom - minSize.y);
        next.pos.y    = nextTop;
        next.extent.y = bottom - nextTop;
        break;
    }
    case EResizeEdge::Bottom:
        next.extent.y = std::max(minSize.y, next.extent.y + pointerDelta.y);
        break;
    case EResizeEdge::BottomRight:
        next.extent.x = std::max(minSize.x, next.extent.x + pointerDelta.x);
        next.extent.y = std::max(minSize.y, next.extent.y + pointerDelta.y);
        break;
    }
    setWindowRect(next);
    commitGeometryToContext(false);
}

void UIDockFloatingWindow::commitGeometryToContext(bool notify)
{
    if (!_context) {
        return;
    }
    _context->setFloatingWindowRect(_floatingId, _windowRect.pos, _windowRect.extent);
    if (notify) {
        _context->fireFloatingUpdated();
    }
}

} // namespace ya
