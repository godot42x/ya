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
#include "GUI/Widgets/UIBehavior.h"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
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

/// Mouse cursor the window host should show while this widget is hovered.
/// Mapped to a system cursor by the host (SDL system cursors); the default
/// is the arrow and split panes request a resize cursor over their divider.
enum class ECursorType : uint8_t
{
    Arrow,            // default pointer
    ResizeEastWest,   // vertical divider (left/right panes)
    ResizeNorthSouth, // horizontal divider (top/bottom panes)
};

struct WidgetTree;
struct UIDragDropOperation
{
    std::string typeId = "text";
    std::string payload;
    std::string ghostLabel;
};
using UIDragDropOperationRef = std::shared_ptr<UIDragDropOperation>;
struct FDragDetectedEvent
{
    glm::vec2 startPoint{};
    glm::vec2 currentPoint{};
};
struct DragSessionObserver;
struct WidgetAttachment;
struct UIElement;
class UISlot;
class UILayout;
class ReactiveBase;

using UIElementRef = std::shared_ptr<UIElement>;
using FChildSlotInitializer = std::function<void(UIElement&, UISlot&)>;

class UIFrameBuilder;

/// One delivery stage in an explicit WidgetTree route. Existing controls
/// continue to receive target delivery through handleInputEvent(); preview
/// and bubble hooks are introduced separately so the route model can grow
/// without duplicating leaf-control behavior.
enum class EWidgetEventRoutePhase : uint8_t
{
    Preview,
    Target,
    Bubble,
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
    EWidgetEventRoutePhase phase = EWidgetEventRoutePhase::Target;
};

/// Why a widget was invalidated (diagnostics, GI-001). An enum, not a string,
/// so the hot path never allocates: names resolve only when a trace is dumped.
/// Recorded on the widget (_lastInvalidationReason) and aggregated by the
/// owning tree into transition counters.
enum class EUIInvalidationReason : uint8_t
{
    None,                  // no invalidation observed (clean frame)
    PaintProperty,         // transient/visual property write (e.g. VisualFlag)
    LayoutProperty,        // layout-affecting property write
    ReactivePaint,         // ReactiveBase::notifyDependents at Paint granularity
    ReactiveLayout,        // ReactiveBase::notifyDependents at Layout granularity
    ChildStructure,        // attach/detach/reparent -> tree layout invalidation
    GeometryChanged,       // setLayoutRect detected a rect move/resize
    BuildContextChanged,   // build context (scale/offset) cache invalidation
    InheritedPaintContext, // inherited paint context (clip/visibility) invalidation
    Volatile,              // _bVolatile per-frame rebuild (implicit, not a transition)
};

/// Declared invalidation scope of a property write (GI-104). Each runtime
/// property has exactly one stable impact contract: its setter declares the
/// impact here instead of choosing a dirty API directly, so a caller cannot
/// silently downgrade a Layout change to a Paint-only repaint.
enum class EUIPropertyImpact : uint8_t
{
    None,                // no invalidation (e.g. runtime-only callback storage)
    Paint,               // repaint this widget only
    Layout,              // re-run measure + arrange (implies repaint)
    SubtreePaintContext, // repaint this widget and its whole subtree (clip/visibility)
};

struct YA_GUI_API UIElement : public std::enable_shared_from_this<UIElement>
{
    YA_REFLECT_BEGIN(UIElement)
    YA_REFLECT_FIELD(_name)
    YA_REFLECT_FIELD(_position, .instanceEditable())
    YA_REFLECT_FIELD(_size, .instanceEditable())
    YA_REFLECT_FIELD(_visibility, .instanceEditable())
    YA_REFLECT_FIELD(_zOrder, .instanceEditable())
    // _anchorMin/_anchorMax are no longer authorable: stretch geometry lives on
    // the parent->child slot edge (UICanvasSlot), never on the child. They
    // remain runtime-only layout I/O consumed via the slot.
    // _pivot is reserved (rotation/scale not implemented): authorable but not
    // per-instance overridable yet.
    YA_REFLECT_FIELD(_pivot)
    YA_REFLECT_FIELD(_hitFilter, .instanceEditable())
    YA_REFLECT_FIELD(_focusPolicy, .instanceEditable())
    YA_REFLECT_FIELD(_bAutoSize, .instanceEditable())
    YA_REFLECT_FIELD(_styleKey, .instanceEditable())
    YA_REFLECT_END()

    explicit UIElement(std::string name = "Widget", std::string styleKey = {});
    virtual ~UIElement();

    UIElement(const UIElement&)            = delete;
    UIElement& operator=(const UIElement&) = delete;

    /// Lifecycle hook invoked by WidgetTree immediately before this widget
    /// (and its subtree) is attached. Compound widgets use it to construct
    /// their internal DSL exactly once.
    virtual void prepareForAttach() {}
    /// Lifecycle hook invoked after this widget has joined a WidgetTree.
    virtual void onAttached() {}
    /// Lifecycle hook invoked before this widget leaves its WidgetTree.
    virtual void onDetached() {}
    /// Optional frame-driven lifecycle. WidgetTree invokes this once per
    /// frame for attached widgets that opt in through wantsTick().
    virtual void tick(float deltaSeconds);
    [[nodiscard]] virtual bool wantsTick() const;

    void addBehavior(const UIBehaviorRef& behavior);
    void removeBehavior(const UIBehavior& behavior);
    [[nodiscard]] bool hasBehavior(const UIBehavior& behavior) const;

    // === Identity ===
    std::string _name;
    /// Optional stable identity for dumps and list-row reuse. Empty for
    /// hand-authored widgets; `ui::build` copies the builder key here.
    std::string _stableKey;
    /// Stable registry type ID, set by UITypeRegistry::createInstance (empty
    /// for framework-internal / direct make_shared instances).
    std::string _typeId;
    /// Theme catalog key. Empty disables theme lookup. Styled subclasses pass
    /// their family default through the constructor ("button", "panel", ...).
    std::string _styleKey;

    /// Runtime type identity for reflection-based field serialization
    /// (UIDocument). Registry owns the authoring type ID; this is the C++
    /// class identity.
    [[nodiscard]] virtual type_index_t getTypeIndex() const { return ya::type_index_v<UIElement>; }

    // === Field serialization (UIDocument / authoring) ===
    /// Serialize reflected fields (base + own) into a JSON object.
    [[nodiscard]] nlohmann::json serializeFields() const;
    /// Restore reflected fields from a JSON object (base + own).
    virtual void deserializeFields(const nlohmann::json& fields);

    /// Authored TStyle lives on UIStyledWidget (second base). Reflection
    /// member pointers cannot address that subobject from a UIElement*, so
    /// persistence goes through this virtual instead of YA_REFLECT_FIELD.
    [[nodiscard]] virtual nlohmann::json serializeAuthoredStyle() const { return nullptr; }
    virtual void deserializeAuthoredStyle(const nlohmann::json&) {}

    /// Append transient/runtime-only diagnostics for WidgetTreeDump. This is
    /// intentionally separate from serializeFields(): diagnostics include
    /// live control state and must never become authoring persistence.
    virtual void appendRuntimeDiagnostics(nlohmann::json& node,
                                          const WidgetTree& tree) const;
    virtual void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const;

    // === Visual / layout / input properties ===
  protected:
    // Runtime mutable visual/layout properties (GI-202): encapsulated behind
    // changed-only setters setPosition/setSize/setVisibility. Subclasses read
    // these freely in paint/layout/computeDesiredSize; external runtime writes
    // must go through the setters. Authoring/reflection writes go through the
    // mutation transaction (deserializeFields).
    glm::vec2         _position   = {0.0f, 0.0f}; // Offset (px) from the anchor point within the parent rect
    glm::vec2         _size       = {100.0f, 50.0f};
    EWidgetVisibility _visibility = EWidgetVisibility::Visible;
  public:
    // Authoring-only configuration (GI-202 exception list): no runtime
    // business write path yet; kept public for authoring/reflection. To be
    // encapsulated when they gain a changed-only setter.
    int               _zOrder     = 0;
    glm::vec2         _anchorMin  = {0.0f, 0.0f}; // Fraction of the parent rect (clamped 0..1)
    glm::vec2         _anchorMax  = {0.0f, 0.0f};
    glm::vec2         _pivot      = {0.5f, 0.5f}; // Reserved; unused until rotation/scale exists
    EWidgetHitFilter  _hitFilter  = EWidgetHitFilter::Pass;
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

    /// One-shot guard for reportStretchAnchorsIgnored(): a path-A parent
    /// already reported that this child's stretch anchors are being dropped.
    /// Mutable because the report happens during a const measure/arrange pass.
    mutable bool _bStretchAnchorsWarned = false;

    /// Volatile (Slate-style): re-run this widget's paintSelf every frame,
    /// bypassing the draw-item cache, regardless of dirty state. The
    /// consistency backstop for widgets whose presentation state is written
    /// from outside the invalidation chain (e.g. a presenter that copies
    /// model state into widget fields each frame). Costs the incremental-reuse
    /// win for this widget; keeps data/display consistency the invariant.
    bool _bVolatile = false;

    /// Enabled state (editor-parity P6). A disabled widget (or one inside a
    /// disabled ancestor) does not receive input; paint graying is left to
    /// each widget (or the host styles the disabled subtree).
    bool _bEnabled = true;
    /// Changed-only setter: disables/enables this widget and repaints its
    /// subtree (visual state may change for every descendant).
    void setEnabled(bool value)
    {
        if (_bEnabled == value) {
            return;
        }
        _bEnabled = value;
        invalidateSubtree(EUIInvalidationReason::PaintProperty);
    }
    [[nodiscard]] bool isEnabled() const { return _bEnabled; }
    /// Effective enabled state: false when this widget OR any ancestor is
    /// disabled. Walked on the input path (cheap parent chain).
    [[nodiscard]] bool isEnabledInTree() const
    {
        for (const UIElement* node = this; node; node = node->getParent()) {
            if (!node->_bEnabled) {
                return false;
            }
        }
        return true;
    }

    /// Tooltip text (editor-parity P6). When non-empty, the tree shows a
    /// styled tooltip below this widget after a hover dwell delay.
    std::string _tooltip;
    void setTooltip(std::string text) { _tooltip = std::move(text); }
    [[nodiscard]] const std::string& getTooltip() const { return _tooltip; }

    /// Guardrail G1: paint() clips this widget's own paint + children to its
    /// layout rect by default, so a widget can never draw over its siblings.
    /// Set false only for widgets that legitimately paint outside their rect
    /// (rare; prefer moving that paint to a layer).
    bool _bSelfClip = true;

    /// Layout cache: final rect in tree-local logical pixels, computed by the
    /// layout pass. Not serialized.
    Rect2D _layoutRect{};

    /// The layout this element hosts, or nullptr for a non-host element (which
    /// keeps the legacy self-positioned behaviour). Owned here when installed
    /// via installLayout(); never serialized.
    UILayout* _layout = nullptr;
    std::unique_ptr<UILayout> _ownedLayout;

    // === Tree membership (managed by WidgetTree only) ===
    /// Owning tree, or nullptr while detached.
    [[nodiscard]] WidgetTree* getTree() const { return _tree; }
    /// Visual parent (non-owning), or nullptr while detached.
    [[nodiscard]] UIElement* getParent() const { return _parent; }
    [[nodiscard]] bool       isAttached() const { return _tree != nullptr; }
    /// Children in attachment order (paint order = getChildrenInPaintOrder).
    [[nodiscard]] const std::vector<UIElementRef>& getChildren() const { return _children; }
    [[nodiscard]] const std::vector<UIBehaviorRef>& getBehaviors() const { return _behaviors; }
    /// Children stably sorted by _zOrder ascending.
    [[nodiscard]] std::vector<UIElement*> getChildrenInPaintOrder() const;
    /// Active parent-child edge object, or null only for the internal root /
    /// a detached widget. Slots are owned by the visual parent, never by the
    /// child, and are recreated on reparent.
    [[nodiscard]] UISlot* getSlot() const;
    [[nodiscard]] UISlot* getSlotForChild(const UIElement& child) const;

    // === Layout (top-down, called by WidgetTree::layout) ===
    //
    // Two contracts decide a child's size, and which one applies is decided by
    // the PARENT, not the child:
    //
    //   Path A - parent owns arrangement (box / scroll / split / overlay /
    //            size box / button ...). The parent overrides layoutAssigned()
    //            and assigns every child rect itself from slot data. The
    //            child's anchors are IGNORED. Child intent lives in the slot.
    //
    //   Path B - child self-positions (panel / selectable row / plain
    //            elements). The parent keeps the base layoutChildren(), which
    //            calls child->layout(parentRect) and lets the child run its
    //            own anchor math. The base UISlot carries no layout data, so
    //            there is nothing for the parent to honour.
    //
    // The two paths are mutually exclusive on purpose: a child never has two
    // competing stretch mechanisms. A parent that arranges children MUST route
    // those rect assignments through UILayout::assignChildRect() (or report
    // via reportStretchAnchorsIgnored()) so dropped anchor intent is reported
    // instead of silently discarded.
    //
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

    // —— Layout host hook ——
    //
    // Any element can install a layout. When one is installed, this element is a
    // HOST: it arranges its children through that layout (and measures through
    // it) instead of falling back to the legacy self-positioned path. This is
    // what removes the path-A / path-B split: "has a layout" is the only
    // distinction, and Canvas is just one layout among Box / Split / Table ...
    [[nodiscard]] UILayout* getLayout() const { return _layout; }

    /// Install a layout owned by this element (transfers ownership).
    void installLayout(std::unique_ptr<UILayout> layout);

    /// True when the anchors declare a stretch intent (min != max on either
    /// axis). Non-zero anchor span is one of only two things that stretch a
    /// widget; a zero component in `_size` is NOT one of them.
    [[nodiscard]] bool hasStretchAnchors() const
    {
        return _anchorMin.x != _anchorMax.x || _anchorMin.y != _anchorMax.y;
    }

    /// Path-A parents call this before assigning a rect: if the child declared
    /// stretch anchors that path A ignores by contract, report it once.
    /// const because layout algorithms run against a const parent state.
    void reportStretchAnchorsIgnored() const;

    // === Paint (after layout; records resolved draw items into the frame) ===
    /// Records this element and its subtree into `builder`. Runs before the
    /// RenderGraph is built; never during command recording.
    virtual void paint(UIFrameBuilder& builder);

    // === Events (hit-tested by WidgetTree's topmost-first walker) ===
    /// Return true to consume the event. `ctx.logicalPoint` is in tree-local
    /// logical pixels; hit-test against _layoutRect.
    virtual bool handleInputEvent(const Event& event, const WidgetEventContext& ctx);
    /// Root-to-target route hook. The default is deliberately passive:
    /// existing controls retain their target behavior until they explicitly
    /// opt into preview semantics.
    virtual bool previewInputEvent(const Event& event, const WidgetEventContext& ctx);
    /// Target-to-root route hook. The default preserves the established
    /// parent handling behavior for controls such as nested scroll viewports.
    virtual bool bubbleInputEvent(const Event& event, const WidgetEventContext& ctx);
    /// Whether this widget presents hover feedback and is therefore an
    /// eligible hover target for the tree's hover tracking. Plain text,
    /// containers and popup shields are NOT hoverable (their children/other
    /// layers must not steal hover from the real interactive leaf); buttons /
    /// menu entries / split dividers are. The tree resolves the deepest
    /// hoverable widget under the pointer from this flag, so a button's text
    /// child reports the button (not the text) as the hovered widget.
    [[nodiscard]] virtual bool isHoverable() const { return false; }
    /// Whether this widget is transparent to hover despite being hit-testable
    /// for pointer presses. A non-modal popup shield is the canonical case: it
    /// is drawn transparent (invisible to the user), must still swallow
    /// presses (to dismiss the popup), but must NOT steal hover from the
    /// visible widget beneath it (a menu bar item keeps hover-switch alive
    /// while a menu is open). Modal shields and ordinary widgets keep the
    /// default (false) so they block hover as well as presses.
    [[nodiscard]] virtual bool isHoverTransparent() const { return false; }
    /// Cursor to show while this widget is hovered (queried by the host from
    /// the tree's hovered widget). Base: arrow; split panes override it.
    [[nodiscard]] virtual ECursorType getCursor() const { return ECursorType::Arrow; }
    /// Pointer hover lifecycle. The tree resolves a single hover owner and
    /// sends enter/leave when that owner changes. Default leave behavior keeps
    /// the legacy contract alive by clearing the widget's hover visuals.
    virtual void onPointerEnter() {}
    virtual void onPointerLeave() { resetHoverState(); }
    /// Legacy hover-clear hook. Kept as a compatibility layer while controls
    /// gradually migrate to explicit enter/leave lifecycle handling.
    virtual void resetHoverState() {}
    /// Clear ALL transient input state (hover / press / drag session). Called
    /// by WidgetTree when a subtree is detached while the tree still points
    /// into it (focus / capture / hover), so stale input state never survives
    /// a re-attach. Base clears hover only.
    virtual void clearTransientInputState() { resetHoverState(); }
    /// Focus lifecycle hooks (called by WidgetTree::setFocus). `bFromKeyboard`
    /// distinguishes Tab-traversal focus (drives a persistent visual highlight)
    /// from pointer/programmatic focus (logical only, no lingering highlight).
    virtual void onFocusGained(bool /*bFromKeyboard*/) {}
    virtual void onFocusLost() {}
    /// Whether child hits are culled to this widget's own rect (scroll
    /// viewports / clipped containers). Base: children hit-test freely, even
    /// outside the parent rect.
    [[nodiscard]] virtual bool cullsChildHits(const glm::vec2& /*logicalPoint*/) const { return false; }

    // === Drag & drop target hooks (gui-app-bootstrap Phase 4) ===
    /// Whether this widget accepts a drag payload at `logicalPoint` (the
    /// tree highlights it as a valid drop target during a drag session).
    [[nodiscard]] virtual bool canAcceptDrop(const std::string& payload,
                                             const glm::vec2& logicalPoint);
    [[nodiscard]] virtual bool canAcceptDrop(const UIDragDropOperation& operation,
                                             const glm::vec2& logicalPoint);
    /// Called when a drag session is released over this target (only after
    /// canAcceptDrop returned true for that point).
    virtual void onDrop(const std::string& payload, const glm::vec2& logicalPoint);
    virtual void onDrop(const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    /// Visual feedback while the drag hovers this target (cleared on leave /
    /// drop / cancel). Targets with a point-SENSITIVE preview (e.g. a dock
    /// space whose highlight follows the pointer) override updateDropHover
    /// instead.
    virtual void setDropHighlight(bool bHighlight);
    /// Point-sensitive hover feedback: called with the CURRENT drag point on
    /// every pointer move while this widget is the active drop target (the
    /// tree calls it after setDropHighlight(true) and on each move). Default:
    /// no-op — targets without a moving preview keep using setDropHighlight.
    virtual void updateDropHover(const std::string& payload,
                                 const glm::vec2& logicalPoint);
    virtual void updateDropHover(const UIDragDropOperation& operation,
                                 const glm::vec2& logicalPoint);
    /// Start an operation owned by the widget tree. Any UIElement may invoke
    /// this; drag-source widget subclasses are not required.
    bool beginDragOperation(UIDragDropOperationRef operation,
                            bool bShowGhost = true,
                            bool bSkipSourceInHitTest = false);
    virtual UIDragDropOperationRef onDragDetected(const FDragDetectedEvent& event);

    // === Reactive dependency tracking ===
    /// Mark this widget paint-dirty (called by ReactiveBase::notifyDependents).
    /// The next buildSnapshot re-runs this widget's paintSelf instead of
    /// reusing its cached draw-item segment. `reason` records the invalidation
    /// origin for diagnostics, but only on the 0->1 dirty transition.
    void markPaintDirty(EUIInvalidationReason reason = EUIInvalidationReason::None);
    [[nodiscard]] bool isPaintDirty() const { return _bPaintDirty; }
    void clearPaintDirty() { _bPaintDirty = false; }
    /// Most recent invalidation reason recorded on this widget (diagnostics).
    [[nodiscard]] EUIInvalidationReason getLastInvalidationReason() const { return _lastInvalidationReason; }
    /// Mark this widget and its whole subtree paint-dirty (Slate dirty-subtree
    /// semantics): used when a change affects the subtree as a batch instead of
    /// a single widget, e.g. a presenter re-selecting every row in a list.
    /// `reason` tags the invalidation (default InheritedPaintContext).
    void invalidateSubtree(EUIInvalidationReason reason = EUIInvalidationReason::InheritedPaintContext);
    /// Mark this widget layout-dirty: paint-dirty plus an invalidation of the
    /// owning tree's layout (measure + arrange). Implemented in .cpp.
    void markLayoutDirty(EUIInvalidationReason reason = EUIInvalidationReason::None);
    /// Apply a property write's declared impact (GI-104). Setters call this
    /// instead of markPaintDirty/markLayoutDirty/invalidateSubtree directly,
    /// so a property's invalidation scope is a stable contract, not a
    /// per-call-site decision.
    void invalidateProperty(EUIPropertyImpact impact);

    // === Changed-only property setters / getters (GI-105 / GI-202) ===
    // Presenters call these instead of writing the backing field directly, so
    // a same-value write is a no-op and only a real change invalidates at the
    // property's declared impact. `_position`/`_size` are layout inputs
    // (Layout); `_visibility` is SubtreePaintContext unless the Collapsed
    // transition changes whether layout space is kept. The backing fields are
    // protected (GI-202): external readers use the getters.
    [[nodiscard]] const glm::vec2& getPosition() const { return _position; }
    [[nodiscard]] const glm::vec2& getSize() const { return _size; }
    [[nodiscard]] EWidgetVisibility getVisibility() const { return _visibility; }

    void setPosition(const glm::vec2& value);

    void setStyleKey(std::string value)
    {
        if (_styleKey == value) {
            return;
        }
        _styleKey = std::move(value);
        invalidateProperty(EUIPropertyImpact::Layout);
    }

    void setSize(const glm::vec2& value);

    void setVisibility(EWidgetVisibility value)
    {
        if (_visibility == value) {
            return;
        }
        const bool bPrevKeepsSpace = (_visibility != EWidgetVisibility::Collapsed);
        const bool bNextKeepsSpace = (value != EWidgetVisibility::Collapsed);
        _visibility = value;
        invalidateProperty(bPrevKeepsSpace != bNextKeepsSpace
                               ? EUIPropertyImpact::Layout
                               : EUIPropertyImpact::SubtreePaintContext);
    }
    /// Record `ref` as a paint-collected dependency (called by Reactive::get
    /// during the paint walk). Cleared before a dirty widget re-runs its paint.
    void trackPaintDependency(ReactiveBase* ref) { _paintDependencies.insert(ref); }
    /// Record `ref` as a persistent (bind-time) dependency, e.g. split ratio or
    /// style. Survives paint re-collection; removed only by unbind/rebind or
    /// destruction.
    void trackPersistentDependency(ReactiveBase* ref) { _persistentDependencies.insert(ref); }
    /// Remove `ref` from both dependency lists (called by the ref's destructor
    /// so a widget never holds a dangling ref pointer).
    void untrackDependency(ReactiveBase* ref)
    {
        _paintDependencies.erase(ref);
        _persistentDependencies.erase(ref);
    }

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
    /// Self hit-test contract, called by the tree's topmost-first walker to
    /// decide whether this widget (and only this widget) is hit at
    /// `logicalPoint`. The default is `isHitTestableSelf() &&
    /// hitTestLayoutRect(logicalPoint)`. Layout hosts may narrow their own hit
    /// region by overriding this (e.g. a split pane only reports a hit over
    /// its divider strip, so its full-area rect never steals hover from an
    /// overlapping child). Children are always tested before self, so a child
    /// hit inside the narrowed region still wins.
    [[nodiscard]] virtual bool hitTestSelf(const glm::vec2& logicalPoint) const
    {
        return isHitTestableSelf() && hitTestLayoutRect(logicalPoint);
    }

    /// Authoring-only: attach a child to a detached subtree (UIDocument
    /// instantiate). The child must not be attached anywhere; tree membership
    /// is assigned when the subtree root is attached to a WidgetTree.
    void addDetachedChild(const UIElementRef& child);
    void addDetachedChild(const UIElementRef& child, FChildSlotInitializer init);
    void initializeChildSlot(UIElement& child, FChildSlotInitializer init);

  protected:
    /// Called when setLayoutRect detects a rect change (GI-304). Clip hosts
    /// override it to invalidate their subtree's cached draw-item segments: a
    /// changed clip rect invalidates every descendant's resolved clip even
    /// when the descendant's own rect is unchanged, so the base GeometryChanged
    /// mark (self only) is not enough.
    virtual void onLayoutRectChanged() {}

    /// Store the widget's final layout rect (clamping negative extents, per
    /// the layout contract) and mark it paint-dirty when the rect actually
    /// moved/resized — a changed rect invalidates the draw items cached from
    /// the previous rect (they carry the old pixel positions). Every
    /// layout/layoutAssigned override must route its rect assignment through
    /// here so a layout change propagates to the incremental paint cache.
    void setLayoutRect(const Rect2D& rect)
    {
        Rect2D clamped = rect;
        clamped.extent = glm::max(clamped.extent, glm::vec2(0.0f));
        if (clamped.pos != _layoutRect.pos || clamped.extent != _layoutRect.extent) {
            markPaintDirty(EUIInvalidationReason::GeometryChanged);
            onLayoutRectChanged();
        }
        _layoutRect = clamped;
    }

    /// Anchor math: rect.min = parent.pos + parent.size*anchorMin + _position;
    /// rect.max = parent.pos + parent.size*anchorMax + _position + _size.
    [[nodiscard]] Rect2D computeAnchorRect(const Rect2D& parentRect) const;

  public:
    /// Resolve this element's rect from a parent-provided canvas slot (anchor
    /// Min/Max + offset + min/max clamps) against `parentRect`. Size resolution
    /// keeps the historical contract: an axis with an anchor span stretches to
    /// the parent, an AutoSize axis uses computeDesiredSize(), otherwise the
    /// axis keeps this element's own size.
    [[nodiscard]] Rect2D resolveCanvasRect(const Rect2D&    parentRect,
                                           const glm::vec2& anchorMin,
                                           const glm::vec2& anchorMax,
                                           const glm::vec2& offset,
                                           const glm::vec2& minSize,
                                           const glm::vec2& maxSize) const;

  protected:
    /// Lay out direct children within `layoutRect` (paint order).
    void layoutChildren(const Rect2D& layoutRect);
    /// Recursively paint children in paint order. Virtual so layout hosts
    /// (Container/ScrollViewport/SplitPane) customize the children clip /
    /// traversal context while the base paint keeps the single self
    /// rebuild/reuse pipeline (GI-302).
    virtual void paintChildren(UIFrameBuilder& builder);
    /// Subclasses draw themselves here (base: no-op).
    virtual void paintSelf(UIFrameBuilder& builder) { (void)builder; }
    /// Clear this widget's paint-collected reactive dependencies before
    /// re-running its paint (dirty widget re-collects from scratch). Does NOT
    /// touch persistent (bind-time) edges. Implemented in .cpp.
    void clearDependencies();
    /// Clear this widget's persistent (bind-time) reactive dependencies. Called
    /// on unbind/rebind/destruction, not on paint re-collection. Implemented in
    /// .cpp.
    void clearPersistentDependencies();
    /// Factory for one parent-owned child edge. Generic elements create a
    /// plain UISlot; layout hosts override with a concrete slot type.
    [[nodiscard]] virtual std::unique_ptr<UISlot> createSlotForChild(UIElement& child);

  private:
    friend struct WidgetTree;
    friend class UITypeRegistry;
    friend struct UIDocument;

    /// Visual parent / tree hold strong references to children; the child
    /// points back with a raw (non-owning) pointer.
    std::vector<UIElementRef> _children;
    std::vector<UIBehaviorRef> _behaviors;
    std::vector<std::unique_ptr<UISlot>> _childSlots;
    UIElement*                _parent = nullptr;
    WidgetTree*               _tree   = nullptr;

    /// Module-owner lease set by UITypeRegistry::createInstance: keeps the
    /// owning module alive and decrements its live-instance count when this
    /// element is destroyed.
    std::shared_ptr<void> _moduleLease;

    /// Paint-dirty flag (reactive invalidation): set by ReactiveBase::notify-
    /// Dependents, cleared after this widget re-runs its paintSelf.
    bool _bPaintDirty = false;
    /// Most recent invalidation reason recorded on this widget (diagnostics).
    /// Updated on the 0->1 dirty transition; cleared alongside _bPaintDirty
    /// after a rebuild so a clean frame reads back as None.
    EUIInvalidationReason _lastInvalidationReason = EUIInvalidationReason::None;
    /// Reactive refs this widget read during its last paint walk. Cleared
    /// before a dirty widget re-runs its paint.
    std::unordered_set<ReactiveBase*> _paintDependencies;
    /// Reactive refs this widget bound at bind time (split ratio, style).
    /// Cleared only on unbind/rebind/destruction.
    std::unordered_set<ReactiveBase*> _persistentDependencies;

    void appendChildEdge(const UIElementRef& child);
    void appendChildEdge(const UIElementRef& child, FChildSlotInitializer init);
    void insertChildEdge(size_t index, const UIElementRef& child);
    void insertChildEdge(size_t index, const UIElementRef& child, FChildSlotInitializer init);
    void removeChildEdge(UIElement& child);
    void finalizeInsertedChild(const UIElementRef& child);
};

/// A paint-affecting boolean flag whose only write path marks the owning
/// widget paint-dirty. Transient input state (hover / pressed / focused /
/// dragging / highlight) is a paint attribute: mutating it invalidates the
/// widget's cached draw items, so a changed state actually re-paints. Wrapping
/// the flag in this type makes the invalidation impossible to forget — the
/// assignment operator is the sole write path and it always marks dirty.
class VisualFlag
{
public:
    explicit VisualFlag(UIElement& owner) : _owner(owner) {}

    VisualFlag(const VisualFlag&)            = delete;
    VisualFlag& operator=(const VisualFlag&) = delete;

    VisualFlag& operator=(bool value)
    {
        if (_value != value) {
            _value = value;
            _owner.markPaintDirty(EUIInvalidationReason::PaintProperty);
        }
        return *this;
    }

    [[nodiscard]] operator bool() const { return _value; }
    [[nodiscard]] bool get() const { return _value; }

  private:
    UIElement& _owner;
    bool       _value = false;
};

} // namespace ya
