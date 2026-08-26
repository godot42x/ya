#include "GUI/Widgets/Controls/DockFloatingWindow.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/DockSpace.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

namespace
{

bool pointInRect(const glm::vec2& point, const Rect2D& rect)
{
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.extent.y;
}

constexpr float kResizeThickness = 6.0f;
constexpr float kCornerGripSize  = 14.0f;

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
        // Theme resolution (style-system Phase 3): the edge affordance color
        // comes from the owner window's FFloatingWindowStyle when its key
        // resolves (same tree; the handle registers its own generation edge).
        FFloatingWindowStyle style;
        if (_owner && !_owner->_styleKey.empty()) {
            if (const FFloatingWindowStyle* themed =
                    resolveThemeStyle<FFloatingWindowStyle>(*this, _owner->_styleKey)) {
                style = *themed;
            }
        }
        const glm::vec4 edgeColor = style.edgeAffordance;
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
                                           std::shared_ptr<UIDockWorkspace> ws)
    : UIContainer(std::move(name))
    , _floatingId(floatingId)
    , _ws(std::move(ws))
{
    setDirection(EWidgetBoxLayout::Vertical);
    setSpacing(0.0f);
    setClipChildren(true);
    _hitFilter = EWidgetHitFilter::Stop;

    auto header = std::make_shared<UIContainer>(std::format("{}_Header", _name));
    header->setDirection(EWidgetBoxLayout::Horizontal);
    header->setSpacing(0.0f);
    _header = header;

    _tabBar = std::make_shared<UITabBar>(std::format("{}_TabBar", _name));
    _tabBar->_bDraggableTabs = true;
    _tabBar->_onTabDragBegin = [this](int index, const std::string&)
    {
        if (index < 0) {
            return;
        }
        if (const auto* rec = _ws->findFloatingById(_floatingId)) {
            if (static_cast<size_t>(index) < rec->panelIds.size()) {
                _panelId = rec->panelIds[static_cast<size_t>(index)];
                _title = _ws->findPanel(_panelId) ? _ws->findPanel(_panelId)->name : std::string{};
            }
        }
        beginDockDrag();
    };
    header->addDetachedChild(_tabBar);

    auto close = std::make_shared<UIButton>(std::format("{}_Close", _name));
    close->setContentPadding({8.0f, 4.0f});
    auto closeText = std::make_shared<UIText>(std::format("{}_CloseLabel", _name));
    closeText->setText("x");
    closeText->_fontSize = 12;
    close->addDetachedChild(closeText);
    close->_onClick = [this]()
    {
        if (_ws && _ws->dockPanelHome(_panelId) && _ws->floatingHost()) {
            // Host observes floating drift via the workspace; the window is
            // removed by the host once it re-syncs its window set.
        }
    };
    header->addDetachedChild(close);
    addDetachedChild(header);

    _content = std::make_shared<UIContainer>(std::format("{}_Content", _name));
    _content->setClipChildren(true);
    _content->setStretchLastChild(true);
    addDetachedChild(_content);
    setStretchLastChild(true);

    refreshFromWorkspace();

    for (const EResizeEdge edge : {EResizeEdge::Left, EResizeEdge::Right, EResizeEdge::Top,
                                   EResizeEdge::Bottom, EResizeEdge::BottomRight}) {
        auto handle = std::make_shared<FResizeHandle>(this, edge);
        _resizeHandles.push_back(handle);
        addDetachedChild(handle);
    }

    _windowRect = {glm::vec2{120.0f, 120.0f}, glm::vec2{360.0f, 260.0f}};
}

void UIDockFloatingWindow::refreshFromWorkspace()
{
    if (!_ws) {
        return;
    }
    const auto* rec = _ws->findFloatingById(_floatingId);
    if (!rec) {
        return;
    }
    // Rebuild the tab strip to match the floating record's panel set.
    const int tabCount = static_cast<int>(_tabBar->getChildren().size());
    for (int i = tabCount - 1; i >= 0; --i) {
        _tabBar->removeTab(i);
    }
    for (const DockPanelId pid : rec->panelIds) {
        const std::string title = _ws->findPanel(pid) ? _ws->findPanel(pid)->name : std::string{};
        _tabBar->addTab(title.empty() ? "?" : title);
    }
    _panelId = rec->activePanelId;
    if (_ws->findPanel(_panelId)) {
        _title = _ws->findPanel(_panelId)->name;
    }
    int activeIndex = 0;
    for (size_t i = 0; i < rec->panelIds.size(); ++i) {
        if (rec->panelIds[i] == _panelId) {
            activeIndex = static_cast<int>(i);
            break;
        }
    }
    _tabBar->syncSelectedTab(activeIndex);
    _tabBar->_onTabSelected = [this](int index)
    {
        if (const auto* r = _ws->findFloatingById(_floatingId)) {
            if (static_cast<size_t>(index) < r->panelIds.size()) {
                _panelId = r->panelIds[static_cast<size_t>(index)];
                _title = _ws->findPanel(_panelId) ? _ws->findPanel(_panelId)->name : std::string{};
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
    // Swap in the active panel's widget (mirrors UIDockSpace::rebuildLeaf).
    if (tree) {
        auto oldChildren = _content->getChildrenInPaintOrder();
        for (UIElement* child : oldChildren) {
            if (child && child->participatesInLayout()) {
                tree->detach(*child);
                break;
            }
        }
    }
    if (_ws && _panelId != kInvalidDockPanelId) {
        if (const auto* panel = _ws->findPanel(_panelId)) {
            _content->addDetachedChild(panel->widget);
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
    (void)rect;
    setLayoutRect(_windowRect);
    UIContainer::layoutAssigned(_windowRect);
    const EResizeEdge edges[] = {EResizeEdge::Left, EResizeEdge::Right, EResizeEdge::Top,
                                 EResizeEdge::Bottom, EResizeEdge::BottomRight};
    for (size_t i = 0; i < _resizeHandles.size() && i < std::size(edges); ++i) {
        _resizeHandles[i]->layoutAssigned(resizeHandleRect(edges[i]));
    }
}

void UIDockFloatingWindow::paintSelf(UIFrameBuilder& builder)
{
    // Theme resolution (style-system Phase 3): body / border / inner chrome
    // from FFloatingWindowStyle when the key resolves; otherwise the
    // default-constructed style is the fallback (no magic literals). Pure
    // visual, Paint level (minSize is consumed by the resize interaction
    // path, not layout).
    FFloatingWindowStyle style;
    if (!_styleKey.empty()) {
        if (const FFloatingWindowStyle* themed =
                resolveThemeStyle<FFloatingWindowStyle>(*this, _styleKey)) {
            style = *themed;
        }
    }
    builder.addBrush(_layoutRect, style.bodyFill);
    builder.addRectOutline(_layoutRect, style.borderColor, 1.0f);
    builder.addRectOutline(
        Rect2D{_layoutRect.pos + glm::vec2{1.0f, 1.0f}, _layoutRect.extent - glm::vec2{2.0f, 2.0f}},
        style.innerFill.tintColor, 1.0f);
}

void UIDockFloatingWindow::beginDockDrag()
{
    if (WidgetTree* tree = getTree()) {
        // This drag session now owns the window: bypass the header's own
        // window-move path so it can't double-move the window.
        _bDockDragging = true;
        _bTitlePressed = false;
        _bTitleMoving  = false;
        const std::string payload =
            std::string(UIDockSpace::kDockPanelPayload) + std::to_string(_panelId);
        DragSessionObserver observer;
        // A floating window's tab drag is a docking intent: it projects the dock
        // chooser onto the DockSpace. The actual dock is performed by the
        // DockSpace itself — because bSkipSourceInHitTest lets WidgetTree's
        // findDropTarget fall through to the DockSpace beneath the floating
        // window, endDrag() already calls DockSpace::onDrop. Re-dropping here
        // would double-dock and corrupt the dock model (crash). So onFinished
        // only clears the preview.
        auto lastPreview = std::make_shared<std::optional<UIDockSpace::FDropPreview>>();
        observer.onMove = [this, payload, lastPreview](const std::string&, const glm::vec2& logicalPoint, std::string_view)
        {
            // Follow the pointer: move the floating window with the drag while
            // simultaneously projecting the dock chooser onto the DockSpace
            // beneath it. bSkipSourceInHitTest keeps this window from eating the
            // drop, so WidgetTree still routes the drop to the DockSpace.
            if (_lastDragPoint) {
                _windowRect.pos += logicalPoint - *_lastDragPoint;
                if (_ws) {
                    _ws->setFloatingWindowPos(_floatingId, _windowRect.pos);
                }
            }
            _lastDragPoint = logicalPoint;
            if (WidgetTree* tree = getTree()) {
                tree->invalidateLayout();
            }
            UIDockSpace* space = _ws ? _ws->dockSpace() : nullptr;
            if (!space) {
                return;
            }
            space->updateDropHover(payload, logicalPoint);
            if (space->hasDropPreview()) {
                // A leaf is under the pointer: remember the chooser so it can be
                // restored while the pointer is over empty canvas.
                *lastPreview = space->dropPreview();
            }
            else if (*lastPreview) {
                // Pointer over empty space / the floating window itself: keep the
                // last chooser visible instead of letting it vanish.
                space->setDropPreview(**lastPreview);
            }
        };
        observer.onTargetChanged = [](std::string_view, std::string_view) {};
        observer.onFinished = [this](EDragFinishResult, const glm::vec2&, std::string_view)
        {
            _lastDragPoint.reset();
            _bDockDragging = false;
            if (UIDockSpace* space = _ws ? _ws->dockSpace() : nullptr) {
                space->clearDropPreview();
            }
            // The dragged tab's panelId was captured as _panelId during the
            // drag; restore the workspace's active panel so the window shows the
            // correct tab if it stayed floating (a successful dock already
            // removed the panel and refreshed this window via the host).
            refreshFromWorkspace();
        };
        tree->beginDrag(this, payload, _title, std::move(observer), /*bShowGhost=*/false,
                        /*bSkipSourceInHitTest=*/true);
    }
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
    _windowRect.pos += delta;
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
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            return true;
        }
    }

    return UIContainer::handleInputEvent(event, ctx);
}

bool UIDockFloatingWindow::canAcceptDrop(const std::string& payload, const glm::vec2& logicalPoint)
{
    UIDockSpace* space = _ws ? _ws->dockSpace() : nullptr;
    if (!space) {
        return false;
    }
    const auto preview = space->dropPreviewFor(payload, logicalPoint);
    // Only accept a merge that targets THIS floating window as a new tab.
    return preview.has_value() && preview->bMerge &&
           preview->targetFloatingId == _floatingId && !preview->bDisabled;
}

void UIDockFloatingWindow::onDrop(const std::string& payload, const glm::vec2& logicalPoint)
{
    if (UIDockSpace* space = _ws ? _ws->dockSpace() : nullptr) {
        space->onDrop(payload, logicalPoint);
    }
}

void UIDockFloatingWindow::clearTransientInputState()
{
    _bTitlePressed = false;
    _bTitleMoving = false;
    _bDockDragging = false;
    _lastDragPoint.reset();
    UIContainer::clearTransientInputState();
}

/// Resolve the window's min size from its theme style (fallback = the
/// framework-constructed FFloatingWindowStyle default minSize, which matches
/// the historical constants). Interaction paths read this live; the theme
/// edge is registered by paintSelf.
static glm::vec2 floatingMinSize(const UIDockFloatingWindow& window)
{
    if (!window._styleKey.empty()) {
        if (const FFloatingWindowStyle* style =
                resolveThemeStyle<FFloatingWindowStyle>(window, window._styleKey)) {
            return style->minSize;
        }
    }
    return {220.0f, 160.0f};
}

void UIDockFloatingWindow::resizeTo(const glm::vec2& extent)
{
    const glm::vec2 minSize = floatingMinSize(*this);
    _windowRect.extent.x    = std::max(minSize.x, extent.x);
    _windowRect.extent.y    = std::max(minSize.y, extent.y);
}

Rect2D UIDockFloatingWindow::resizeHandleRect(EResizeEdge edge) const
{
    switch (edge) {
    case EResizeEdge::Left:
        return Rect2D{_windowRect.pos, glm::vec2{kResizeThickness, _windowRect.extent.y}};
    case EResizeEdge::Right:
        return Rect2D{glm::vec2{_windowRect.pos.x + _windowRect.extent.x - kResizeThickness,
                                _windowRect.pos.y},
                      glm::vec2{kResizeThickness, _windowRect.extent.y}};
    case EResizeEdge::Top:
        return Rect2D{_windowRect.pos, glm::vec2{_windowRect.extent.x, kResizeThickness}};
    case EResizeEdge::Bottom:
        return Rect2D{glm::vec2{_windowRect.pos.x,
                                _windowRect.pos.y + _windowRect.extent.y - kResizeThickness},
                      glm::vec2{_windowRect.extent.x, kResizeThickness}};
    case EResizeEdge::BottomRight:
        return Rect2D{glm::vec2{_windowRect.pos.x + _windowRect.extent.x - kCornerGripSize,
                                _windowRect.pos.y + _windowRect.extent.y - kCornerGripSize},
                      glm::vec2{kCornerGripSize, kCornerGripSize}};
    }
    return _windowRect;
}

void UIDockFloatingWindow::applyResizeFromEdge(EResizeEdge edge, const glm::vec2& pointerDelta)
{
    const float     right   = _windowRect.pos.x + _windowRect.extent.x;
    const float     bottom  = _windowRect.pos.y + _windowRect.extent.y;
    const glm::vec2 minSize = floatingMinSize(*this);

    switch (edge) {
    case EResizeEdge::Left: {
        const float nextLeft = std::min(_windowRect.pos.x + pointerDelta.x, right - minSize.x);
        _windowRect.pos.x    = nextLeft;
        _windowRect.extent.x = right - nextLeft;
        break;
    }
    case EResizeEdge::Right:
        _windowRect.extent.x = std::max(minSize.x, _windowRect.extent.x + pointerDelta.x);
        break;
    case EResizeEdge::Top: {
        const float nextTop = std::min(_windowRect.pos.y + pointerDelta.y, bottom - minSize.y);
        _windowRect.pos.y   = nextTop;
        _windowRect.extent.y = bottom - nextTop;
        break;
    }
    case EResizeEdge::Bottom:
        _windowRect.extent.y = std::max(minSize.y, _windowRect.extent.y + pointerDelta.y);
        break;
    case EResizeEdge::BottomRight:
        _windowRect.extent.x = std::max(minSize.x, _windowRect.extent.x + pointerDelta.x);
        _windowRect.extent.y = std::max(minSize.y, _windowRect.extent.y + pointerDelta.y);
        break;
    }
}

} // namespace ya
