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

UIDockFloatingHost::~UIDockFloatingHost()
{
    if (_ws && _ws->floatingHost() == this) {
        _ws->setFloatingHost(nullptr);
    }
}

void UIDockFloatingHost::bindWorkspace(std::shared_ptr<UIDockWorkspace> ws)
{
    if (_ws && _ws != ws && _ws->floatingHost() == this) {
        _ws->setFloatingHost(nullptr);
    }
    _ws = std::move(ws);
    if (_ws) {
        _ws->setFloatingHost(this);
        // Weak self: the workspace may fire floating-updated after this widget
        // is destroyed (another host re-binds the same workspace), so the
        // callback must never dereference a stale 'this'.
        std::weak_ptr<UIDockFloatingHost> weakSelf =
            std::static_pointer_cast<UIDockFloatingHost>(shared_from_this());
        _ws->setOnFloatingUpdated([weakSelf]()
        {
            if (auto self = weakSelf.lock()) {
                self->syncFromWorkspace();
            }
        });
    }
}

void UIDockFloatingHost::syncFromWorkspace()
{
    if (!_ws) {
        return;
    }
    WidgetTree* tree = getTree();

    // Drop windows whose floating record no longer exists.
    for (auto it = _windows.begin(); it != _windows.end();) {
        if (!_ws->findFloatingById(it->first)) {
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
        auto it = _windows.find(record.id);
        if (it != _windows.end()) {
            it->second->setWindowRect({record.pos, record.size});
            it->second->refreshFromWorkspace();
            continue;
        }
        auto window = std::make_shared<UIDockFloatingWindow>(
            std::format("FloatingWindow{}", record.id), record.id, _ws);
        window->setWindowRect({record.pos, record.size});
        window->_onActivated = [this, floatingId = record.id]()
        {
            auto found = _windows.find(floatingId);
            if (found != _windows.end()) {
                bringToFront(found->second);
            }
        };
        if (tree) {
            tree->attach(*this, window);
        }
        _windows.emplace(record.id, window);
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
