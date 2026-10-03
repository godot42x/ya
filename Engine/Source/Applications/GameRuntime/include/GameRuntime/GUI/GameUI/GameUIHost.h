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

#include "GameRuntime/GUI/GameUI/IGameUIBehaviorRuntime.h"
#include "GameRuntime/GUI/GameUI/IGameUIController.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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

    /// Bind the current game presentation area. `viewportPx` is the view's
    /// device-pixel rect. `framebufferScale` is that view's pixel density
    /// (device pixels per logical point), so the logical canvas is
    /// `viewportPx / framebufferScale`. It is not the host `renderScale`.
    ///
    /// The fit factor (viewport vs reference resolution) rides the tree's
    /// DPI axis, not uiScale: fonts re-rasterize at the final pixel size.
    /// It is `min(logical / reference)` with no floor. A degenerate
    /// (non-finite or non-positive) fit falls back to 1. Integer font raster
    /// sizes are the cache quantum, so the fit itself is not snapped.
    /// Bitmap text still has its own 9px raster floor in `planTextRaster`;
    /// below that, glyphs can overflow the slot they were authored for.
    /// Window and viewport minimum sizes are what keep a presentation out
    /// of that range. There is no min UI scale.
    void setPresentation(const Rect2D& viewportPx, const glm::vec2& framebufferScale);

    /// Reference resolution for scale-to-fit. Zero on either axis means scale 1
    /// (the tree lays out in viewport logical pixels). Takes effect on the next
    /// setPresentation.
    void setReferenceResolution(glm::uvec2 resolution);
    [[nodiscard]] glm::uvec2 referenceResolution() const { return _referenceResolution; }
    /// The fit factor actually in effect. Pointer mapping and the tree DPI
    /// both read it.
    [[nodiscard]] float referenceScale() const { return _referenceScale; }

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

    /// Turns authored behaviour specs of mounted entries into live behaviours
    /// and starts them in UILogic. Null (the default) mounts documents with
    /// their specs inert. Replacing it remounts the presented scene.
    void setBehaviorRuntime(std::unique_ptr<IGameUIBehaviorRuntime> runtime);
    [[nodiscard]] IGameUIBehaviorRuntime* getBehaviorRuntime() const { return _behaviorRuntime.get(); }

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
    /// Lay the tree out now if it is dirty. UILogic runs before this frame's
    /// layout, so code that must read this frame's geometry asks for it.
    void layoutNow();

    // === Timers (host clock) ===
    /// `fire` runs once `delaySeconds` of this host's clock have passed, then
    /// every `intervalSeconds` while it returns true (0 = once). Timers run in
    /// update() before the tree tick, whether or not anything is visible; a
    /// timer that falls behind fires once per update, not in a burst.
    uint64_t addTimer(const void* owner, float delaySeconds, float intervalSeconds, std::function<bool()> fire);
    void     cancelTimer(uint64_t timerId);
    void     cancelTimersOf(const void* owner);

    // === Mounted entries ===
    /// Root of the mounted entry `entryId`, or null.
    [[nodiscard]] UIElementRef findEntryRoot(std::string_view entryId) const;
    /// The mounted entry root that `widget` belongs to (itself or an ancestor), or null.
    [[nodiscard]] UIElement* entryRootOf(const UIElement& widget) const;
    /// Widget named `name` inside the entry rooted at `entryRoot` (index rebuilt
    /// after structural changes). An ambiguous name returns the first in tree
    /// order and warns once.
    [[nodiscard]] UIElementRef findInEntry(const UIElement& entryRoot, std::string_view name);

    // === Structural changes (applied by flushStructuralChanges) ===
    /// Instantiate `documentPath` now; attach it under `parent` at the next
    /// flush and activate its behaviour specs with the parent's entry. Until
    /// then the subtree is pending: detached but safe to configure. Null when
    /// the document does not resolve.
    [[nodiscard]] UIElementRef queueSpawn(std::string_view documentPath, UIElement& parent);
    /// Detach `widget` with its subtree at the next flush. A pending spawn is
    /// cancelled right away.
    void queueDestroy(UIElement& widget);
    /// `widget` belongs to a subtree queued by queueSpawn and not attached yet.
    [[nodiscard]] bool isPendingSpawn(const UIElement& widget) const;
    /// StructuralFlush: queued destroys, then spawns whose parent is still in
    /// the tree (a spawn under a destroyed parent is dropped).
    void flushStructuralChanges();

    /// Remember which content-layer root came from which scene entry. Replaces
    /// the previous map. Called by the controller after a mount.
    void setMountedRoots(std::vector<std::pair<std::string, std::weak_ptr<UIElement>>> roots);

    void clearMountedRoots();

    /// `widgetName` matches `UIElement::_name` on the entry root or a descendant.
    /// Returns false when the entry or the widget is not in the mounted tree.
    bool setMountedText(std::string_view entryId, std::string_view widgetName, const std::string& text);
    bool setMountedVisible(std::string_view entryId, std::string_view widgetName, bool visible);

  private:
    struct FMountedEntry
    {
        std::string                                             entryId;
        std::weak_ptr<UIElement>                                root;
        std::unordered_map<std::string, std::weak_ptr<UIElement>> names;
        std::unordered_map<std::string, bool>                   ambiguous; ///< name -> already warned
        bool                                                    bIndexDirty = false;
    };
    struct FTimer
    {
        const void*           owner;
        double                due;
        float                 interval;
        std::function<bool()> fire;
    };
    struct FTimerDue
    {
        double   due;
        uint64_t id;

        /// Heap order for a min-heap on (due, id).
        static bool firesAfter(const FTimerDue& a, const FTimerDue& b)
        {
            return a.due != b.due ? a.due > b.due : a.id > b.id;
        }
    };
    struct FPendingSpawn
    {
        UIElementRef             widget;
        std::weak_ptr<UIElement> parent;
    };

    void advanceTimers(float deltaSeconds);
    void scheduleTimer(uint64_t id, double due);
    void compactTimerQueue();
    void addEntry(std::string entryId, const UIElementRef& root);
    void mountWorldWidget(const UIElementRef& widget);
    [[nodiscard]] UIElement* findMountedWidget(std::string_view entryId, std::string_view widgetName) const;
    [[nodiscard]] FMountedEntry* entryFor(const UIElement& entryRoot);
    /// Declared before `_tree`: behaviours it created live on tree widgets and
    /// must be torn down before it.
    std::unique_ptr<IGameUIBehaviorRuntime> _behaviorRuntime;
    WidgetTree                     _tree;
    std::unique_ptr<IGameUIController> _controller;
    UIDocumentStore*               _documents = nullptr;
    Scene*                         _mountedScene = nullptr;
    Rect2D                         _viewportPx{};
    glm::vec2                      _framebufferScale = {1.0f, 1.0f};
    glm::uvec2                     _referenceResolution{0, 0};
    float                          _referenceScale = 1.0f;
    EUIUpdateClock                 _updateClock = EUIUpdateClock::RealTime;
    std::vector<FMountedEntry>     _entries;
    std::vector<FPendingSpawn>     _pendingSpawns;
    std::vector<std::weak_ptr<UIElement>> _pendingDestroys;
    std::unordered_map<uint64_t, FTimer> _timers;
    /// Min-heap on (due, id): due timers fire earliest first, ties in creation
    /// order. A cancelled timer's entry stays until it surfaces and is skipped.
    std::vector<FTimerDue>         _timerQueue;
    uint64_t                       _nextTimerId = 1;
    double                         _clockSeconds = 0.0;
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
/// Each attached entry's behaviour specs go through `activator`; the editor
/// preview passes null so authoring never runs behaviours.
/// Errors go to `onError` (entryId included); a null sink logs through
/// YA_CORE_ERROR.
[[nodiscard]] YA_GAME_RUNTIME_API std::vector<FSceneUIMount>
mountSceneAutoMountEntries(Scene&                                       scene,
                           WidgetTree&                                  tree,
                           UIDocumentStore*                             documents,
                           IUIBehaviorActivator*                        activator,
                           const std::function<void(std::string_view)>& onError = {});

} // namespace ya
