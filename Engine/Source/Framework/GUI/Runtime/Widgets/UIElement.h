#pragma once

// ============================================================================
// UIElement - the Game UI widget base class (ui-widget-tree-refactor Phase 1).
//
// Semantics ported from the legacy Node2D UI path, minus every Scene/Node/ECS
// assumption:
//   - NOT a scene node: no Node inheritance, no entity, no scene-tree
//     membership; ownership is shared (UIElementRef) and the visual parent is
//     a non-owning pointer.
//   - detached by default: an element only participates in layout/input/paint
//     once a WidgetTree attaches it (single visual parent enforced by the tree).
//   - layout/paint/input properties keep the proven Node2D semantics:
//     anchor math, visibility axes, zOrder paint order, Pass/Stop hit filter.
//
// Naming note: this module intentionally uses EWidget* enum names while the
// legacy GUI/Scene module still exports EUI* names; Phase 6 removes the legacy
// module and renames back to the short form.
// ============================================================================

#include "Core/Common/Types.h"
#include "Core/Event.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ya
{

enum class EWidgetAlignH : uint8_t
{
    Left,
    Center,
    Right,
};

enum class EWidgetAlignV : uint8_t
{
    Top,
    Center,
    Bottom,
};

/// Per-widget event routing at the game boundary (same semantics as the
/// legacy EUIHitFilter): Pass nodes respond but never block; Stop nodes
/// consume exclusively.
enum class EWidgetHitFilter : uint8_t
{
    Pass,
    Stop,
};

/// Render / hit-test / layout state (UMG Visibility semantics), identical to
/// the legacy EUIVisibility.
enum class EWidgetVisibility : uint8_t
{
    Visible,             // render + self hit + children hit + layout space
    Hidden,              // no render, no hit; keeps layout space
    Collapsed,           // no render, no hit; no layout space
    HitTestInvisible,    // renders; self not hittable, children still are
    SelfHitTestInvisible // renders; the whole subtree is not hittable
};

struct WidgetTree;
struct WidgetAttachment;
struct UIElement;

using UIElementRef = std::shared_ptr<UIElement>;

/// Paint context passed down the tree during the paint pass. `uiScale` maps
/// logical UI pixels to render-target pixels (host/framebuffer scale).
struct WidgetPaintContext
{
    glm::vec2 uiScale = {1.0f, 1.0f};
};

/// Event context for widget hit-testing / event dispatch. `logicalPoint` is
/// in tree-local logical pixels (top-left origin, Y down) — the host converts
/// window coordinates before dispatch.
struct WidgetEventContext
{
    glm::vec2 logicalPoint = {0.0f, 0.0f};
    /// Set by WidgetTree when the event is routed through pointer capture:
    /// widgets may then accept events outside their own rect (and must not
    /// rely on their own hit test).
    bool bViaCapture = false;
};

struct UIElement : public std::enable_shared_from_this<UIElement>
{
    explicit UIElement(std::string name = "Widget");
    virtual ~UIElement();

    UIElement(const UIElement&)            = delete;
    UIElement& operator=(const UIElement&) = delete;

    // === Identity ===
    std::string _name;
    /// Stable registry type ID, set by UITypeRegistry::createInstance (empty
    /// for framework-internal / direct make_shared instances).
    std::string _typeId;

    // === Visual / layout / input properties ===
    glm::vec2          _position   = {0.0f, 0.0f}; // Offset (px) from the anchor point within the parent rect
    glm::vec2          _size       = {100.0f, 50.0f};
    EWidgetVisibility  _visibility = EWidgetVisibility::Visible;
    int                _zOrder     = 0;
    glm::vec2          _anchorMin  = {0.0f, 0.0f}; // Fraction of the parent rect (clamped 0..1)
    glm::vec2          _anchorMax  = {0.0f, 0.0f};
    glm::vec2          _pivot      = {0.5f, 0.5f}; // Reserved; unused until rotation/scale exists
    EWidgetHitFilter   _hitFilter  = EWidgetHitFilter::Pass;

    /// Layout cache: final rect in tree-local logical pixels, computed by the
    /// layout pass. Not serialized.
    Rect2D _layoutRect{};

    // === Tree membership (managed by WidgetTree only) ===
    /// Owning tree, or nullptr while detached.
    [[nodiscard]] WidgetTree* getTree() const { return _tree; }
    /// Visual parent (non-owning), or nullptr while detached.
    [[nodiscard]] UIElement* getParent() const { return _parent; }
    [[nodiscard]] bool       isAttached() const { return _tree != nullptr; }
    /// Children in attachment order (paint order = getChildrenInPaintOrder).
    [[nodiscard]] const std::vector<UIElementRef>& getChildren() const { return _children; }
    /// Children stably sorted by _zOrder ascending.
    [[nodiscard]] std::vector<UIElement*> getChildrenInPaintOrder() const;

    // === Layout (top-down, called by WidgetTree::layout) ===
    /// Compute this element's rect within `parentRect` (anchor math), store it
    /// in `_layoutRect`, then lay out children in paint order.
    virtual void layout(const Rect2D& parentRect);
    /// Container-assigned layout: take `rect` verbatim (no anchor math).
    void layoutAssigned(const Rect2D& rect);
    /// Desired size for container arrangement (leaf = _size; auto-size text =
    /// measured text; containers aggregate children).
    [[nodiscard]] virtual glm::vec2 computeDesiredSize() const;

    // === Paint (after layout; records commands into the active batch) ===
    virtual void paint(const WidgetPaintContext& ctx);

    // === Events (hit-tested by WidgetTree's topmost-first walker) ===
    /// Return true to consume the event. `ctx.logicalPoint` is in tree-local
    /// logical pixels; hit-test against _layoutRect.
    virtual bool handleInputEvent(const Event& event, const WidgetEventContext& ctx);
    /// Clear transient input state (e.g. button hover) before a MouseMoved
    /// hit-test pass.
    virtual void resetHoverState() {}

    // === Effective-state queries ===
    [[nodiscard]] bool isVisibleForRender() const
    {
        return _visibility != EWidgetVisibility::Hidden &&
               _visibility != EWidgetVisibility::Collapsed;
    }
    [[nodiscard]] bool isHitTestableSelf() const
    {
        return _visibility == EWidgetVisibility::Visible;
    }
    [[nodiscard]] bool isHitTestableSubtree() const
    {
        return _visibility == EWidgetVisibility::Visible ||
               _visibility == EWidgetVisibility::HitTestInvisible;
    }
    [[nodiscard]] bool participatesInLayout() const
    {
        return _visibility != EWidgetVisibility::Collapsed;
    }
    /// Whether this element and every ancestor pass isVisibleForRender().
    [[nodiscard]] bool isVisibleInTree() const;
    /// Whether the UI walker would descend into this subtree given the
    /// ancestor chain (Hidden/Collapsed/SelfHitTestInvisible cull hits).
    [[nodiscard]] bool isHitTestableInTree() const;
    /// Own-rect hit test against the cached layout rect.
    [[nodiscard]] bool hitTestLayoutRect(const glm::vec2& logicalPoint) const;

  protected:
    /// Anchor math: rect.min = parent.pos + parent.size*anchorMin + _position;
    /// rect.max = parent.pos + parent.size*anchorMax + _position + _size.
    [[nodiscard]] Rect2D computeAnchorRect(const Rect2D& parentRect) const;
    /// Lay out direct children within `layoutRect` (paint order).
    void layoutChildren(const Rect2D& layoutRect);
    /// Recursively paint children in paint order.
    void paintChildren(const WidgetPaintContext& ctx);
    /// Subclasses draw themselves here (base: no-op).
    virtual void paintSelf(const WidgetPaintContext& ctx) {}

  private:
    friend struct WidgetTree;
    friend class UITypeRegistry;

    /// Visual parent / tree hold strong references to children; the child
    /// points back with a raw (non-owning) pointer.
    std::vector<UIElementRef> _children;
    UIElement*                _parent = nullptr;
    WidgetTree*               _tree   = nullptr;

    /// Module-owner lease set by UITypeRegistry::createInstance: keeps the
    /// owning module alive and decrements its live-instance count when this
    /// element is destroyed.
    std::shared_ptr<void> _moduleLease;
};

} // namespace ya
