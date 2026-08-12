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
#include "Core/Reflection/Reflection.h"

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

enum class EWidgetBoxLayout : uint8_t
{
    Horizontal,
    Vertical,
};

/// Main-axis arrangement of a box container (Stack role, gui-app-bootstrap
/// Phase 2): where the packed children sit when they do not fill the content
/// extent.
enum class EWidgetMainAxisAlignment : uint8_t
{
    Start,  // children packed at the content start (default)
    Center, // children centered along the main axis
    End,    // children packed at the content end
};

/// Keyboard focus participation (gui-app-bootstrap Phase 2).
///   None      - default: never focused by Tab traversal, skipped by the
///               tree's stable-order focus walk
///   Focusable - participates in Tab / Shift+Tab traversal
enum class EWidgetFocusPolicy : uint8_t
{
    None,
    Focusable,
};

struct WidgetTree;
struct WidgetAttachment;
struct UIElement;

using UIElementRef = std::shared_ptr<UIElement>;

class UIFrameBuilder;

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
    YA_REFLECT_BEGIN(UIElement)
    YA_REFLECT_FIELD(_name)
    YA_REFLECT_FIELD(_position, .instanceEditable())
    YA_REFLECT_FIELD(_size, .instanceEditable())
    YA_REFLECT_FIELD(_visibility, .instanceEditable())
    YA_REFLECT_FIELD(_zOrder, .instanceEditable())
    YA_REFLECT_FIELD(_anchorMin, .instanceEditable())
    YA_REFLECT_FIELD(_anchorMax, .instanceEditable())
    // _pivot is reserved (rotation/scale not implemented): authorable but not
    // per-instance overridable yet.
    YA_REFLECT_FIELD(_pivot)
    YA_REFLECT_FIELD(_hitFilter, .instanceEditable())
    YA_REFLECT_FIELD(_focusPolicy, .instanceEditable())
    YA_REFLECT_FIELD(_bAutoSize, .instanceEditable())
    YA_REFLECT_END()

    explicit UIElement(std::string name = "Widget");
    virtual ~UIElement();

    UIElement(const UIElement&)            = delete;
    UIElement& operator=(const UIElement&) = delete;

    // === Identity ===
    std::string _name;
    /// Stable registry type ID, set by UITypeRegistry::createInstance (empty
    /// for framework-internal / direct make_shared instances).
    std::string _typeId;

    /// Runtime type identity for reflection-based field serialization
    /// (UIDocument). Registry owns the authoring type ID; this is the C++
    /// class identity.
    [[nodiscard]] virtual type_index_t getTypeIndex() const { return ya::type_index_v<UIElement>; }

    // === Field serialization (UIDocument / authoring) ===
    /// Serialize reflected fields (base + own) into a JSON object.
    [[nodiscard]] nlohmann::json serializeFields() const;
    /// Restore reflected fields from a JSON object (base + own).
    void deserializeFields(const nlohmann::json& fields);

    // === Visual / layout / input properties ===
    glm::vec2          _position   = {0.0f, 0.0f}; // Offset (px) from the anchor point within the parent rect
    glm::vec2          _size       = {100.0f, 50.0f};
    EWidgetVisibility  _visibility = EWidgetVisibility::Visible;
    int                _zOrder     = 0;
    glm::vec2          _anchorMin  = {0.0f, 0.0f}; // Fraction of the parent rect (clamped 0..1)
    glm::vec2          _anchorMax  = {0.0f, 0.0f};
    glm::vec2          _pivot      = {0.5f, 0.5f}; // Reserved; unused until rotation/scale exists
    EWidgetHitFilter   _hitFilter  = EWidgetHitFilter::Pass;
    /// Keyboard focus participation (Tab traversal). Default None: plain
    /// widgets never take focus.
    EWidgetFocusPolicy _focusPolicy = EWidgetFocusPolicy::None;
    /// SizeToContent (Slate DesiredSize model): when set, the final size on
    /// each axis resolves from computeDesiredSize() (recursively aggregated
    /// from content children) instead of _size. Resolution precedence per
    /// axis: anchor stretch (anchorMin/Max span) > AutoSize > _size. Default
    /// off so existing explicit-size widgets are unaffected. Container packing
    /// always uses desired size for the main axis regardless of this flag;
    /// the flag only governs the widget's own size when it resolves its own
    /// rect (anchor layout) or when a parent assigns an auto-sized slot.
    bool _bAutoSize = false;

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
    /// Container-assigned layout: take `rect` verbatim (no anchor math) and
    /// run this element's own arrangement (containers re-arrange children,
    /// scroll viewports shift their content, plain elements lay children out
    /// with anchor math). Virtual so nested containers / split panes /
    /// scroll viewports keep their custom layout when they receive an
    /// assigned rect from a parent container.
    virtual void layoutAssigned(const Rect2D& rect);
    /// Desired size for container arrangement (leaf = _size; auto-size text =
    /// measured text; containers aggregate children).
    [[nodiscard]] virtual glm::vec2 computeDesiredSize() const;

    // === Paint (after layout; records resolved draw items into the frame) ===
    /// Records this element and its subtree into `builder`. Runs before the
    /// RenderGraph is built; never during command recording.
    virtual void paint(UIFrameBuilder& builder);

    // === Events (hit-tested by WidgetTree's topmost-first walker) ===
    /// Return true to consume the event. `ctx.logicalPoint` is in tree-local
    /// logical pixels; hit-test against _layoutRect.
    virtual bool handleInputEvent(const Event& event, const WidgetEventContext& ctx);
    /// Clear transient input state (e.g. button hover) before a MouseMoved
    /// hit-test pass.
    virtual void resetHoverState() {}
    /// Clear ALL transient input state (hover / press / drag session). Called
    /// by WidgetTree when a subtree is detached while the tree still points
    /// into it (focus / capture / hover), so stale input state never survives
    /// a re-attach. Base clears hover only.
    virtual void clearTransientInputState() { resetHoverState(); }
    /// Focus lifecycle hooks (called by WidgetTree::setFocus).
    virtual void onFocusGained() {}
    virtual void onFocusLost() {}
    /// Whether child hits are culled to this widget's own rect (scroll
    /// viewports / clipped containers). Base: children hit-test freely, even
    /// outside the parent rect.
    [[nodiscard]] virtual bool cullsChildHits(const glm::vec2& /*logicalPoint*/) const { return false; }

    // === Drag & drop target hooks (gui-app-bootstrap Phase 4) ===
    /// Whether this widget accepts a drag payload at `logicalPoint` (the
    /// tree highlights it as a valid drop target during a drag session).
    [[nodiscard]] virtual bool canAcceptDrop(const std::string& /*payload*/,
                                             const glm::vec2& /*logicalPoint*/) { return false; }
    /// Called when a drag session is released over this target (only after
    /// canAcceptDrop returned true for that point).
    virtual void onDrop(const std::string& /*payload*/, const glm::vec2& /*logicalPoint*/) {}
    /// Visual feedback while the drag hovers this target (cleared on leave /
    /// drop / cancel).
    virtual void setDropHighlight(bool /*bHighlight*/) {}

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

    /// Authoring-only: attach a child to a detached subtree (UIDocument
    /// instantiate). The child must not be attached anywhere; tree membership
    /// is assigned when the subtree root is attached to a WidgetTree.
    void addDetachedChild(const UIElementRef& child);

  protected:
    /// Anchor math: rect.min = parent.pos + parent.size*anchorMin + _position;
    /// rect.max = parent.pos + parent.size*anchorMax + _position + _size.
    [[nodiscard]] Rect2D computeAnchorRect(const Rect2D& parentRect) const;
    /// Lay out direct children within `layoutRect` (paint order).
    void layoutChildren(const Rect2D& layoutRect);
    /// Recursively paint children in paint order.
    void paintChildren(UIFrameBuilder& builder);
    /// Subclasses draw themselves here (base: no-op).
    virtual void paintSelf(UIFrameBuilder& builder) { (void)builder; }

  private:
    friend struct WidgetTree;
    friend class UITypeRegistry;
    friend struct UIDocument;

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
