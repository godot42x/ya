#pragma once

#include "Core/Common/Types.h"
#include "Core/Event.h"
#include "GUI/Widgets/WidgetTree.h"

#include <glm/glm.hpp>
#include <memory>

namespace ya
{

/// Snapshot of the retained viewport host each frame. Overlays read this in
/// viewport-local coordinates (origin = top-left of the viewport image rect).
struct FEditorViewportHostState
{
    Rect2D    widgetRect{};
    glm::vec2 extent{0.0f};
    bool      bHovered  = false;
    bool      bFocused  = false;
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};

/// Retained viewport overlay contract. Implementations (gizmo, selection
/// marquee, drag-drop preview) consume host state and may capture pointer input
/// while active. Painting may remain ImGui-backed until a Render2D bridge lands.
struct IEditorViewportOverlay
{
    virtual ~IEditorViewportOverlay() = default;

    virtual void syncHost(const FEditorViewportHostState& host) = 0;

    /// `localPoint` is relative to `host.widgetRect.pos`.
    [[nodiscard]] virtual EWidgetRouteResult dispatchEvent(const Event& event,
                                                           const glm::vec2& localPoint) = 0;

  [[nodiscard]] virtual bool wantsPointerCapture() const { return false; }
  [[nodiscard]] virtual bool isActive() const { return false; }
};

/// Owns the single viewport overlay installed by editor tooling.
class EditorViewportOverlayHost
{
  public:
    void setOverlay(std::shared_ptr<IEditorViewportOverlay> overlay);
    void clearOverlay();

    void syncHost(const FEditorViewportHostState& host);

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& localPoint);
    [[nodiscard]] bool wantsPointerCapture() const;
    [[nodiscard]] bool isActive() const;

  private:
    std::shared_ptr<IEditorViewportOverlay> _overlay;
    FEditorViewportHostState                _host{};
};

} // namespace ya
