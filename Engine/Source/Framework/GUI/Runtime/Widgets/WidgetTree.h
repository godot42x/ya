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
#include <string>
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
    [[nodiscard]] WidgetAttachment attach(UIElement& parent, const UIElementRef& widget);
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
    [[nodiscard]] const std::vector<UIElement*>& getPointerPath() const { return _pointerPath; }
    /// Current focus path, root to focused widget. Empty without focus.
    [[nodiscard]] const std::vector<UIElement*>& getFocusPath() const { return _focusPath; }
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

    /// Collect deepest hit candidates in topmost-first order. Each candidate
    /// becomes one explicit preview -> target -> bubble route attempt.
    static void collectHitTargetsSubtree(UIElement* element,
                                         const glm::vec2& logicalPoint,
                                         std::vector<UIElement*>& outTargets);
    /// Resolve the single hovered widget from the hit candidates: walk each
    /// candidate (topmost-first) and its ancestor chain for the first
    /// isHoverable() widget. This decouples "hover" from "topmost hit" so a
    /// text child or a transparent popup shield never becomes the hover owner
    /// in place of the real interactive leaf.
    [[nodiscard]] static UIElement* resolveHoverTarget(const std::vector<UIElement*>& targets);
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
    void resolvePointerTargets(EEvent::T eventType,
                               const WidgetEventContext& ctx,
                               std::vector<UIElement*>& outTargets);
    [[nodiscard]] EWidgetRouteResult dispatchResolvedRoute(const Event& event,
                                                           const WidgetEventContext& ctx,
                                                           const std::vector<UIElement*>& targets);
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
    UIElement*    _focused      = nullptr;
    UIElement*    _captured     = nullptr;
    UIElement*    _hovered      = nullptr;
    WidgetPointerState _pointerState;
    std::vector<UIElement*> _pointerPath;
    std::vector<UIElement*> _focusPath;
    WidgetRouteTrace _lastRouteTrace;

    UIElement*        _dragSource   = nullptr;
    std::string       _dragPayload;
    glm::vec2         _dragPoint{};
    UIElement*        _dragDropTarget = nullptr;
    UIElementRef      _dragGhost;
};

} // namespace ya
