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

#include "Core/Api.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include "Host/GUI/GameUI/IGameUIController.h"
#include "Host/GUI/GameUI/UIDocumentResolver.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct Scene;

struct YA_HOST_API GameUIHost
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

    /// Shared `.yaui` resolver (one resolve entry for Editor/PIE/Runtime).
    [[nodiscard]] UIDocumentResolver& getDocumentResolver() { return _documentResolver; }

    /// The host's tree is only presented while a scene is mounted.
    [[nodiscard]] Scene* getMountedScene() const { return _mountedScene; }

    /// Unmount + remount the currently presented scene (picks up resolver
    /// changes after a document reload).
    void reloadMountedSceneUI();

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
    UIDocumentResolver             _documentResolver;
    Scene*                         _mountedScene = nullptr;
    Rect2D                         _viewportPx{};
    glm::vec2                      _framebufferScale = {1.0f, 1.0f};
};

/// Strong texture resolver shared by every Game UI snapshot build context
/// (runtime host, editor canvas preview, UI designer). The snapshot holds
/// the returned shared_ptr, so draw resources survive queue submit even if
/// the asset cache unloads/clears/reloads the texture afterwards.
[[nodiscard]] YA_HOST_API std::shared_ptr<Texture> resolveGameUITexture(const std::string& assetPath);

/// Instantiate + attach all autoMount SceneWidgetEntries of `scene` into
/// `tree`'s content layer (entry zOrder -> widget zOrder, entry overrides
/// applied). Single mount path shared by the default controller (keeps the
/// returned attachments for scene-lifecycle tracking) and the editor canvas
/// preview (stateless per-frame rebuild, drops them after the snapshot).
/// Errors go to `onError` (entryId + documentPath included); a null sink
/// logs through YA_CORE_ERROR.
[[nodiscard]] YA_HOST_API std::vector<WidgetAttachment>
mountSceneAutoMountEntries(Scene&                                       scene,
                           WidgetTree&                                  tree,
                           UIDocumentResolver&                          resolver,
                           const std::function<void(std::string_view)>& onError = {});

} // namespace ya
