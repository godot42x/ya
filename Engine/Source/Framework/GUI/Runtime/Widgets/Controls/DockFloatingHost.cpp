#include "GUI/Widgets/Controls/DockFloatingHost.h"

#include "GUI/Widgets/Controls/DockFloatingWindow.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

UIDockFloatingHost::UIDockFloatingHost(std::string name)
    : UIElement(std::move(name))
{
    // Non-modal: empty areas of the host pass input (and drag-drop) through to
    // the content below, while its child floating windows stay hittable.
    _hitFilter = EWidgetHitFilter::Pass;
    setVisibility(EWidgetVisibility::HitTestInvisible);
}

void UIDockFloatingHost::bindWorkspace(std::shared_ptr<UIDockWorkspace> ws)
{
    _ws = std::move(ws);
    if (_ws) {
        _ws->setFloatingHost(this);
        _ws->setOnFloatingUpdated([this]() { syncFromWorkspace(); });
    }
}

void UIDockFloatingHost::syncFromWorkspace()
{
    if (!_ws) {
        return;
    }
    WidgetTree* tree = getTree();

    // Drop windows whose panel is no longer floating.
    for (auto it = _windows.begin(); it != _windows.end();) {
        const DockPanelId panelId = it->first;
        if (!_ws->isPanelFloating(panelId)) {
            if (tree && it->second) {
                tree->detach(*it->second);
            }
            it = _windows.erase(it);
        }
        else {
            ++it;
        }
    }

    // Create / refresh windows for current floating records.
    for (const auto& record : _ws->floatingWindows()) {
        auto it = _windows.find(record.panelId);
        if (it != _windows.end()) {
            it->second->setWindowRect({record.pos, record.size});
            continue;
        }
        const std::string title = _ws->findPanel(record.panelId)
                                      ? _ws->findPanel(record.panelId)->name : std::string{};
        auto window = std::make_shared<UIDockFloatingWindow>(
            std::format("FloatingWindow{}", record.panelId), record.panelId, title, _ws);
        window->setWindowRect({record.pos, record.size});
        window->_onActivated = [this, panelId = record.panelId]()
        {
            auto found = _windows.find(panelId);
            if (found != _windows.end()) {
                bringToFront(found->second);
            }
        };
        if (tree) {
            tree->attach(*this, window);
        }
        _windows.emplace(record.panelId, window);
    }

    if (tree) {
        tree->invalidateLayout();
    }
    markPaintDirty();
}

void UIDockFloatingHost::bringToFront(const std::shared_ptr<UIDockFloatingWindow>& window)
{
    if (!window) {
        return;
    }
    if (WidgetTree* tree = getTree()) {
        tree->reparent(*this, window);
        tree->invalidateLayout();
    }
}

void UIDockFloatingHost::layout(const Rect2D& parentRect)
{
    layoutAssigned(parentRect);
}

void UIDockFloatingHost::layoutAssigned(const Rect2D& rect)
{
    _hostRect = rect;
    setLayoutRect(rect);
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child && child->participatesInLayout()) {
            child->layoutAssigned(rect);
        }
    }
}

bool UIDockFloatingHost::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    return UIElement::handleInputEvent(event, ctx);
}

} // namespace ya
