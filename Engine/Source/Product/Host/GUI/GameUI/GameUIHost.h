#pragma once

// ============================================================================
// GameUIHost - Product/Host adapter for the presentation WidgetTree
// (ui-widget-tree-refactor Phase 3).
//
// Owns the single live WidgetTree of the current game presentation area and
// resolves active worlds/scenes to it:
//   - presentation context: viewport rect (window px), framebuffer scale and
//     logical extent map window coordinates <-> tree-local logical pixels;
//   - scene lifecycle: activate -> controller mounts autoMount entries;
//     switch/destroy -> controller unmounts them;
//   - input: window events dispatch into the tree (topmost-first);
//   - frame: buildSnapshot() produces the immutable packet consumed by the
//     compose pass (recording never touches the tree).
//
// The interface does not assume a singleton: future multi-window / split-
// screen hosts own one GameUIHost (and WidgetTree) per presentation area.
// ============================================================================

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include "Host/GUI/GameUI/IGameUIController.h"

#include <memory>

namespace ya
{

struct Scene;

struct GameUIHost
{
    GameUIHost();
    ~GameUIHost();

    GameUIHost(const GameUIHost&)            = delete;
    GameUIHost& operator=(const GameUIHost&) = delete;

    /// Bind the current game presentation area. `viewportPx` is the viewport
    /// rect in window pixels; `framebufferScale` maps logical UI pixels to
    /// window pixels (1 for a 1:1 window scale).
    void setPresentation(const Rect2D& viewportPx, const glm::vec2& framebufferScale);

    [[nodiscard]] WidgetTree& getTree() { return _tree; }
    [[nodiscard]] const WidgetTree& getTree() const { return _tree; }

    /// Replace the default scene<->tree policy (project hook).
    void setController(std::unique_ptr<IGameUIController> controller);
    [[nodiscard]] IGameUIController* getController() const { return _controller.get(); }

    /// The host's tree is only presented while a scene is mounted.
    [[nodiscard]] Scene* getMountedScene() const { return _mountedScene; }

    // === Scene lifecycle ===
    void onSceneActivated(Scene& scene);
    void onSceneDeactivated(Scene& scene);

    // === Game-layer semantics ===
    /// "Join this world's Game UI": resolve `world` through the controller and
    /// attach the widget to the content layer. Explicit world, no ambiguity.
    [[nodiscard]] WidgetAttachment addToWorld(Scene& world, const UIElementRef& widget);

    // === Input ===
    /// Dispatch a window-coordinate event into the tree (top-left origin,
    /// Y down). Returns the route result; the caller decides gameplay
    /// fallback when not exclusively consumed.
    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& windowPoint);

    // === Frame ===
    /// Layout + paint into an immutable snapshot for this frame's compose.
    [[nodiscard]] UIFrameSnapshot buildSnapshot();

  private:
    WidgetTree                     _tree;
    std::unique_ptr<IGameUIController> _controller;
    Scene*                         _mountedScene = nullptr;
    Rect2D                         _viewportPx{};
    glm::vec2                      _framebufferScale = {1.0f, 1.0f};
};

} // namespace ya
