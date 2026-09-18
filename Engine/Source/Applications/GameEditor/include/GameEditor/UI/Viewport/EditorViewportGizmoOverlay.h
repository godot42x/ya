#pragma once

#include "GameEditor/UI/Viewport/EditorViewportGizmoController.h"
#include "GameEditor/UI/Viewport/EditorViewportHost.h"

#include <glm/glm.hpp>

namespace ya
{

/// Retained viewport overlay that routes pointer/keyboard input to the native
/// editor gizmo controller. Rendering is recorded into the viewport compose
/// pass by `EditorViewportGizmoController::recordOverlay()`.
class EditorViewportGizmoOverlay final : public IEditorViewportOverlay
{
  public:
    explicit EditorViewportGizmoOverlay(EditorViewportGizmoController& controller);

    void syncHost(const FEditorViewportHostState& host) override;

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event,
                                                     const glm::vec2& localPoint) override;

    [[nodiscard]] bool wantsPointerCapture() const override;
    [[nodiscard]] bool isActive() const override;

  private:
    EditorViewportGizmoController* _controller = nullptr;
    FEditorViewportHostState       _host{};
};

} // namespace ya
