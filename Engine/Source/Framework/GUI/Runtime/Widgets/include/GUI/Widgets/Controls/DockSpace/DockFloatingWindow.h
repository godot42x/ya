#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockDropTarget.h"
#include "GUI/Widgets/Theme.h"

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
struct FDockContext;

/// A floating dock window (Phase 5). Presents one FDockContext floating record as a
/// titled window that may host several tabs (panels). It is positioned
/// absolutely inside a UIDockFloatingHost via the host-owned UICanvasSlot:
///   - `setWindowRect` writes that edge (and seeds it at attach);
///   - dragging any of its tabs (dock-panel payload) projects the dock chooser
///     and either re-docks onto a DockSpace / another floating window or moves
///     the window when released in empty space;
///   - the close button re-docks the active tab back to the dock tree's root.
struct YA_GUI_API UIDockFloatingWindow : public UIElement, public UIStyledWidget<UIDockFloatingWindow, FFloatingWindowStyle>
{
    YA_GUI_AUTHORED_STYLE_IO(FFloatingWindowStyle)

    explicit UIDockFloatingWindow(std::string name, FDockFloatingWindowId floatingId,
                                  std::shared_ptr<FDockContext> context);

    [[nodiscard]] DockPanelId getActivePanelId() const { return _panelId; }
    [[nodiscard]] FDockFloatingWindowId getFloatingId() const { return _floatingId; }
    [[nodiscard]] const Rect2D& getWindowRect() const { return _windowRect; }
    void setWindowRect(const Rect2D& rect);
    void resizeTo(const glm::vec2& extent);
    void commitGeometryToContext(bool notify);
    /// Rebuild the window's tab bar + content to match the context's current
    /// floating record for this window (called by the host on floating updates).
    void refreshFromContext();
    /// Stack/well drop target for this overlay window. Does not consult UIDockSpace.
    [[nodiscard]] std::optional<FDockDropTarget> dropTargetAt(const glm::vec2& logicalPoint,
                                                              DockPanelId sourcePanelId) const;
    /// Fired when the window is activated (title drag begins). Used by the host
    /// to bring the window to the front of the floating z-order.
    std::function<void()> _onActivated;

    void layout(const Rect2D& parentRect) override;
    void onAttached() override;
    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override { node["type"] = "overlay"; }
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

  protected:
    void applyAssignedLayout(const Rect2D& rect) override;

  private:
    friend struct FDockFloatingWindowDropTargetBehavior;
    friend struct FDockFloatingWindowPanelDragBehavior;

    void beginWindowMove();
    void updateWindowMove(const glm::vec2& logicalPoint);
    void rebuildContent();

    FDockFloatingWindowId _floatingId = kInvalidFloatingWindowId;
    DockPanelId _panelId = kInvalidDockPanelId; ///< Active (selected) tab.
    std::string _title;
    std::shared_ptr<FDockContext> _context;
    std::shared_ptr<UITabBar> _tabBar;
    std::shared_ptr<UIContainer> _content;
    std::shared_ptr<UIContainer> _chrome;
    /// Title strip container (the windows grab zone): pressing + dragging its
    /// empty area starts the dock-panel drag (dock on a DockSpace, move on
    /// empty space) — mirrors the tab-strip drag.
    std::shared_ptr<UIContainer> _header;
    UIElement*                   _hideAffordance = nullptr;
    /// Transient title-drag arm state (mirrors UITabBar's 6px threshold).
    bool      _bTitlePressed = false;
    bool      _bTitleMoving  = false;
    /// True while an active dock-panel drag session owns this window. While set,
    /// the header's window-move path is bypassed so the drag session is the sole
    /// mover (otherwise both would double-move the window and fight for the
    /// pointer).
    bool      _bDockDragging = false;
    glm::vec2 _titlePressPoint{0.0f, 0.0f};
    Rect2D _windowRect;
    std::optional<glm::vec2> _lastDragPoint;
    std::vector<std::shared_ptr<UIElement>> _resizeHandles;

};

} // namespace ya
