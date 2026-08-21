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

    // Header row: draggable title tab + close button.
    auto header = std::make_shared<UIContainer>(std::format("{}_Header", _name));
    header->setDirection(EWidgetBoxLayout::Horizontal);
    header->setSpacing(0.0f);

    auto bar = std::make_shared<UITabBar>(std::format("{}_TabBar", _name));
    bar->_bDraggableTabs = true;
    bar->addTab(_title);
    bar->syncSelectedTab(0);
    bar->_onTabDragBegin = [this](int, const std::string&) { beginTabDrag(); };
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
}

void UIDockFloatingWindow::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, {0.16f, 0.17f, 0.22f, 0.97f}, nullptr);
    builder.addRectOutline(_layoutRect, {0.30f, 0.33f, 0.40f, 1.0f}, 1.0f);
}

void UIDockFloatingWindow::beginTabDrag()
{
    if (_onActivated) {
        _onActivated();
    }
    if (WidgetTree* tree = getTree()) {
        DragSessionObserver observer;
        observer.onTargetChanged = [](std::string_view, std::string_view) {};
        observer.onFinished = [this](EDragFinishResult result, const glm::vec2& logicalPoint, std::string_view)
        {
            if (result == EDragFinishResult::NoTarget) {
                // Not re-docked: relocate the window to where it was released.
                _windowRect.pos = logicalPoint;
                if (WidgetTree* t = getTree()) t->invalidateLayout();
            }
        };
        tree->beginDrag(this, std::string(UIDockSpace::kDockPanelPayload) + std::to_string(_panelId),
                        _title, std::move(observer));
    }
}

bool UIDockFloatingWindow::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    return UIContainer::handleInputEvent(event, ctx);
}

void UIDockFloatingWindow::clearTransientInputState()
{
}

} // namespace ya
