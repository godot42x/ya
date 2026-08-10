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

struct WidgetTree final
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

    /// Topmost-first event dispatch (children before parent, zOrder
    /// descending, layers bottom -> top). Pointer capture overrides the walk;
    /// keyboard events route to the focused widget. Returns the route result.
    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const WidgetEventContext& ctx);

    /// Topmost-first pick of the widget under `logicalPoint` (children before
    /// self, zOrder descending, respecting subtree culling). Shared by the
    /// editor preview picking and hit-test diagnostics; null when nothing is
    /// hit.
    [[nodiscard]] UIElement* pickAt(const glm::vec2& logicalPoint) const { return topmostHit(logicalPoint); }

    // === Focus / capture / hover ===
    void setFocus(UIElement* widget);
    [[nodiscard]] UIElement* getFocused() const { return _focused; }
    void setPointerCapture(UIElement* widget);
    void releasePointerCapture(UIElement* widget);
    [[nodiscard]] UIElement* getPointerCapture() const { return _captured; }
    [[nodiscard]] UIElement* getHovered() const { return _hovered; }

  private:
    friend struct UIElement;

    /// Topmost-first hit walk (children before self, zOrder descending).
    static UIElement* topmostHitSubtree(UIElement* element, const glm::vec2& logicalPoint);
    /// Event dispatch walk with the legacy Pass/Stop routing semantics.
    static EWidgetRouteResult dispatchSubtree(UIElement* element,
                                              const Event& event,
                                              const WidgetEventContext& ctx);
    /// Assign tree membership to a widget and its whole subtree (invariant:
    /// attached iff every descendant is a member of the same tree).
    static void markSubtreeMembership(UIElement* widget, WidgetTree* tree);
    /// Shared reparentBefore/After implementation (friend access to the
    /// widget's private parent/children state).
    static void reparentRelativeTo(WidgetTree& tree, UIElement& sibling, const UIElementRef& widget, bool bAfter);

    void onWidgetDetached(UIElement& widget);
    void clearTransientState(UIElement& widget);
    [[nodiscard]] UIElement* topmostHit(const glm::vec2& logicalPoint) const;

    UIElementRef _root;
    std::array<UIElementRef, static_cast<size_t>(ELayer::Count)> _layers;
    Extent2D      _logicalExtent{};
    bool          _bLayoutDirty = true;
    UIElement*    _focused      = nullptr;
    UIElement*    _captured     = nullptr;
    UIElement*    _hovered      = nullptr;
};

} // namespace ya
