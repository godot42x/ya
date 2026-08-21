#pragma once

#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DockNode.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ya
{

struct UITabBar;
struct UIContainer;
struct UIButton;
struct UIDockWorkspace;

/// A single floating dock window (Phase 5). Holds one panel's title bar and
/// content, is positioned absolutely inside a UIDockFloatingHost, and:
///   - dragging its tab (dock-panel payload) re-docks onto a DockSpace or
///     moves the window when released in empty space;
///   - the close button re-docks the panel back to the dock tree's root leaf.
struct YA_GUI_API UIDockFloatingWindow : public UIContainer
{
    explicit UIDockFloatingWindow(std::string name, DockPanelId panelId, std::string title,
                                  std::shared_ptr<UIDockWorkspace> ws);

    [[nodiscard]] DockPanelId getPanelId() const { return _panelId; }
    [[nodiscard]] const Rect2D& getWindowRect() const { return _windowRect; }
    void setWindowRect(const Rect2D& rect) { _windowRect = rect; }
    void resizeTo(const glm::vec2& extent);
    /// Fired when the window is activated (title drag begins). Used by the host
    /// to bring the window to the front of the floating z-order.
    std::function<void()> _onActivated;

    void layout(const Rect2D& parentRect) override;
    void layoutAssigned(const Rect2D& rect) override;
    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override;

    enum class EResizeEdge : uint8_t
    {
        Left,
        Right,
        Top,
        Bottom,
        BottomRight,
    };

    void applyResizeFromEdge(EResizeEdge edge, const glm::vec2& pointerDelta);

  private:

    void beginTabDrag();
    [[nodiscard]] Rect2D resizeHandleRect(EResizeEdge edge) const;

    DockPanelId _panelId = kInvalidDockPanelId;
    std::string _title;
    std::shared_ptr<UIDockWorkspace> _ws;
    Rect2D _windowRect;
    std::optional<glm::vec2> _lastDragPoint;
    std::vector<std::shared_ptr<UIElement>> _resizeHandles;

};

} // namespace ya
