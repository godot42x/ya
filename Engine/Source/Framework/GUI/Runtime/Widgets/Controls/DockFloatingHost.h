#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/Controls/DockNode.h"

#include <memory>
#include <unordered_map>

namespace ya
{

struct UIDockWorkspace;
struct UIDockFloatingWindow;

/// Non-modal floating-window host (Phase 5). Lives on the Popup layer, renders
/// the workspace's floating windows above the content, and keeps their
/// z-order (an activated / tab-dragged window moves to the top). Empty areas
/// pass input through to the content underneath.
struct YA_GUI_API UIDockFloatingHost : public UIElement
{
    explicit UIDockFloatingHost(std::string name = "DockFloatingHost");

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDockFloatingHost>; }

    void bindWorkspace(std::shared_ptr<UIDockWorkspace> ws);
    /// Reconcile this host's windows with the workspace's floating records.
    void syncFromWorkspace();
    /// Move a window to the top of the floating z-order.
    void bringToFront(const std::shared_ptr<UIDockFloatingWindow>& window);

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;

  private:
    std::shared_ptr<UIDockWorkspace> _ws;
    std::unordered_map<DockPanelId, std::shared_ptr<UIDockFloatingWindow>> _windows;
    Rect2D _hostRect;
};

} // namespace ya
