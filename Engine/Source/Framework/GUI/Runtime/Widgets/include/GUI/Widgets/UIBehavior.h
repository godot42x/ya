#pragma once

#include "Core/Common/Types.h"
#include "Core/TypeIndex.h"
#include "GUI/Widgets/DragDropOperation.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ya
{

struct UIElement;
struct FDragDetectedEvent;
struct WidgetEventContext;
class Event;

/// What a behaviour can be asked to do. The set is closed: each value is a
/// generic dispatch point owned by the GUI framework (frame, input route,
/// action bubble). Behaviour *kinds* stay open -- any module derives new ones
/// without touching this list or the engine. A feature (drag, drop, tween) is
/// a behaviour kind, never a capability.
enum class EUIBehaviorCapability : uint8_t
{
    Tick,
    Input,
    Action,
    Count,
};

template <typename TSelf, typename... TCapabilities>
struct UIBehaviorWith;

// === Capability interfaces ===
// Constructible only as a base of UIBehaviorWith<...>: inheriting one without
// declaring it there does not compile, so no behaviour implements a capability
// the framework never dispatches to.

/// Per-frame work while the owner is visible in the tree.
struct IUITickable
{
    static constexpr EUIBehaviorCapability kCapability = EUIBehaviorCapability::Tick;

    [[nodiscard]] virtual bool wantsTick() const { return true; }
    virtual void               tick(UIElement& owner, float deltaSeconds) = 0;

  protected:
    ~IUITickable() = default;

  private:
    template <typename, typename...>
    friend struct UIBehaviorWith;
    IUITickable() = default;
};

/// The owner's input route phases, before the owner's own handling.
struct IUIInputHandler
{
    static constexpr EUIBehaviorCapability kCapability = EUIBehaviorCapability::Input;

    virtual bool previewInputEvent(UIElement& /*owner*/, const Event& /*event*/, const WidgetEventContext& /*ctx*/) { return false; }
    virtual bool handleInputEvent(UIElement& /*owner*/, const Event& /*event*/, const WidgetEventContext& /*ctx*/) { return false; }
    virtual bool bubbleInputEvent(UIElement& /*owner*/, const Event& /*event*/, const WidgetEventContext& /*ctx*/) { return false; }

  protected:
    ~IUIInputHandler() = default;

  private:
    template <typename, typename...>
    friend struct UIBehaviorWith;
    IUIInputHandler() = default;
};

/// A named action emitted by `source` (the owner or a descendant) is bubbling
/// through the owner (WidgetTree::emitAction). Return true to consume it.
struct IUIActionHandler
{
    static constexpr EUIBehaviorCapability kCapability = EUIBehaviorCapability::Action;

    virtual bool onAction(UIElement& owner, UIElement& source, std::string_view action) = 0;

  protected:
    ~IUIActionHandler() = default;

  private:
    template <typename, typename...>
    friend struct UIBehaviorWith;
    IUIActionHandler() = default;
};

/// The capability table: one interface per EUIBehaviorCapability, in enum order.
using FUIBehaviorCapabilities = std::tuple<IUITickable, IUIInputHandler, IUIActionHandler>;

namespace detail
{
template <size_t... I>
consteval bool capabilityTableMatchesEnum(std::index_sequence<I...>)
{
    return ((std::tuple_element_t<I, FUIBehaviorCapabilities>::kCapability == static_cast<EUIBehaviorCapability>(I)) && ...);
}
template <typename TTable>
struct TBehaviorIndexOf;
template <typename... TInterfaces>
struct TBehaviorIndexOf<std::tuple<TInterfaces...>>
{
    using type = std::tuple<std::vector<TInterfaces*>...>;
};
} // namespace detail

static_assert(std::tuple_size_v<FUIBehaviorCapabilities> == static_cast<size_t>(EUIBehaviorCapability::Count));
static_assert(detail::capabilityTableMatchesEnum(std::make_index_sequence<std::tuple_size_v<FUIBehaviorCapabilities>>{}));

/// A widget's behaviours indexed two ways: by kind (`kinds`, parallel to
/// UIElement::_behaviors) for findBehavior<T>(), and by capability for dispatch.
struct FUIBehaviorIndex
{
    std::vector<type_index_t>                                 kinds;
    detail::TBehaviorIndexOf<FUIBehaviorCapabilities>::type byCapability;
};

/// A part attached to a widget that is not the widget's own type. Carries only
/// ownership, lifecycle and its kind; everything it can be asked to do comes
/// from the capabilities it declares through UIBehaviorWith<Self, ...>. A
/// widget holds at most one behaviour per kind; a behaviour that needs several
/// instances of its work (tweens) keeps them inside itself.
struct YA_GUI_API UIBehavior
{
  private:
    friend struct UIElement;
    template <typename, typename...>
    friend struct UIBehaviorWith;

    UIElement*   _owner        = nullptr;
    type_index_t _kind         = 0;
    uint8_t      _capabilities = 0;

  public:
    virtual ~UIBehavior() = default;

    [[nodiscard]] UIElement*   getOwner() const { return _owner; }
    /// type_index_v of the concrete behaviour type (compile-time, stable
    /// across modules).
    [[nodiscard]] type_index_t getKind() const { return _kind; }
    [[nodiscard]] bool         hasCapability(EUIBehaviorCapability capability) const
    {
        return (_capabilities & (1u << static_cast<uint8_t>(capability))) != 0;
    }

    virtual void onAttached(UIElement& owner);
    virtual void onDetached(UIElement& owner);

  protected:
    void invalidateOwnerPaint() const;
    void invalidateOwnerLayout() const;
    void invalidateOwnerSubtree() const;

  private:
    UIBehavior() = default;
    /// The interface sub-object for a declared capability. Asked once per
    /// capability when the behaviour joins or leaves a widget, never on dispatch.
    [[nodiscard]] virtual void* capabilityInterface(EUIBehaviorCapability capability) = 0;
};

/// Base of every behaviour. `TSelf` is the (final) behaviour type itself and
/// names its kind; `TCapabilities` are the interfaces it implements (possibly
/// none). The declaration is the whole registration.
template <typename TSelf, typename... TCapabilities>
struct UIBehaviorWith : UIBehavior, TCapabilities...
{
    static_assert(((std::tuple_element_t<static_cast<size_t>(TCapabilities::kCapability), FUIBehaviorCapabilities>::kCapability ==
                    TCapabilities::kCapability) && ...),
                  "UIBehaviorWith takes capability interfaces from FUIBehaviorCapabilities");

  protected:
    UIBehaviorWith()
    {
        static_assert(std::is_base_of_v<UIBehaviorWith, TSelf> && std::is_final_v<TSelf>,
                      "UIBehaviorWith<TSelf, ...>: TSelf is the final behaviour type deriving from it");
        _kind         = type_index_v<TSelf>;
        _capabilities = static_cast<uint8_t>((0u | ... | (1u << static_cast<uint8_t>(TCapabilities::kCapability))));
    }

  private:
    void* capabilityInterface(EUIBehaviorCapability capability) final
    {
        void* found = nullptr;
        (void)((capability == TCapabilities::kCapability && (found = static_cast<TCapabilities*>(this), true)) || ...);
        return found;
    }
};

using UIBehaviorRef = std::shared_ptr<UIBehavior>;

// === Stock behaviours ===

/// The owner as the source of a drag session. Press / capture / threshold go
/// through the owner's input route; the tree's drag detection asks
/// onDragDetected (detectDrag).
struct YA_GUI_API UIDragSourceBehavior final : public UIBehaviorWith<UIDragSourceBehavior, IUIInputHandler>
{
    std::function<UIDragDropOperationRef(UIElement& owner)> operationFactory;
    std::function<void(UIElement& owner, bool bPressed)>    setPressedState;
    /// Owner left the tree (after the pressed state was cleared).
    std::function<void(UIElement& owner)> onOwnerDetached;
    bool bCapturePointerOnPress = false;
    /// When true, captured mouse moves past `dragThreshold` start a drag
    /// session. Capture without this flag (or `bCapturePointerOnPress`) used
    /// to swallow moves and block the tree's drag detection.
    bool  bBeginDragFromCapturedMove = false;
    float dragThreshold              = 6.0f;

    bool                   handleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx) override;
    UIDragDropOperationRef onDragDetected(UIElement& owner, const FDragDetectedEvent& event);
    void                   onDetached(UIElement& owner) override;

  private:
    void releasePress(UIElement& owner);

    bool      _bPressed = false;
    glm::vec2 _pressPoint{0.0f, 0.0f};
};

/// The owner as a drop target of a drag session; the tree asks it through
/// acceptsDrop / previewsDrop / dropOnto / highlightDrop / hoverDrop.
struct YA_GUI_API UIDropTargetBehavior final : public UIBehaviorWith<UIDropTargetBehavior>
{
    std::function<bool(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)> canAccept;
    /// Hover preview may be shown before the point is a valid drop (dock
    /// chooser). Unset: same as canAccept.
    std::function<bool(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)> canPreview;
    std::function<void(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)> handleDrop;
    std::function<void(UIElement& owner, bool bHighlight)> setHighlightState;
    /// Point-sensitive hover feedback on every move while the owner is the
    /// active target (after the highlight turned on).
    std::function<void(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)> updateHover;
    /// Owner left the tree (after the highlight was cleared).
    std::function<void(UIElement& owner)> onOwnerDetached;

    [[nodiscard]] bool canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) const;
    [[nodiscard]] bool canPreviewDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) const;
    void               onDetached(UIElement& owner) override;
};

// === What the tree's drag session asks a widget ===
// A widget without the drag source / drop target behaviour answers "no".

[[nodiscard]] YA_GUI_API bool acceptsDrop(UIElement& target, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
[[nodiscard]] YA_GUI_API bool previewsDrop(UIElement& target, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
/// Delivered only when the target accepts the point; clears the highlight first.
YA_GUI_API void dropOnto(UIElement& target, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
YA_GUI_API void highlightDrop(UIElement& target, bool bHighlight);
/// Delivered only when the target previews the point.
YA_GUI_API void hoverDrop(UIElement& target, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
[[nodiscard]] YA_GUI_API UIDragDropOperationRef detectDrag(UIElement& source, const FDragDetectedEvent& event);

/// Forwards the owning tree's per-frame tick to a callback.
///
/// This is how a widget says "I need a frame" and gets it from the tree, rather
/// than the code that created it remembering to push a refresh every frame.
/// Attach it to the widget that owns the state and the frame loop reaches it
/// through the normal subtree walk, so a surface that only opens/closes the
/// widget never has to know it has per-frame work.
///
/// A widget that is not visible in the tree is not ticked, so this is for state
/// that only matters while shown; use it instead of a parallel clock.
struct YA_GUI_API UITickBehavior final : public UIBehaviorWith<UITickBehavior, IUITickable>
{
    std::function<void(UIElement& owner, float deltaSeconds)> onTick;

    [[nodiscard]] bool wantsTick() const override { return static_cast<bool>(onTick); }
    void               tick(UIElement& owner, float deltaSeconds) override
    {
        if (onTick) {
            onTick(owner, deltaSeconds);
        }
    }
};

} // namespace ya
