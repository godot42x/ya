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

UIDockFloatingWindow::UIDockFloatingWindow(std::string name, DockPanelId panelId, std::string title,
                                           std::shared_ptr<UIDockWorkspace> ws)
    : UIContainer(std::move(name))
    , _panelId(panelId)
    , _title(std::move(title))
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

    auto bar = std::make_shared<UITabBar>(std::format("{}_TabBar", _name));
    bar->_bDraggableTabs = true;
    bar->addTab(_title);
    bar->syncSelectedTab(0);
    bar->_onTabDragBegin = [this](int, const std::string&) { beginDockDrag(); };
    header->addDetachedChild(bar);

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

    auto content = std::make_shared<UIContainer>(std::format("{}_Content", _name));
    content->setClipChildren(true);
    content->setStretchLastChild(true);
    addDetachedChild(content);
    setStretchLastChild(true);

    if (_ws) {
        if (const auto* panel = _ws->findPanel(_panelId)) {
            content->addDetachedChild(panel->widget);
        }
    }

    for (const EResizeEdge edge : {EResizeEdge::Left, EResizeEdge::Right, EResizeEdge::Top,
                                   EResizeEdge::Bottom, EResizeEdge::BottomRight}) {
        auto handle = std::make_shared<FResizeHandle>(this, edge);
        _resizeHandles.push_back(handle);
        addDetachedChild(handle);
    }

    _windowRect = {glm::vec2{120.0f, 120.0f}, glm::vec2{360.0f, 260.0f}};
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
        DragSessionObserver observer;
        observer.onMove = [this](const std::string&, const glm::vec2& logicalPoint, std::string_view)
        {
            if (!_lastDragPoint) {
                _lastDragPoint = logicalPoint;
                return;
            }
            const glm::vec2 delta = logicalPoint - *_lastDragPoint;
            _lastDragPoint = logicalPoint;
            _windowRect.pos += delta;
            if (WidgetTree* t = getTree()) {
                t->invalidateLayout();
            }
        };
        observer.onTargetChanged = [](std::string_view, std::string_view) {};
        observer.onFinished = [this](EDragFinishResult, const glm::vec2&, std::string_view)
        {
            _lastDragPoint.reset();
        };
        tree->beginDrag(this, std::string(UIDockSpace::kDockPanelPayload) + std::to_string(_panelId),
                        _title, std::move(observer), /*bShowGhost=*/false,
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

    return UIContainer::handleInputEvent(event, ctx);
}

void UIDockFloatingWindow::clearTransientInputState()
{
    _bTitlePressed = false;
    _bTitleMoving = false;
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
