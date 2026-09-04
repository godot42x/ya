#pragma once

#include "GameEditor/UI/EditorViewportHost.h"

#include <glm/glm.hpp>

namespace ya
{

struct EditorLayer;

/// Retained viewport overlay that routes pointer/keyboard input to the native
/// editor gizmo controller. Rendering is recorded into the viewport compose
/// pass by `EditorLayer::recordViewportGizmoOverlay()`.
class EditorViewportGizmoOverlay final : public IEditorViewportOverlay
{
  public:
    explicit EditorViewportGizmoOverlay(EditorLayer& layer);

    void syncHost(const FEditorViewportHostState& host) override;

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event,
                                                     const glm::vec2& localPoint) override;

    [[nodiscard]] bool wantsPointerCapture() const override;
    [[nodiscard]] bool isActive() const override;

  private:
    EditorLayer*             _layer = nullptr;
    FEditorViewportHostState _host{};
};

} // namespace ya
