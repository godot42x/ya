#pragma once

#include "GameEditor/UI/EditorViewportHost.h"

#include <glm/glm.hpp>

namespace ya
{

struct EditorLayer;

/// Retained viewport overlay that routes pointer/keyboard input to ImGuizmo and
/// exposes active/capture state to EditorSurface. Painting is submitted by
/// EditorSurface::presentViewportGizmo after the WidgetTree chrome replay.
class EditorViewportGizmoOverlay final : public IEditorViewportOverlay
{
  public:
    explicit EditorViewportGizmoOverlay(EditorLayer& layer);

    void syncHost(const FEditorViewportHostState& host) override;

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event,
                                                     const glm::vec2& localPoint) override;

    [[nodiscard]] bool wantsPointerCapture() const override;
    [[nodiscard]] bool isActive() const override;

    void syncImGuiIO() const;

  private:
    EditorLayer*             _layer = nullptr;
    FEditorViewportHostState _host{};
    glm::vec2                _windowMousePos{0.0f};
    bool                     _bMouseDown[3] = {false, false, false};
};

} // namespace ya
