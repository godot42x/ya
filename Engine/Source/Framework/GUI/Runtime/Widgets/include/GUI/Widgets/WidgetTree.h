#pragma once

// ============================================================================
// WidgetTree - the single live visual tree for one Game UI presentation
// context (ui-widget-tree-refactor Phase 1).
//
// Ownership: the tree owns its internal root and the stable system layers;
// visual parents hold strong refs to their children; children point back with
// raw (non-owning) pointers. Attaching enforces a single visual parent;
// reparenting must be explicit.
//
// Layers (paint order bottom -> top):
//   Content  - "join this world's Game UI" default mount point (zOrder sorted)
//   Popup    - framework popup/menu layer (above all project content)
//   Tooltip  - framework tooltip layer
//   DragIme  - drag / IME / debug overlays
// Project code cannot override system layers through ordinary child zOrder.
// ============================================================================

#include "GUI/Binding/Reactive.h"
#include "GUI/Layout/UICanvasLayout.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/GuiTextureCatalog.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetAttachment.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ya
{

struct UITheme;

/// Canvas layout host used by the tree root and system layers. `ui::attach`
/// uses `SlotArgs` so a layer parent only accepts canvas slot builders.
struct YA_GUI_API UICanvasRoot : UIElement
{
    using SlotArgs = FCanvasSlotArgs;

    explicit UICanvasRoot(std::string name);
};

/// Result of one game-UI event route pass. Named distinctly from the legacy
/// EWidgetRouteResult while the old GUI/Scene module still exists (Phase 6 merge).
enum class EWidgetRouteResult : uint8_t
{
    NotHandled,       // no widget consumed the event
    HandledPass,      // UI responded but the event also falls through to the game
    HandledExclusive, // a Stop widget consumed the event; the game must not receive it
};

enum class EWidgetRoutePolicy : uint8_t
{
    None,
    HitTest,
    PointerCapture,
    Focus,
    TabTraversal,
    DragSession,
    Popup,
    Modal,
};

enum class EWidgetLayoutInvalidation : uint8_t
{
    Arrange = 1,
    Measure = 2,
    Structure = 4,
};

/// Tree-owned pointer state. The host supplies the coordinate on each native
/// pointer event; consumers read the retained value instead of forwarding
/// stale business-level mouse positions between controls.
struct WidgetPointerState
{
    glm::vec2 logicalPoint = {0.0f, 0.0f};
    bool      bKnown       = false;
};

/// Terminal state of a WidgetTree drag session. This is intentionally a
/// framework-level result: consumers such as DockSpace, TreeView, and
/// application tooling can interpret the same lifecycle without the tree
/// knowing their payload semantics.
enum class EDragFinishResult : uint8_t
{
    Dropped,
    NoTarget,
    Cancelled,
};

/// Synchronous observer for a drag session owned by WidgetTree. `onMove` is
/// called for every pointer move (including moves that remain over the same
/// target); `onTargetChanged` is called only when the accepting target
/// changes. Names are copied by the tree and are valid for the duration of
/// each callback, so observers never retain raw widget pointers across a
/// callback.
struct DragSessionObserver
{
    std::function<void(const UIDragDropOperation& operation,
                       const glm::vec2& logicalPoint,
                       std::string_view targetName)> onMove;
    std::function<void(std::string_view previousTarget,
                       std::string_view currentTarget)> onTargetChanged;
    std::function<void(EDragFinishResult result,
                       const glm::vec2& logicalPoint,
                       std::string_view targetName)> onFinished;
};

/// Per-frame performance counters for the most recent buildSnapshot(). A
/// lightweight observable surface for the reactive-binding perf pipeline:
/// layout/paint wall time, the number of widgets walked by paint, and the
/// draw-item count of the resulting snapshot. Resolved per buildSnapshot call.
struct GuiPerfStats
{
    float    layoutMS        = 0.0f; // layout() wall time (0 when layout was clean)
    float    paintMS         = 0.0f; // paint walk wall time
    uint32_t paintedWidgets  = 0;    // widgets that participated in the paint walk
    uint32_t rebuiltWidgets  = 0;    // widgets that re-ran paintSelf (dirty)
    uint32_t drawItems       = 0;    // draw items in the resulting snapshot
    uint64_t layoutSkippedWidgets = 0;
    // Invalidation diagnostics (GI-001): cumulative clean->dirty transition
    // counts observed by this tree. A "transition" is a 0->1 dirty edge, so
    // repeated marks of an already-dirty widget are not double-counted.
    uint64_t paintDirtyTransitions  = 0;
    uint64_t layoutDirtyTransitions = 0;
    uint64_t cacheInvalidations    = 0; // build/inherited context cache resets (Phase 2)
    uint64_t arrangeInvalidations   = 0;
    uint64_t measureInvalidations   = 0;
    uint64_t structureInvalidations = 0;
};

/// Stable diagnostic record for the most recently resolved event route.
/// Names, not raw widget pointers, are retained so a later detach cannot make
/// an automation dump unsafe to inspect.
struct WidgetRouteTrace
{
    struct Step
    {
        std::string             widget;
        EWidgetEventRoutePhase  phase = EWidgetEventRoutePhase::Target;
        bool                    bHandled = false;
        EWidgetHitFilter        hitFilter = EWidgetHitFilter::Pass;
    };

    EWidgetRoutePolicy policy = EWidgetRoutePolicy::None;
    std::string        target;
    std::vector<std::string> path;
    std::vector<Step>  steps;
    EWidgetRouteResult result = EWidgetRouteResult::NotHandled;
};

struct YA_GUI_API WidgetTree final
{
    enum class ELayer : uint8_t
    {
        Content = 0,
        Popup,
        Tooltip,
        DragIme,
        Count,
    };

    explicit WidgetTree(Extent2D logicalExtent = {});
    ~WidgetTree();

    WidgetTree(const WidgetTree&)            = delete;
    WidgetTree& operator=(const WidgetTree&) = delete;

    // === Presentation context ===
    void setLogicalExtent(Extent2D extent);
    [[nodiscard]] Extent2D getLogicalExtent() const { return _logicalExtent; }

    /// Device-pixel-ratio mapping: logical canvas points -> framebuffer pixels.
    /// The host publishes the real window-system DPI
    /// here on init / resize / monitor move. It is orthogonal to the user zoom
    /// carried by UIFrameBuildContext::uiScale; the final target-pixel size is
    /// logical * dpiScale * uiScale. Defaults to 1.0 (headless / unscaled).
    void setDpiScale(float scale);
    [[nodiscard]] float getDpiScale() const { return _dpiScale; }

    /// The one DPI publish step for every host that presents a tree: `scale`
    /// folds into this tree's layout/paint mapping AND is published to the
    /// font stack, so glyph raster density always matches the scale the
    /// snapshot is built at. Hosts call it when the window's device scale is
    /// learned (init / resize / monitor move) and again right before each
    /// buildSnapshot -- FontManager's active scale is process state shared by
    /// every tree, so the sync belongs to the snapshot boundary, not to
    /// window events alone.
    void publishDpiScale(float scale);

    /// Clipboard used by focused text fields (primary+C/X/V). Default is an
    /// in-memory buffer so closure tests do not need SDL. Windowed hosts bind
    /// OS clipboard via `setClipboardHooks`.
    void setClipboardText(std::string text);
    [[nodiscard]] std::string getClipboardText() const;
    void setClipboardHooks(std::function<std::string()> read,
                           std::function<void(const std::string&)> write);

    // === Theme (style-system Phase 2) ===
    /// Mount the tree-level theme (app/game provides the content). Switching
    /// bumps the generation token, which repaints every widget that resolved
    /// a style through it (resolveThemeStyle registers that edge).
    void setTheme(UITheme* theme);
    [[nodiscard]] UITheme* getTheme() const { return _theme; }
    /// Theme-switch invalidation token: widgets read it during paint (via
    /// resolveThemeStyle) so a theme switch repaints them.
    [[nodiscard]] const std::shared_ptr<Reactive<uint64_t>>& getThemeGeneration() const { return _themeGeneration; }

    /// Path-keyed async textures. The source adapter lives in the product host
    /// (AssetManager) or Workbench (builtin lookup); the catalog is tree-owned
    /// so paint never holds AssetManager or widget-pointer listeners.
    void setTextureSource(IGuiTextureSource* source);
    [[nodiscard]] IGuiTextureSource* getTextureSource() const { return _textureSource; }
    [[nodiscard]] FGuiTextureCatalog& textureCatalog() { return _textureCatalog; }
    [[nodiscard]] const FGuiTextureCatalog& textureCatalog() const { return _textureCatalog; }

    // === Structure ===
    /// Internal root (owns the layers). Not a business object.
    [[nodiscard]] UIElement* getRoot() const { return _root.get(); }
    /// Stable system layer. `layer == Content` is the "join the world's Game
    /// UI" mount point.
    [[nodiscard]] UICanvasRoot* getLayer(ELayer layer) const;

    // === Attach / reparent / detach (single-parent contract) ===
    /// Attach `widget` under `parent` (must belong to this tree). Fails
    /// (returns invalid attachment, logs an error) when the widget is already
    /// attached anywhere — reparent() is the explicit move operation. The
    /// widget keeps its own _zOrder (set it before attaching).
    [[maybe_unused]] WidgetAttachment attach(UIElement& parent, const UIElementRef& widget);
    /// Attach `widget` and initialize its parent-owned edge atomically from
    /// the caller's typed-slot intent.
    [[nodiscard]] WidgetAttachment attach(UIElement& parent,
                                          const UIElementRef& widget,
                                          FChildSlotInitializer init);
    /// Attach `widget` under a canvas parent with explicit canvas edge intent.
    /// Prefer `ui::attach(tree, typedParent, widget, canvasSlot()...)` so the
    /// parent type selects the slot builder at compile time. This overload is
    /// canvas-only; a non-canvas parent asserts.
    [[nodiscard]] WidgetAttachment attach(UIElement&             parent,
                                           const UIElementRef&    widget,
                                           const FCanvasSlotArgs& args);
    [[nodiscard]] WidgetAttachment attachToLayer(ELayer layer,
                                                const UIElementRef& widget,
                                                const FCanvasSlotArgs& args);
    /// Explicit move: detach from the current parent (if any) and attach under
    /// `newParent`. The widget may come from any tree, including detached.
    void reparent(UIElement& newParent, const UIElementRef& widget);
    /// Move `widget` under `sibling`'s parent, positioned immediately before
    /// (after) `sibling` in paint order. No-op when `widget` is `sibling`.
    void reparentBefore(UIElement& sibling, const UIElementRef& widget);
    void reparentAfter(UIElement& sibling, const UIElementRef& widget);
    /// Recursively detach the whole subtree from the tree. Never destroys the
    /// widget; releases focus/capture/hover pointing into the subtree.
    /// A widget that still has a visual parent after its chrome was detached
    /// (parented, `_tree == nullptr`) is unlinked from that parent so a later
    /// attach/addDetachedChild does not see a stale parent.
    void detach(UIElement& widget);
    /// Whether `widget` is attached anywhere in this tree.
    [[nodiscard]] bool contains(const UIElement& widget) const;

    // === Frame passes ===
    /// Mark layout dirty (called on attach/detach/property-affecting edits;
    /// the host calls layout() once per frame before snapshot).
    void invalidateLayout(EWidgetLayoutInvalidation scope = EWidgetLayoutInvalidation::Structure);
    /// Full layout pass: root fills the logical extent, layers fill in layer
    /// order, content children sort by zOrder.
    void layout();
    /// Drive frame lifecycle for attached widgets that opt into ticking.
    /// Events are dispatched before this call; snapshot/layout/paint happen
    /// after it.
    void tick(float deltaSeconds);
    [[nodiscard]] bool isLayoutValid() const { return !_bLayoutDirty; }

    /// Layout (if dirty) + paint the whole tree into an immutable frame
    /// snapshot. Must be called before the RenderGraph is built; command
    /// recording only ever consumes the returned snapshot. Business code must
    /// not mutate tree structure from paint/layout callbacks; framework-owned
    /// tooltip and drag-session maintenance is the only exception and runs at
    /// explicit pass boundaries.
    [[nodiscard]] UIFrameSnapshot buildSnapshot(const UIFrameBuildContext& ctx);

    /// Per-frame counters from the most recent buildSnapshot() call.
    [[nodiscard]] const GuiPerfStats& getPerfStats() const { return _perfStats; }
    /// Opt-in inspector packet from the most recent buildSnapshot(). Empty
    /// unless `YA_GUI_INSPECTOR_IS_ENABLED()`.
    [[nodiscard]] FGuiFrameInspectorRecord& getFrameInspectorRecord() { return _inspectorRecord; }
    [[nodiscard]] const FGuiFrameInspectorRecord& getFrameInspectorRecord() const { return _inspectorRecord; }
    /// Cumulative G2 validation-frame mismatches since tree creation
    /// (guardrail G-C; always 0 in release builds).
    [[nodiscard]] uint64_t getValidationMismatches() const { return _validationMismatches; }
    /// Most recent invalidation reason observed by this tree (diagnostics).
    /// Updated on each dirty transition; None until the first invalidation.
    [[nodiscard]] EUIInvalidationReason getLastInvalidationReason() const { return _lastInvalidationReason; }

    /// Explicit event dispatch. Pointer routes use preview (root -> parent),
    /// target, then bubble (parent -> root); Pass routes continue to lower
    /// hit candidates and Stop routes terminate delivery. Pointer capture
    /// overrides hit discovery, keyboard routes to the focused widget, and
    /// Tab / Shift+Tab is handled by the tree first.
    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const WidgetEventContext& ctx);

    /// Topmost-first pick of the widget under `logicalPoint` (children before
    /// self, zOrder descending, respecting subtree culling). Shared by the
    /// editor preview picking and hit-test diagnostics; null when nothing is
    /// hit.
    [[nodiscard]] UIElement* pickAt(const glm::vec2& logicalPoint) const { return topmostHit(logicalPoint); }

    // === Focus / capture / hover ===
    /// Move keyboard focus. Notifies the previous/next widget through
    /// onFocusLost / onFocusGained. `bFromKeyboard` marks Tab-traversal focus
    /// (drives the button's persistent focus highlight).
    void setFocus(UIElement* widget, bool bFromKeyboard = false);
    [[nodiscard]] UIElement* getFocused() const { return _focused; }
    [[nodiscard]] bool wantsTextInput() const
    {
        for (UIElement* node : getFocusPath()) {
            if (node && node->isAttached() && node->wantsTextInput()) {
                return true;
            }
        }
        return false;
    }
    /// Pointer capture contract: capture is legal only inside a live press
    /// session (asserted), and it cannot outlive that session. The tree closes
    /// the session on the matching release; when the framework has to close it
    /// because the platform lost the release, the captor is told through
    /// clearTransientInputState() and the capture is dropped by the tree. A
    /// widget therefore never has to remember to release capture on a path the
    /// platform may not deliver.
    void setPointerCapture(UIElement* widget);
    void releasePointerCapture(UIElement* widget);
    [[nodiscard]] UIElement* getPointerCapture() const { return _captured; }
    /// Physical buttons this tree last saw held, encoded `1u << EMouse::T`.
    /// Zero means no pointer session can be live.
    [[nodiscard]] uint32_t getPointerButtonsDown() const { return _pointerButtonsDown; }
    /// Count of pointer sessions the framework had to end on its own because
    /// the platform never delivered their release (focus loss, pointer left
    /// every window, re-press with no release in between, or capture that
    /// outlived its press). Non-zero is a lost-release diagnostic, published as
    /// `gui.tree.pointer_recoveries`.
    [[nodiscard]] uint64_t getPointerSessionRecoveries() const { return _pointerSessionRecoveries; }
    /// End the live pointer session without a release: drop a tentative drag
    /// candidate, cancel an active drag session (observers receive
    /// EDragFinishResult::Cancelled), tell and drop the pointer capture, and
    /// clear the button mask. Idempotent; a tree with no live session does
    /// nothing. Framework-owned recovery for a release the platform did not
    /// deliver.
    void cancelPointerSession(std::string_view cause);
    /// Reconcile the cached button mask with the platform's authoritative state
    /// (`osButtonsDown` uses the same `1u << EMouse::T` encoding). When the
    /// platform reports no button held while the tree still owns a session, the
    /// session is cancelled: a tree never keeps a press the pointer ended.
    /// Hosts call this on focus loss and when the pointer leaves their window.
    void reconcilePointerButtons(uint32_t osButtonsDown, std::string_view cause);
    [[nodiscard]] bool hasModalPopup() const;
    [[nodiscard]] UIElement* getHovered() const { return _hovered; }
    [[nodiscard]] UIElement* getTooltipHost() const { return _tooltipHost.get(); }

    /// Drop hover, tooltip and the ordinary pointer-over path without injecting
    /// a pointer event. Capture, focus and an active drag session stay put so
    /// a WindowFocusLost can keep cross-window drag/capture alive.
    void clearPointerOverState();

    /// Remove the active tooltip (hover change / detach / tree teardown).
    void removeTooltip();
    /// Mount the tooltip for the hovered widget once the dwell delay elapsed
    /// (called every frame from buildSnapshot).
    void updateTooltip();
    [[nodiscard]] const WidgetPointerState& getPointerState() const { return _pointerState; }
    /// Current pointer route path, root to target. For ordinary input it is
    /// the topmost hit path; while captured it terminates at the captor.
    /// Returns a snapshot of the live nodes (detached/destroyed entries are
    /// dropped), so callers never observe a dangling pointer.
    [[nodiscard]] std::vector<UIElement*> getPointerPath() const;
    /// Current focus path, root to focused widget. Empty without focus.
    [[nodiscard]] std::vector<UIElement*> getFocusPath() const;
    [[nodiscard]] const WidgetRouteTrace& getLastRouteTrace() const { return _lastRouteTrace; }

    // === Drag & drop (source-local) ===
    /// Whether this tree is the source of the host drag session. The unique
    /// session identity (source vs hover window) lives on GUIDragRouter.
    [[nodiscard]] bool isDragging() const { return static_cast<bool>(_dragOperation); }
    /// Start a drag session from `source`. Generic id/text lives on
    /// `operation->payload`; domain types inherit `UIDragDropOperation`.
    /// A ghost (Border + label) follows the pointer on the DragIme layer
    /// unless `bShowGhost` is false (the source itself follows the pointer
    /// instead, e.g. a dock floating window). While the session is active,
    /// the drag SOURCE subtree is skipped by the hit walk so the widgets
    /// beneath it stay reachable as drop targets.
    void beginDrag(UIElement* source,
                   UIDragDropOperationRef operation,
                   DragSessionObserver observer = {},
                   bool bShowGhost = true,
                   bool bSkipSourceInHitTest = false);
    /// Move the drag ghost and refresh the highlighted drop target.
    void updateDrag(const glm::vec2& logicalPoint);
    /// Release the drag: deliver `onDrop` to the topmost accepting target.
    void endDrag(const glm::vec2& logicalPoint);
    /// Abort the drag without delivering a drop.
    void cancelDrag();
    /// Finish a source session after a foreign tree accepted or rejected the
    /// drop. Does not search this tree for a target (use `endDrag` for that).
    void finishDrag(EDragFinishResult result);
    /// Hover a drop target using an operation owned by another tree. Does not
    /// start a local drag session: `isDragging()` stays false.
    void setExternalDropHover(const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    void clearExternalDropHover();
    /// Hide the source-local ghost and drop preview while the pointer is over
    /// another window or the desktop. The session stays active; the hover tree
    /// (or the small desktop overlay) shows the same ghost chrome.
    void setSourceDragChromeVisible(bool visible);
    /// Deliver `onDrop` for an operation owned by another tree. Returns true
    /// when a target accepted the drop.
    [[nodiscard]] bool dropExternal(const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    [[nodiscard]] UIElement* getDragSource() const { return _dragSource; }
    [[nodiscard]] const UIDragDropOperation* getDragOperation() const { return _dragOperation.get(); }
    [[nodiscard]] UIElement* getDropTarget() const { return _dragDropTarget; }

  private:
    friend struct UIElement;

    /// Single-topmost hit test: recurse zOrder-high-first, children before
    /// self, returning the first (and only) hit. Mirrors UE Slate / WPF / Qt /
    /// DOM: one point resolves to exactly one widget, then routing and hover
    /// both derive from that widget's ancestor path. Returns null when nothing
    /// is hit. During an active drag, `skipSubtree` (the drag source) is
    /// ignored so the widgets beneath the dragged widget remain reachable as
    /// drop targets.
    [[nodiscard]] static UIElement* hitTestAt(UIElement* element,
                                              const glm::vec2& logicalPoint,
                                              bool bForHover = false,
                                              UIElement* skipSubtree = nullptr);
    /// Resolve the hover owner from a hit target: deepest attached
    /// isHoverable() ancestor whose hitTestSelf contains `logicalPoint`.
    /// A text child resolves to its button; an expander/split whose hoverable
    /// region is only the header/divider is skipped when the pointer is on
    /// a body descendant.
    [[nodiscard]] static UIElement* hoverOwnerAlongPath(UIElement* target,
                                                        const glm::vec2& logicalPoint);
    /// Assign tree membership to a widget and its whole subtree (invariant:
    /// attached iff every descendant is a member of the same tree).
    static void markSubtreeMembership(UIElement* widget, WidgetTree* tree);
    static void prepareSubtree(UIElement* widget);
    static void notifyAttachedSubtree(UIElement* widget);
    static void notifyDetachedSubtree(UIElement* widget);
    static void tickSubtree(UIElement* widget, float deltaSeconds);
    /// Collect attached, visible, focusable widgets in stable paint order
    /// (layers bottom -> top, children zOrder ascending) for Tab traversal.
    void collectFocusables(std::vector<UIElement*>& outFocusables) const;
    /// Shared reparentBefore/After implementation (friend access to the
    /// widget's private parent/children state).
    static void reparentRelativeTo(WidgetTree& tree, UIElement& sibling, const UIElementRef& widget, bool bAfter);

    void onWidgetDetached(UIElement& widget);
    void clearTransientState(UIElement& widget);
    [[nodiscard]] UIElement* topmostHit(const glm::vec2& logicalPoint) const;
    [[nodiscard]] static std::vector<UIElement*> buildPath(UIElement* target);
    void preparePointerState(EEvent::T eventType, const WidgetEventContext& ctx);
    [[nodiscard]] EWidgetRouteResult dispatchCapturedPointerEvent(const Event& event,
                                                                  const WidgetEventContext& ctx,
                                                                  EEvent::T eventType);
    [[nodiscard]] EWidgetRouteResult dispatchRoute(UIElement* target,
                                                   const Event& event,
                                                   const WidgetEventContext& ctx,
                                                   EWidgetRoutePolicy policy,
                                                   bool bAppendTrace);
    [[nodiscard]] static EWidgetRouteResult mergeRouteResult(EWidgetRouteResult current,
                                                             EWidgetRouteResult next);
    [[nodiscard]] static EWidgetRoutePolicy classifyPointerRoute(const std::vector<UIElement*>& path);
    void refreshPointerPath(UIElement* target);
    void refreshFocusPath();
    /// Unified liveness sweep at the top of each input dispatch: drops focus /
    /// capture / hover / path entries that no longer point at a live, attached
    /// widget. Complements detach-time clearing so no code path can leave the
    /// tree holding a dangling transient reference (UE FocusPath semantics).
    void pruneTransientState();
    /// Press/release pairing for pointer capture. A leftover capture with the
    /// mouse already up (or a second press of the same button) is the
    /// "click twice to activate" class of bugs — assert instead of eating
    /// the first click.
    void beginPointerDispatch(const Event& event);
    void endPointerDispatch(const Event& event);
    void repairPointerSession(std::string_view where);
    void updateHovered(UIElement* widget);
    void beginRouteTrace(EWidgetRoutePolicy policy, UIElement* target);
    void appendRouteTraceStep(const UIElement& widget,
                              EWidgetEventRoutePhase phase,
                              bool bHandled);
    void setRouteTrace(EWidgetRoutePolicy policy, UIElement* target)
    {
        beginRouteTrace(policy, target);
    }
    /// Topmost widget accepting `operation` at `logicalPoint` (walks ancestors
    /// of the hit widget). The no-argument overload uses the local session.
    [[nodiscard]] UIElement* findDropTarget(const glm::vec2& logicalPoint) const;
    [[nodiscard]] UIElement* findDropTarget(const glm::vec2& logicalPoint,
                                            const UIDragDropOperation* operation) const;
    [[nodiscard]] UIElement* findDropHoverTarget(const glm::vec2& logicalPoint,
                                                 const UIDragDropOperation* operation) const;
    void applyDropTarget(UIElement* target,
                         const UIDragDropOperation& operation,
                         const glm::vec2& logicalPoint);
    [[nodiscard]] UIElementRef attachDragGhost(const std::string& label);
    void placeDragGhost(UIElement& ghost, const glm::vec2& logicalPoint);
    void clearExternalGhost();
    /// Release ghost + highlight + payload (shared by end/cancel).
    void clearDragSession();
    /// Poll FontManager::resourceRevision() and, on change, remasure+repaint
    /// every attached widget so nested fill containers cannot skip stale
    /// text metrics.
    void applyFontResourceRevision();
    void markSubtreeResourceReady(UIElement& element);

    UIElementRef _root;
    std::array<UIElementRef, static_cast<size_t>(ELayer::Count)> _layers;
    Extent2D      _logicalExtent{};
    float         _dpiScale = 1.0f; // logical points -> framebuffer pixels
    std::string   _clipboardText;
    std::function<std::string()> _clipboardRead;
    std::function<void(const std::string&)> _clipboardWrite;
    bool          _bLayoutDirty = true;
    uint8_t       _layoutInvalidationMask = static_cast<uint8_t>(EWidgetLayoutInvalidation::Structure);
    GuiPerfStats               _perfStats;
    FGuiFrameInspectorRecord   _inspectorRecord;
    uint64_t                   _inspectorPrevPaintDirty   = 0;
    uint64_t                   _inspectorPrevLayoutDirty  = 0;
    uint64_t                   _inspectorPrevArrangeDirty = 0;

    // Tree-level theme (style-system Phase 2). _themeGeneration is a
    // Reactive<uint64_t> token: setTheme bumps it so every widget that read
    // it during paint (resolveThemeStyle) repaints on the next snapshot.
    UITheme*                                _theme = nullptr;
    std::shared_ptr<Reactive<uint64_t>>     _themeGeneration = std::make_shared<Reactive<uint64_t>>(0);

    // Invalidation diagnostics (GI-001): cumulative dirty-transition counters
    // since tree construction, plus the most recent invalidation reason.
    // Snapshot into _perfStats at buildSnapshot for per-frame comparison.
    // Updated by UIElement::markPaintDirty/markLayoutDirty (friend access).
    uint64_t              _paintDirtyTransitions  = 0;
    uint64_t              _layoutDirtyTransitions = 0;
    uint64_t              _cacheInvalidations    = 0;
    uint64_t              _arrangeInvalidations   = 0;
    uint64_t              _measureInvalidations   = 0;
    uint64_t              _structureInvalidations = 0;
    uint64_t              _layoutSkippedWidgets   = 0;
    EUIInvalidationReason _lastInvalidationReason = EUIInvalidationReason::None;

    /// Double-buffered per-widget draw-item caches for incremental paint:
    /// index [_cacheIndex] is read (previous frame), [_cacheIndex ^ 1] is
    /// written this frame and swapped at the end of buildSnapshot.
    std::array<std::unordered_map<uint64_t, std::vector<UIFrameDrawItem>>, 2> _itemCache;
    int _cacheIndex = 0;
    /// Frames built since tree creation (drives the debug validation frame).
    uint32_t _frameCounter = 0;
    /// Cumulative G2 validation mismatches (debug builds only; scenario
    /// assert_validation_clean reads this through the public getter).
    uint64_t _validationMismatches = 0;

    // Build-context validity (GI-002): draw-item segments hold final target-
    // pixel + resolved-texture data. uiScale/offset/DPI mapping changes drop
    // caches as BuildContextChanged; ctx.generation (resolver identity swap)
    // drops caches as ResourceReady. Everyday texture ready is path-keyed via
    // FGuiTextureCatalog, not generation. Font atlas identity is a separate
    // FontManager::resourceRevision poll.
    bool      _bHasBuildContext = false;
    uint64_t  _lastGeneration   = 0;
    glm::vec2 _lastUiScale      = {1.0f, 1.0f};
    glm::vec2 _lastOffset       = {0.0f, 0.0f};
    bool      _bHasFontRevision = false;
    uint64_t  _lastFontRevision = 0;
    IGuiTextureSource* _textureSource = nullptr;
    FGuiTextureCatalog _textureCatalog;
    bool               _bHasTextureEpoch = false;
    uint64_t           _lastTextureEpoch = 0;
    UIElement*    _focused      = nullptr;
    UIElement*    _captured     = nullptr;
    /// Bits indexed by `EMouse::T`. Capture may exist only while a bit is set;
    /// a press whose bit is already set is a leftover session, recovered as a
    /// cancel rather than treated as a fatal invariant violation.
    uint32_t       _pointerButtonsDown = 0;
    uint64_t       _pointerSessionRecoveries = 0;
    UIElement*    _hovered      = nullptr;
    /// Tooltip host widget currently mounted on the Tooltip layer (null
    /// when no tooltip is shown). Owned by the tree via attachment.
    std::shared_ptr<UIElement> _tooltipHost;
    /// Frame at which the current hover began (dwell delay for tooltips).
    uint32_t _hoveredSinceFrame = 0;
    bool     _bTooltipShown     = false;
    WidgetPointerState _pointerState;
    // Weak pointer paths (UE FWeakWidgetPath semantics): root-to-target live
    // routes that survive widget destruction without dangling. Read through
    // getPointerPath()/getFocusPath(), which lock and drop dead entries.
    std::vector<std::weak_ptr<UIElement>> _pointerPath;
    std::vector<std::weak_ptr<UIElement>> _focusPath;
    WidgetRouteTrace _lastRouteTrace;

    UIElement*        _dragSource   = nullptr;
    /// Strong hold on the drag source for the duration of the session. A drop
    /// handler (onDrop) may detach/destroy the source subtree before the
    /// session's onFinished observer runs (dock floating-window re-sync does
    /// exactly this), so the source must stay alive until the finish callbacks
    /// complete. Released when endDrag/cancelDrag return; this member never
    /// outlives the session, so no permanent retention.
    UIElementRef      _dragSourceKeepAlive;
    UIElement*        _dragCandidate = nullptr;
    glm::vec2         _dragCandidateStart{};
    /// When true, drop-target discovery ignores the drag source subtree
    /// (opt-in: the dragged widget itself follows the pointer, e.g. a dock
    /// floating window; containers like DockSpace/TreeView keep their own
    /// subtree hittable).
    bool               _bDragSkipSource = false;
    UIDragDropOperationRef _dragOperation;
    glm::vec2         _dragPoint{};
    UIElement*        _dragDropTarget = nullptr;
    /// Non-owning pointer into another tree's session. Null unless this tree
    /// is the hover/drop side of a cross-window drag.
    const UIDragDropOperation* _externalDropOp = nullptr;
    UIElementRef      _dragGhost;
    UIElementRef      _externalGhost;
    DragSessionObserver _dragObserver;
};

} // namespace ya
