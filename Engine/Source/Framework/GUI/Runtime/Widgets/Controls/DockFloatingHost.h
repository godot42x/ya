#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/Controls/DockNode.h"
#include "GUI/Widgets/Controls/DockContext.h"

#include <memory>
#include <unordered_map>

namespace ya
{

struct UIDockFloatingWindow;

/// Popup-layer projection of `FDockContext`'s floating windows.
/// Renders torn-off windows above content and keeps their z-order (an
/// activated / tab-dragged window moves to the top). Empty areas pass input
/// through to the content underneath. The docked tree is projected by
/// `UIDockSpace`, not here.
struct YA_GUI_API UIDockFloatingHost : public UIElement
{
    explicit UIDockFloatingHost(std::string name = "DockFloatingHost");
    /// Unregister the context back-pointer (FDockContext::_floatingHost)
    /// so a context that outlives this widget never hands out a dangling
    /// pointer.
    ~UIDockFloatingHost() override;

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDockFloatingHost>; }

    void bindContext(std::shared_ptr<FDockContext> context);
    /// Reconcile this host's windows with the context's floating records.
    void syncFromContext();
    /// Move a window to the top of the floating z-order.
    void bringToFront(const std::shared_ptr<UIDockFloatingWindow>& window);

    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override
    {
        node["type"] = "canvas";
    }

  private:
    std::shared_ptr<FDockContext> _context;
    std::unordered_map<FDockFloatingWindowId, std::shared_ptr<UIDockFloatingWindow>> _windows;
};

} // namespace ya
