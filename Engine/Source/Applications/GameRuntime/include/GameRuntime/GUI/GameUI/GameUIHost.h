#pragma once

// ============================================================================
// GameUIHost - GameRuntime adapter for the presentation WidgetTree
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

#include "GUI/Widgets/GuiTextureCatalog.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include "GameRuntime/GUI/GameUI/IGameUIController.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct Scene;
struct UIDocumentStore;

/// Which clock a hosted UI tree reads. A single `dt` parameter could not say
/// this: a game HUD should freeze with the game, while a pause menu has to keep
/// animating on the frame the game stopped. The app owns the pause decision and
/// the caller knows which kind of UI this tree is, so the choice is an argument
/// at the update site rather than a default baked into the host.
enum class EUIUpdateClock : uint8_t
{
    /// Game time. Zero while the game is paused, so gameplay UI holds still.
    GameTime,
    /// Wall-clock frame time. Keeps running while the game is paused, which is
    /// what a pause menu / loading overlay needs.
    RealTime,
};

/// Both clocks for one frame. Carrying them together keeps the pause decision in
/// one place: the caller computes them once and each tree reads the one it
/// asked for.
struct FUIFrameClock
{
    /// Frame delta with the game's pause applied. 0 while paused.
    float gameDelta = 0.0f;
    /// Frame delta untouched by pause.
    float realDelta = 0.0f;

    [[nodiscard]] float forClock(EUIUpdateClock clock) const
    {
        return clock == EUIUpdateClock::RealTime ? realDelta : gameDelta;
    }
};

struct YA_GAME_RUNTIME_API GameUIHost
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

    /// Document table used to resolve SceneWidgetEntry::documentPath. Owned by
    /// the application; a host without one reports entry mount errors instead
    /// of guessing a path.
    void setDocumentStore(UIDocumentStore* documents) { _documents = documents; }
    [[nodiscard]] UIDocumentStore* getDocumentStore() const { return _documents; }

    /// The host's tree is only presented while a scene is mounted.
    [[nodiscard]] Scene* getMountedScene() const { return _mountedScene; }

    /// Unmount + remount the currently presented scene (picks up entry/document
    /// changes after an edit).
    void reloadMountedSceneUI();

    // === Scene lifecycle ===
    void onSceneActivated(Scene& scene);
    void onSceneDeactivated(Scene& scene);

    // === Game-layer semantics ===
    /// "Join this world's Game UI": resolve `world` through the controller and
    /// attach the widget to the content layer. Explicit world, no ambiguity.
    [[nodiscard]] WidgetAttachment addToWorld(Scene& world, const UIElementRef& widget);
    [[nodiscard]] WidgetAttachment addToWorld(Scene& world,
                                              const UIElementRef& widget,
                                              const FCanvasSlotArgs& args);

    // === Input ===
    /// Dispatch a window-coordinate event into the tree (top-left origin,
    /// Y down). Returns the route result; the caller decides gameplay
    /// fallback when not exclusively consumed.
    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& windowPoint);

    // === Frame ===
    /// Advance the mounted tree's frame-driven state (behaviours, tweens,
    /// self-refreshing widgets). Separate from buildSnapshot, which only lays
    /// out and paints: a widget that animates needs this call, and recording
    /// must never be the thing that ticks it.
    ///
    /// Advance this tree by the delta its declared policy selects. Which clock
    /// is a property of the host, not of the call site: the caller knows the
    /// frame's two deltas, the host knows what kind of UI it carries.
    void update(const FUIFrameClock& clock);
    /// The clock this host's tree follows. Default RealTime keeps a pause menu
    /// and any always-on overlay alive while the game is paused, which is what
    /// the tree did before the choice was named. A pure gameplay HUD should set
    /// GameTime so it freezes with the game.
    ///
    /// Host-wide on purpose: today one host carries one presentation tree, so a
    /// per-subtree clock would be machinery with no second user. When a pause
    /// menu and a HUD must run on different clocks at the same time, that is the
    /// point to split the tree (or add a per-widget clock), not to guess here.
    [[nodiscard]] EUIUpdateClock updateClock() const { return _updateClock; }
    void setUpdateClock(EUIUpdateClock clock) { _updateClock = clock; }
    /// Layout + paint into an immutable snapshot for this frame's compose.
    [[nodiscard]] UIFrameSnapshot buildSnapshot();

    /// Called when a mounted button with a non-empty action name is clicked.
    /// The widget stores the name; this host is the only place that turns it
    /// into a gameplay callback.
    void setUiActionHandler(std::function<void(std::string_view action)> handler);

    /// Remember which content-layer root came from which scene entry. Replaces
    /// the previous map. Called by the controller after a mount.
    void setMountedRoots(std::vector<std::pair<std::string, std::weak_ptr<UIElement>>> roots);

    void clearMountedRoots();

    /// `widgetName` matches `UIElement::_name` on the entry root or a descendant.
    /// Returns false when the entry or the widget is not in the mounted tree.
    bool setMountedText(std::string_view entryId, std::string_view widgetName, const std::string& text);
    bool setMountedVisible(std::string_view entryId, std::string_view widgetName, bool visible);

  private:
    void bindMountedButtonActions();
    [[nodiscard]] UIElement* findMountedWidget(std::string_view entryId, std::string_view widgetName) const;
    WidgetTree                     _tree;
    std::unique_ptr<IGameUIController> _controller;
    UIDocumentStore*               _documents = nullptr;
    Scene*                         _mountedScene = nullptr;
    Rect2D                         _viewportPx{};
    glm::vec2                      _framebufferScale = {1.0f, 1.0f};
    EUIUpdateClock                 _updateClock = EUIUpdateClock::RealTime;
    std::function<void(std::string_view)> _uiActionHandler;
    std::vector<std::pair<std::string, std::weak_ptr<UIElement>>> _mountedRoots;
};

/// One auto-mounted scene entry: the authoring id plus the content-layer attachment.
struct YA_GAME_RUNTIME_API FSceneUIMount
{
    std::string      entryId;
    WidgetAttachment attachment;
};

/// Lookup-only Game UI texture helper (cache hit / miss). Async load and
/// per-path notify live on `gameUITextureSource()` + WidgetTree catalog.
[[nodiscard]] YA_GAME_RUNTIME_API std::shared_ptr<Texture> resolveGameUITexture(const std::string& assetPath);

/// Process-wide AssetManager adapter. Product trees call
/// `tree.setTextureSource(&gameUITextureSource())` once; paint never links
/// AssetManager into a GUI closure.
[[nodiscard]] YA_GAME_RUNTIME_API IGuiTextureSource& gameUITextureSource();

/// Instantiate + attach all autoMount SceneWidgetEntries of `scene` into
/// `tree`'s content layer (entry zOrder -> widget zOrder, entry overrides
/// applied). Single mount path shared by the default controller (keeps the
/// returned attachments for scene-lifecycle tracking) and the editor canvas
/// preview (stateless per-frame rebuild, drops them after the snapshot).
/// Documents come from `documents`; a null store (or an unresolvable path) is
/// reported through `onError` and mounts nothing for that entry.
/// Errors go to `onError` (entryId included); a null sink logs through
/// YA_CORE_ERROR.
[[nodiscard]] YA_GAME_RUNTIME_API std::vector<FSceneUIMount>
mountSceneAutoMountEntries(Scene&                                       scene,
                           WidgetTree&                                  tree,
                           UIDocumentStore*                             documents,
                           const std::function<void(std::string_view)>& onError = {});

} // namespace ya
