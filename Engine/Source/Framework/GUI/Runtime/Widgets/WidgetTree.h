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

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetAttachment.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

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

/// Tree-owned pointer state. The host supplies the coordinate on each native
/// pointer event; consumers read the retained value instead of forwarding
/// stale business-level mouse positions between controls.
struct WidgetPointerState
{
    glm::vec2 logicalPoint = {0.0f, 0.0f};
    bool      bKnown       = false;
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
    // Invalidation diagnostics (GI-001): cumulative clean->dirty transition
    // counts observed by this tree. A "transition" is a 0->1 dirty edge, so
    // repeated marks of an already-dirty widget are not double-counted.
    uint64_t paintDirtyTransitions  = 0;
    uint64_t layoutDirtyTransitions = 0;
    uint64_t cacheInvalidations    = 0; // build/inherited context cache resets (Phase 2)
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

    // === Structure ===
    /// Internal root (owns the layers). Not a business object.
    [[nodiscard]] UIElement* getRoot() const { return _root.get(); }
    /// Stable system layer. `layer == Content` is the "join the world's Game
    /// UI" mount point.
    [[nodiscard]] UIElement* getLayer(ELayer layer) const;

    // === Attach / reparent / detach (single-parent contract) ===
    /// Attach `widget` under `parent` (must belong to this tree). Fails
    /// (returns invalid attachment, logs an error) when the widget is already
    /// attached anywhere — reparent() is the explicit move operation. The
    /// widget keeps its own _zOrder (set it before attaching).
    [[maybe_unused]] WidgetAttachment attach(UIElement& parent, const UIElementRef& widget);
    /// Attach `widget` to a system layer (Content by default for game UI).
    [[nodiscard]] WidgetAttachment attachToLayer(ELayer layer, const UIElementRef& widget);
    /// Explicit move: detach from the current parent (if any) and attach under
    /// `newParent`. The widget may come from any tree, including detached.
    void reparent(UIElement& newParent, const UIElementRef& widget);
    /// Move `widget` under `sibling`'s parent, positioned immediately before
    /// (after) `sibling` in paint order. No-op when `widget` is `sibling`.
    void reparentBefore(UIElement& sibling, const UIElementRef& widget);
    void reparentAfter(UIElement& sibling, const UIElementRef& widget);
    /// Recursively detach the whole subtree from the tree. Never destroys the
    /// widget; releases focus/capture/hover pointing into the subtree.
    void detach(UIElement& widget);
    /// Whether `widget` is attached anywhere in this tree.
    [[nodiscard]] bool contains(const UIElement& widget) const;

    // === Frame passes ===
    /// Mark layout dirty (called on attach/detach/property-affecting edits;
    /// the host calls layout() once per frame before snapshot).
    void invalidateLayout();
    /// Full layout pass: root fills the logical extent, layers fill in layer
    /// order, content children sort by zOrder.
    void layout();
    [[nodiscard]] bool isLayoutValid() const { return !_bLayoutDirty; }

    /// Layout (if dirty) + paint the whole tree into an immutable frame
    /// snapshot. Must be called before the RenderGraph is built; command
    /// recording only ever consumes the returned snapshot.
    [[nodiscard]] UIFrameSnapshot buildSnapshot(const UIFrameBuildContext& ctx);

    /// Per-frame counters from the most recent buildSnapshot() call.
    [[nodiscard]] const GuiPerfStats& getPerfStats() const { return _perfStats; }
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
    void setPointerCapture(UIElement* widget);
    void releasePointerCapture(UIElement* widget);
    [[nodiscard]] UIElement* getPointerCapture() const { return _captured; }
    [[nodiscard]] UIElement* getHovered() const { return _hovered; }
    [[nodiscard]] const WidgetPointerState& getPointerState() const { return _pointerState; }
    /// Current pointer route path, root to target. For ordinary input it is
    /// the topmost hit path; while captured it terminates at the captor.
    /// Returns a snapshot of the live nodes (detached/destroyed entries are
    /// dropped), so callers never observe a dangling pointer.
    [[nodiscard]] std::vector<UIElement*> getPointerPath() const;
    /// Current focus path, root to focused widget. Empty without focus.
    [[nodiscard]] std::vector<UIElement*> getFocusPath() const;
    [[nodiscard]] const WidgetRouteTrace& getLastRouteTrace() const { return _lastRouteTrace; }

    // === Drag & drop session (gui-app-bootstrap Phase 4) ===
    /// Whether a drag session is active. While active the tree intercepts
    /// pointer moves (ghost + drop-target highlight), releases (drop) and
    /// presses/Esc (cancel).
    [[nodiscard]] bool isDragging() const { return !_dragPayload.empty(); }
    /// Start a drag session from `source` with a string payload; a ghost
    /// (Panel + label) follows the pointer on the DragIme layer.
    void beginDrag(UIElement* source, std::string payload, std::string ghostLabel);
    /// Move the drag ghost and refresh the highlighted drop target.
    void updateDrag(const glm::vec2& logicalPoint);
    /// Release the drag: deliver `onDrop` to the topmost accepting target.
    void endDrag(const glm::vec2& logicalPoint);
    /// Abort the drag without delivering a drop.
    void cancelDrag();
    [[nodiscard]] UIElement* getDragSource() const { return _dragSource; }
    [[nodiscard]] const std::string& getDragPayload() const { return _dragPayload; }

  private:
    friend struct UIElement;

    /// Single-topmost hit test: recurse zOrder-high-first, children before
    /// self, returning the first (and only) hit. Mirrors UE Slate / WPF / Qt /
    /// DOM: one point resolves to exactly one widget, then routing and hover
    /// both derive from that widget's ancestor path. Returns null when nothing
    /// is hit.
    [[nodiscard]] static UIElement* hitTestAt(UIElement* element,
                                              const glm::vec2& logicalPoint,
                                              bool bForHover = false);
    /// Resolve the single hover owner from a hit target: walk up its ancestor
    /// chain for the first isHoverable() widget. Because the target is already
    /// the topmost hit, this is deterministic (the deepest hoverable) with no
    /// separate scan — a text child or a transparent popup shield can never
    /// become the hover owner in place of the real interactive leaf.
    [[nodiscard]] static UIElement* hoverOwnerAlongPath(UIElement* target);
    /// Assign tree membership to a widget and its whole subtree (invariant:
    /// attached iff every descendant is a member of the same tree).
    static void markSubtreeMembership(UIElement* widget, WidgetTree* tree);
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
    void updateHovered(UIElement* widget);
    void beginRouteTrace(EWidgetRoutePolicy policy, UIElement* target);
    void appendRouteTraceStep(const UIElement& widget,
                              EWidgetEventRoutePhase phase,
                              bool bHandled);
    void setRouteTrace(EWidgetRoutePolicy policy, UIElement* target)
    {
        beginRouteTrace(policy, target);
    }
    /// Topmost widget accepting the active drag payload at `logicalPoint`
    /// (walks ancestors of the hit widget).
    [[nodiscard]] UIElement* findDropTarget(const glm::vec2& logicalPoint) const;
    /// Release ghost + highlight + payload (shared by end/cancel).
    void clearDragSession();

    UIElementRef _root;
    std::array<UIElementRef, static_cast<size_t>(ELayer::Count)> _layers;
    Extent2D      _logicalExtent{};
    bool          _bLayoutDirty = true;
    GuiPerfStats  _perfStats;

    // Invalidation diagnostics (GI-001): cumulative dirty-transition counters
    // since tree construction, plus the most recent invalidation reason.
    // Snapshot into _perfStats at buildSnapshot for per-frame comparison.
    // Updated by UIElement::markPaintDirty/markLayoutDirty (friend access).
    uint64_t              _paintDirtyTransitions  = 0;
    uint64_t              _layoutDirtyTransitions = 0;
    uint64_t              _cacheInvalidations    = 0;
    EUIInvalidationReason _lastInvalidationReason = EUIInvalidationReason::None;

    /// Double-buffered per-widget draw-item caches for incremental paint:
    /// index [_cacheIndex] is read (previous frame), [_cacheIndex ^ 1] is
    /// written this frame and swapped at the end of buildSnapshot.
    std::array<std::unordered_map<const UIElement*, std::vector<UIFrameDrawItem>>, 2> _itemCache;
    int _cacheIndex = 0;
    /// Frames built since tree creation (drives the debug validation frame).
    uint32_t _frameCounter = 0;

    // Build-context validity (GI-002): draw-item segments hold final target-
    // pixel + resolved-texture data, so a changed uiScale/offset/generation
    // invalidates both cache buffers (conservative, correctness-first). The
    // last-seen values detect the change across buildSnapshot calls.
    bool      _bHasBuildContext = false;
    uint64_t  _lastGeneration   = 0;
    glm::vec2 _lastUiScale      = {1.0f, 1.0f};
    glm::vec2 _lastOffset       = {0.0f, 0.0f};
    UIElement*    _focused      = nullptr;
    UIElement*    _captured     = nullptr;
    UIElement*    _hovered      = nullptr;
    WidgetPointerState _pointerState;
    // Weak pointer paths (UE FWeakWidgetPath semantics): root-to-target live
    // routes that survive widget destruction without dangling. Read through
    // getPointerPath()/getFocusPath(), which lock and drop dead entries.
    std::vector<std::weak_ptr<UIElement>> _pointerPath;
    std::vector<std::weak_ptr<UIElement>> _focusPath;
    WidgetRouteTrace _lastRouteTrace;

    UIElement*        _dragSource   = nullptr;
    std::string       _dragPayload;
    glm::vec2         _dragPoint{};
    UIElement*        _dragDropTarget = nullptr;
    UIElementRef      _dragGhost;
};

} // namespace ya
