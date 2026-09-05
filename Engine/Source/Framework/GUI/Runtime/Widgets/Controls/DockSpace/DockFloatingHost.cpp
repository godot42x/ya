#include "GUI/Widgets/Controls/DockSpace/DockFloatingHost.h"

#include "GUI/Widgets/Controls/DockSpace/DockFloatingWindow.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

UIDockFloatingHost::UIDockFloatingHost(std::string name)
    : UIElement(std::move(name))
{
    // Non-modal: empty areas of the host pass input (and drag-drop) through to
    // the content below, while its child floating windows stay hittable.
    installLayout(std::make_unique<UICanvasLayout>());
    _hitFilter = EWidgetHitFilter::Pass;
    setVisibility(EWidgetVisibility::HitTestInvisible);
}

UIDockFloatingHost::~UIDockFloatingHost()
{
    if (_context && _context->floatingHost() == this) {
        _context->setFloatingHost(nullptr);
    }
}

void UIDockFloatingHost::bindContext(std::shared_ptr<FDockContext> context)
{
    if (_context && _context != context && _context->floatingHost() == this) {
        _context->setFloatingHost(nullptr);
    }
    _context = std::move(context);
    if (_context) {
        _context->setFloatingHost(this);
        // Weak self: the context may fire floating-updated after this widget
        // is destroyed (another host re-binds the same context), so the
        // callback must never dereference a stale 'this'.
        std::weak_ptr<UIDockFloatingHost> weakSelf =
            std::static_pointer_cast<UIDockFloatingHost>(shared_from_this());
        _context->setOnFloatingUpdated([weakSelf]()
        {
            if (auto self = weakSelf.lock()) {
                self->syncFromContext();
            }
        });
    }
}

void UIDockFloatingHost::syncFromContext()
{
    if (!_context) {
        return;
    }
    WidgetTree* tree = getTree();

    // Drop windows whose floating record no longer exists.
    for (auto it = _windows.begin(); it != _windows.end();) {
        if (!_context->findFloatingById(it->first)) {
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
    for (const auto& record : _context->floatingWindows()) {
        auto it = _windows.find(record.id);
        if (it != _windows.end()) {
            it->second->setWindowRect({record.pos, record.size});
            it->second->refreshFromContext();
            continue;
        }
        auto window = std::make_shared<UIDockFloatingWindow>(
            std::format("FloatingWindow{}", record.id), record.id, _context);
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
            window->setWindowRect({record.pos, record.size});
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

} // namespace ya
