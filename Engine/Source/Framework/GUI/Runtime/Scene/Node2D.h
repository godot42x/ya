#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Event.h"
#include "Core/Reflection/Reflection.h"
#include "GUI/Runtime/UIBase.h"
#include "GUI/Runtime/Scene/Node.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

// ============================================================================
// Node2D - screen-space 2D/UI node in the unified scene tree
// ============================================================================
//
// Design philosophy (Godot-style):
// - Node2D lives in the SAME scene tree as Node3D (Scene::_rootNode).
// - It is a pure tree node: NO ECS entity, NO components. Ownership lives in
//   Scene::_entityLessNodes.
// - Transform semantics: 2D position accumulates along ancestor Node2D chains
//   only; Node3D ancestors contribute no 2D transform (Godot Control semantics).
//
// Pipeline (Slate-style, single tree):
// - layout(parentRect): computes _layoutRect (canvas logical space) from the
//   parent rect via anchors, then recurses into children (containers arrange).
// - paint(ctx): each node records its own commands into the active Render2D
//   batch, then recurses into children in paint order.
// - handleInputEvent(ctx): hit-tested by the renderer's topmost-first walker
//   (children before parent, zOrder descending).
// ============================================================================

enum class EUIAlignH : uint8_t
{
    Left,
    Center,
    Right,
};

enum class EUIAlignV : uint8_t
{
    Top,
    Center,
    Bottom,
};

/// Per-node game-UI event routing policy at the game boundary (Godot
/// mouse_filter semantics, minus hit-test gating which lives in
/// EUIVisibility): Pass nodes respond but never block; Stop nodes consume
/// exclusively.
enum class EUIHitFilter : uint8_t
{
    Pass,   // (default) hit-testable, responds, but the event keeps flowing
            // to lower nodes and the game (panels, text, canvas).
    Stop,   // hit = exclusively consume: neither lower UI nodes nor the game
            // receive the event (buttons).
};

/// Render / hit-test / layout state (UMG Visibility semantics). The three
/// axes are packed into curated states so no illegal combination is
/// expressible; per-axis predicates live on Node2D:
///   render:  isVisibleForRender()
///   hit:     isHitTestableSelf() / isHitTestableSubtree()
///   layout:  participatesInLayout()
enum class EUIVisibility : uint8_t
{
    Visible,             // render + self hit + children hit + layout space
    Hidden,              // no render, no hit; keeps layout space
    Collapsed,           // no render, no hit; no layout space
    HitTestInvisible,    // renders; self not hittable, children still are
    SelfHitTestInvisible // renders; the whole subtree is not hittable
};

struct YA_GUI_API Node2D : public Node
{
    YA_REFLECT_BEGIN(Node2D, Node)
    YA_REFLECT_FIELD(_position)
    YA_REFLECT_FIELD(_size)
    YA_REFLECT_FIELD(_visibility)
    YA_REFLECT_FIELD(_zOrder)
    YA_REFLECT_FIELD(_anchorMin)
    YA_REFLECT_FIELD(_anchorMax)
    YA_REFLECT_FIELD(_pivot)
    YA_REFLECT_FIELD(_hitFilter)
    YA_REFLECT_END()

    glm::vec2 _position = {0.0f, 0.0f}; // Offset (px) from the anchor point within the parent rect
    glm::vec2 _size     = {100.0f, 50.0f};
    EUIVisibility _visibility = EUIVisibility::Visible;
    int       _zOrder   = 0;

    // Anchored layout (Godot Control semantics). Default {0,0} keeps the
    // legacy absolute layout: rect.min = parent.pos + _position, size = _size.
    glm::vec2 _anchorMin = {0.0f, 0.0f}; // Fraction of the parent rect (clamped 0..1)
    glm::vec2 _anchorMax = {0.0f, 0.0f};
    glm::vec2 _pivot     = {0.5f, 0.5f}; // Reserved; unused until rotation/scale exists
    EUIHitFilter _hitFilter = EUIHitFilter::Pass;

    // Layout cache: final rect in canvas logical space, computed by the layout
    // pass each frame. Not reflected / serialized.
    Rect2D _layoutRect{};

    explicit Node2D(std::string name = "Node2D") : Node(std::move(name), nullptr) {}
    ~Node2D() override = default;

    [[nodiscard]] bool is2D() const override { return true; }

    // === Type identity (used by serialization / factory) ===
    [[nodiscard]] virtual type_index_t getTypeIndex() const { return ya::type_index_v<Node2D>; }
    [[nodiscard]] virtual const char*  getUITypeName() const { return "Node2D"; }

    // === Layout (called by the renderer's layout pass, top-down) ===
    /// Compute this node's rect within `parentRect` (anchor math) and store it
    /// in `_layoutRect`, then lay out children in paint order (zOrder-stable).
    /// Non-2D children are traversed with the same anchor rect (Node3D
    /// ancestors contribute no 2D transform). Containers override to arrange
    /// children; UICanvasNode overrides to fill the assigned area.
    virtual void layout(const Rect2D& parentRect);

    /// Container-assigned layout: take `rect` verbatim (no anchor math) and
    /// lay out this node's children within it. Used by containers, which
    /// override per-child anchor positioning (Godot fit_child_in_rect).
    void layoutAssigned(const Rect2D& rect);

    /// Desired size for container arrangement (leaf = _size; auto-size text =
    /// measured text; containers aggregate children).
    [[nodiscard]] virtual glm::vec2 computeDesiredSize() const;

    // === Paint (after layout; records commands into the active batch) ===
    /// Checks isVisibleForRender() (subtree cull), draws self via
    /// paintSelf(), then recursively paints children in paint order.
    virtual void paint(const UIPaintContext& ctx);

    // === Events (hit-tested by the renderer's topmost-first walker) ===
    /// Return true to consume the event. `ctx.canvasPoint` is in canvas
    /// logical space; hit-test against _layoutRect.
    virtual bool handleInputEvent(const Event& event, const UIEventContext& ctx);

    /// Clear transient input state (e.g. button hover) before a MouseMoved
    /// hit-test pass.
    virtual void resetHoverState() {}

    // === Screen-space helpers (top-left origin, Y down) ===
    /// Whether this node is drawn (Hidden / Collapsed cull the subtree).
    [[nodiscard]] bool isVisibleForRender() const
    {
        return _visibility != EUIVisibility::Hidden && _visibility != EUIVisibility::Collapsed;
    }

    /// Whether this node itself can receive hits (Visible only; ancestors
    /// are handled by the walker's subtree cull).
    [[nodiscard]] bool isHitTestableSelf() const
    {
        return _visibility == EUIVisibility::Visible;
    }

    /// Whether hits can land anywhere in this subtree (Hidden, Collapsed and
    /// SelfHitTestInvisible cull the whole subtree).
    [[nodiscard]] bool isHitTestableSubtree() const
    {
        return _visibility == EUIVisibility::Visible ||
               _visibility == EUIVisibility::HitTestInvisible;
    }

    /// Whether this node participates in container layout (Collapsed takes
    /// no space; Hidden keeps its space, UMG semantics).
    [[nodiscard]] bool participatesInLayout() const
    {
        return _visibility != EUIVisibility::Collapsed;
    }

    /// Whether this node is effectively rendered: itself and every 2D
    /// ancestor pass isVisibleForRender() (Godot is_visible_in_tree
    /// semantics; Node3D ancestors have no visibility concept).
    [[nodiscard]] bool isVisibleInTree() const;

    /// Whether the UI walker would descend into this node's subtree given
    /// the ancestor chain: self passes isHitTestableSubtree() and no 2D
    /// ancestor culls hits (Hidden / Collapsed / SelfHitTestInvisible).
    /// Note: the paint/hit/pick walkers already enforce this by construction;
    /// this query is for game code that needs the effective state.
    [[nodiscard]] bool isHitTestableInTree() const;

    /// Accumulated position along the ancestor Node2D chain.
    [[nodiscard]] glm::vec2 getScreenPosition() const;
    /// Own-rect hit test in screen space. Pure geometry: only checks this
    /// node's own state and rect — ancestor visibility/hit culling belongs
    /// to the walkers (see isVisibleInTree / isHitTestableInTree).
    [[nodiscard]] bool hitTest(const glm::vec2& screenPoint) const;
    /// Hit test against the cached layout rect (canvas logical space).
    [[nodiscard]] bool hitTestLayoutRect(const glm::vec2& canvasPoint) const;

    /// Children in paint order: stable sort by _zOrder ascending; non-2D
    /// children key 0. Event walkers iterate this list in reverse.
    [[nodiscard]] std::vector<Node*> getChildrenInPaintOrder() const;

    // === Reflection field serialization ===
    [[nodiscard]] nlohmann::json serializeFields() const;
    void                        deserializeFields(const nlohmann::json& fields);

  protected:
    /// Anchor math: rect.min = parent.pos + parent.size*anchorMin + _position;
    /// rect.max = parent.pos + parent.size*anchorMax + _position + _size.
    [[nodiscard]] Rect2D computeAnchorRect(const Rect2D& parentRect) const;

    /// Lay out direct 2D children within `layoutRect` (paint order); non-2D
    /// children keep searching for 2D descendants with the same rect.
    void layoutChildren(const Rect2D& layoutRect);

    /// Recursively paint 2D children in paint order (subtree cull applied by
    /// each child's own paint()).
    void paintChildren(const UIPaintContext& ctx);

    /// Subclasses draw themselves here (base: no-op).
    virtual void paintSelf(const UIPaintContext& ctx) {}

  protected:
    static void layoutTransparent(Node* node, const Rect2D& anchorRect);
    static void paintTransparent(Node* node, const UIPaintContext& ctx);
};

/// Canvas root: fills the viewport / assigned area. Children are laid out in
/// its space.
struct YA_GUI_API UICanvasNode : public Node2D
{
    YA_REFLECT_BEGIN(UICanvasNode, Node2D)
    YA_REFLECT_END()

    explicit UICanvasNode(std::string name = "Canvas") : Node2D(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UICanvasNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UICanvasNode"; }

    /// Canvas roots fill the assigned area regardless of _size/_position.
    void layout(const Rect2D& parentRect) override;
};

/// Flat panel: solid color and/or image, optional 9-slice border.
struct YA_GUI_API UIPanelNode : public Node2D
{
    YA_REFLECT_BEGIN(UIPanelNode, Node2D)
    YA_REFLECT_FIELD(_color)
    YA_REFLECT_FIELD(_image)
    YA_REFLECT_FIELD(_bNineSlice)
    YA_REFLECT_FIELD(_nineSliceBorder)
    YA_REFLECT_END()

    glm::vec4  _color            = {0.2f, 0.2f, 0.2f, 0.8f};
    TextureRef _image;
    bool       _bNineSlice       = false;
    glm::vec4  _nineSliceBorder  = {8.0f, 8.0f, 8.0f, 8.0f}; // l, t, r, b in pixels

    explicit UIPanelNode(std::string name = "Panel") : Node2D(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIPanelNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UIPanelNode"; }

    void paintSelf(const UIPaintContext& ctx) override;
};

/// Text element rendered through the font atlas.
struct YA_GUI_API UITextNode : public Node2D
{
    YA_REFLECT_BEGIN(UITextNode, Node2D)
    YA_REFLECT_FIELD(_text)
    YA_REFLECT_FIELD(_fontSize)
    YA_REFLECT_FIELD(_color)
    YA_REFLECT_FIELD(_hAlign)
    YA_REFLECT_FIELD(_vAlign)
    YA_REFLECT_FIELD(_bAutoSize)
    YA_REFLECT_END()

    std::string   _text       = "Text";
    uint32_t      _fontSize   = 16;
    glm::vec4     _color      = {1.0f, 1.0f, 1.0f, 1.0f};
    EUIAlignH     _hAlign     = EUIAlignH::Left;
    EUIAlignV     _vAlign     = EUIAlignV::Top;
    bool          _bAutoSize  = false; // Measure the layout rect from the text

    explicit UITextNode(std::string name = "Text") : Node2D(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITextNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UITextNode"; }

    void paintSelf(const UIPaintContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;
};

/// Button: panel style with hover/pressed states. Click callback is runtime-only
/// (not serialized); hit testing is driven by the UI walker.
struct YA_GUI_API UIButtonNode : public Node2D
{
    YA_REFLECT_BEGIN(UIButtonNode, Node2D)
    YA_REFLECT_FIELD(_normalColor)
    YA_REFLECT_FIELD(_hoveredColor)
    YA_REFLECT_FIELD(_pressedColor)
    YA_REFLECT_END()

    glm::vec4 _normalColor  = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec4 _hoveredColor = {0.6f, 0.6f, 0.6f, 1.0f};
    glm::vec4 _pressedColor = {0.4f, 0.4f, 0.4f, 1.0f};

    // Runtime-only state (not serialized)
    bool                   _bHovered = false;
    bool                   _bPressed = false;
    std::function<void()>  _onClick;

    explicit UIButtonNode(std::string name = "Button") : Node2D(std::move(name))
    {
        _hitFilter = EUIHitFilter::Stop;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIButtonNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UIButtonNode"; }

    void paintSelf(const UIPaintContext& ctx) override;
    bool handleInputEvent(const Event& event, const UIEventContext& ctx) override;
    void resetHoverState() override { _bHovered = false; }
};

enum class EUIBoxLayout : uint8_t
{
    Horizontal,
    Vertical,
};

/// Box container: arranges children left-to-right (Horizontal) or top-to-bottom
/// (Vertical) by their desired sizes, with uniform spacing and padding.
/// Children can be clipped to the content rect via _bClipChildren.
struct YA_GUI_API UIContainerNode : public Node2D
{
    YA_REFLECT_BEGIN(UIContainerNode, Node2D)
    YA_REFLECT_FIELD(_direction)
    YA_REFLECT_FIELD(_spacing)
    YA_REFLECT_FIELD(_padding)
    YA_REFLECT_FIELD(_bClipChildren)
    YA_REFLECT_END()

    EUIBoxLayout _direction     = EUIBoxLayout::Horizontal;
    float        _spacing       = 4.0f;
    float        _padding       = 0.0f;
    bool         _bClipChildren = false;

    explicit UIContainerNode(std::string name = "Container") : Node2D(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIContainerNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UIContainerNode"; }

    void layout(const Rect2D& parentRect) override;
    void paint(const UIPaintContext& ctx) override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    /// Arrange 2D children into a box within `contentRect`; non-2D branches
    /// keep searching for 2D descendants against the container rect.
    void arrangeChildren(const Rect2D& contentRect);
};

/// Create a Node2D subclass by its UI type name (used by scene deserialization
/// and PIE clone). Returns nullptr for unknown types.
std::shared_ptr<Node2D> createNode2DByTypeName(const std::string& typeName, const std::string& name);

/// Every registered Node2D subclass, as short type names, sorted. Driven by
/// ClassRegistry so new node types appear without touching this file.
std::vector<std::string> getRegisteredUINodeTypeNames();

} // namespace ya
